# 解码→渲染管线重构 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 DecodeWorker 回归纯 CPU（只做 JPEG 解码），GL 操作全部回到 GUI 线程，用 TextureRingBuffer 替代旧的三件套（TripleBuffer<FrameSlot> + 双缓冲纹理 + 同步信号）。

**Architecture:** DecodeThread 解码产出 `DecodedFrame` → 放入 `TripleBuffer<DecodedFrame>` → GUI 线程 `doPreRender()` 从 TripleBuffer 取出，通过 PBO 异步上传到 `TextureRingBuffer`（3 槽，frameId 门控）→ `pollFences()` 收割 fence 标记 Ready → `paintGL()` 纯绘制。所有 GL 操作在 GUI 线程的原生上下文中闭环，零跨线程 fence。

**Tech Stack:** Qt 6.9+, OpenGL 3.3+, C++20, CMake

---

## File Structure

| 文件 | 操作 | 职责 |
|------|------|------|
| `src/client/window/TextureRingBuffer.h` | **新建** | 纹理环缓冲区：3 槽状态机、PBO、fence 管理、帧顺序门控 |
| `src/client/managers/DecodeWorker.h` | **重写** | 纯 CPU 解码：ThreadSafeQueue 入队 → JPEG 解码 → TripleBuffer 输出 |
| `src/client/managers/DecodeWorker.cpp` | **重写** | workLoop 极简实现 |
| `src/client/window/GLTextureViewport.h` | **修改** | 新增 m_inputBuffer/m_ringBuffer/m_fallbackTimer，删除旧缓冲/同步成员 |
| `src/client/window/GLTextureViewport.cpp` | **修改** | 新增 doPreRender/onFallbackTimer，paintGL 简化，删除旧上传/同步代码 |
| `src/client/managers/SessionManager.h` | **修改** | 删除 GL 相关成员/method，删除 frameBuffer() |
| `src/client/managers/SessionManager.cpp` | **修改** | createDecodePipeline/destroyDecodePipeline 简化 |
| `src/client/ClientManager.cpp` | **修改** | 删除 attachFrameBuffer/setGLContextForDecode/setGLViewportForUpload 的 GL 上下文连线 |
| `src/client/core/FrameSlot.h` | **删除** | 被 DecodedFrame + TextureRingBuffer::Slot 替代 |
| `src/CMakeLists.txt` | **修改** | 添加 TextureRingBuffer 源文件，移除 FrameSlot |
| `test/test_gl_worker_upload.cpp` | **修改** | 移除 FrameSlot/kPboCount/nextPboIndex 测试，保留 chooseGLFormat |
| `test/test_session_latest_wins.cpp` | **修改** | FrameSlot → DecodeWorker::DecodedFrame |
| `test/test_refresh_latency_metric.cpp` | **修改** | FrameSlot → DecodeWorker::DecodedFrame |
| `test/CMakeLists.txt` | **修改** | 移除 FrameSlot 依赖，添加 TextureRingBuffer 测试源文件引用 |

---

### Task 1: TextureRingBuffer 新类

**Files:**
- Create: `src/client/window/TextureRingBuffer.h`
- Create: `src/client/window/TextureRingBuffer.cpp`

- [ ] **Step 1: 创建 TextureRingBuffer.h 头文件**

```cpp
#pragma once

#ifndef QT_NO_OPENGL

#include <QtGui/qopengl.h>
#include <QtCore/QSize>
#include <QtGui/QImage>
#include <QtOpenGL/QOpenGLBuffer>
#include <atomic>
#include <array>

class QOpenGLFunctions;

/**
 * @brief 纹理环缓冲区 — 管理 N 槽异步纹理上传的完整生命周期。
 *
 * 每个槽位有三种状态：
 *   Free ── acquireWriteSlot() ──→ InFlight
 *                                     │ pollFences() signaled
 *                                     ↓
 *   Free ←── releaseSlot() ──── Ready
 *
 * 帧顺序保证：每个槽位携带单调递增的 frameId，getReadySlot(expectedId)
 * 只返回帧序号严格匹配的 Ready 槽。paintGL 按 frameId 顺序消费。
 *
 * 所有方法必须在拥有原生窗口的 GUI 线程的 GL 上下文中调用。
 */
class TextureRingBuffer {
public:
    static constexpr int kSlotCount = 3;

    enum class SlotState : quint8 {
        Free,
        InFlight,   // GPU DMA 进行中
        Ready       // fence signaled，等待 paintGL 消费
    };

    struct Slot {
        GLuint                textureId = 0;
        GLsync                fence     = nullptr;
        QSize                 size;
        std::atomic<SlotState> state{SlotState::Free};
        quint64               frameId   = 0;
    };

    explicit TextureRingBuffer() = default;
    ~TextureRingBuffer();

    // 禁止拷贝/移动
    TextureRingBuffer(const TextureRingBuffer&) = delete;
    TextureRingBuffer& operator=(const TextureRingBuffer&) = delete;

    // === GUI 线程 PreRender API（需 makeCurrent） ===

    /// 获取一个 Free 槽位，CAS 标记为 InFlight。
    /// @param frameId 由调用方分配的全局帧序号
    /// @return 槽位索引，-1 表示无可用槽（环满）
    int acquireWriteSlot(quint64 frameId);

    /// 将 QImage 异步上传到指定槽位的纹理（内部 PBO + glTexSubImage2D）。
    /// @return GLsync fence，若上传失败返回 nullptr
    GLsync uploadToSlot(int idx, const QImage& image);

    /// 记录 fence，正式提交异步上传。
    void submitSlot(int idx, GLsync fence);

    /// 取消写入：回退 InFlight → Free（上传失败时用）
    void cancelSlot(int idx);

    /// 检查所有 InFlight 槽位的 fence：
    /// signaled → InFlight → Ready（CAS），Ready 时通过回调通知外部
    /// @return 是否有槽位变为 Ready
    bool pollFences();

    // === GUI 线程 paintGL API ===

    /// 获取指定 frameId 的 Ready 槽位。
    /// @return 槽位索引，-1 表示该帧尚未就绪
    int getReadySlot(quint64 expectedFrameId) const;

    /// 是否有比 expected 更新的 Ready 帧（deltaReset 判断用）
    bool hasReadyNewerThan(quint64 expectedFrameId) const;

    /// 最新 Ready 帧的 frameId（deltaReset 跳转目标），-1 表示无
    qint64 newestReadyFrameId() const;

    /// 消费完毕：释放槽位回到 Free 状态
    void releaseSlot(int idx);

    // === 通用访问器 ===

    GLuint  textureId(int idx) const { return m_slots[idx].textureId; }
    quint64 frameId(int idx)   const { return m_slots[idx].frameId; }
    QSize   slotSize(int idx)  const { return m_slots[idx].size; }

    /// GUI 线程窗口关闭前清理所有 GL 资源（需 makeCurrent）
    void cleanup();

    /// 关闭前清理所有 in-flight 槽位的 fence（需 makeCurrent）
    void abortInFlight();

    /// 更新 viewport 的纹理尺寸（pollFences 检测到尺寸变更时更新 m_textureSize）
    QSize textureSize() const;

private:
    struct PboPair {
        GLuint buffer = 0;
        void*  ptr    = nullptr;   // 持久映射指针
    };

    bool tryInitPersistPbo(int requiredBytes);
    void destroyPersistPbo();

    static bool chooseGLFormat(QImage::Format f,
                               GLint&  internalFormat,
                               GLenum& format,
                               GLenum& type,
                               int&    bytesPerPixel);

    // 3 槽纹理环
    std::array<Slot, kSlotCount> m_slots;

    // 持久 PBO 双缓冲
    PboPair m_pbo[2];
    int     m_pboIndex   = 0;
    int     m_pboSize    = 0;
    bool    m_usePersistPbo = false;

    // 纹理尺寸缓存
    QSize m_textureSize;
};

#endif // QT_NO_OPENGL
```

- [ ] **Step 2: 创建 TextureRingBuffer.cpp 实现文件**

完整实现，逐方法写入：

```cpp
#ifndef QT_NO_OPENGL

#include "TextureRingBuffer.h"
#include "../../common/core/logging/LoggingCategories.h"

#include <QtGui/QOpenGLFunctions>
#include <QtGui/QOpenGLContext>
#include <algorithm>
#include <cstring>

// ---- 静态辅助 ----

bool TextureRingBuffer::chooseGLFormat(QImage::Format f,
                                       GLint&  internalFormat,
                                       GLenum& format,
                                       GLenum& type,
                                       int&    bytesPerPixel) {
    switch (f) {
        case QImage::Format_RGB888:
            internalFormat = 0x8051; // GL_RGB8
            format         = 0x1907; // GL_RGB
            type           = 0x1401; // GL_UNSIGNED_BYTE
            bytesPerPixel  = 3;
            return true;
        case QImage::Format_RGBA8888:
        case QImage::Format_RGBA8888_Premultiplied:
            internalFormat = 0x8058; // GL_RGBA8
            format         = 0x1908; // GL_RGBA
            type           = 0x1401; // GL_UNSIGNED_BYTE
            bytesPerPixel  = 4;
            return true;
        default:
            return false;
    }
}

// ---- 构造/析构 ----

TextureRingBuffer::~TextureRingBuffer() {
    // 必须在 ~GLTextureViewport 的 makeCurrent 之后调用 cleanup()，
    // 此析构函数仅作最后兜底（不应到达此处）。
}

// ---- 槽位管理 ----

int TextureRingBuffer::acquireWriteSlot(quint64 frameId) {
    for (int i = 0; i < kSlotCount; ++i) {
        SlotState expected = SlotState::Free;
        if (m_slots[i].state.compare_exchange_strong(expected, SlotState::InFlight,
                                                     std::memory_order_acq_rel)) {
            m_slots[i].frameId = frameId;
            return i;
        }
    }
    return -1;  // 环满
}

void TextureRingBuffer::submitSlot(int idx, GLsync fence) {
    m_slots[idx].fence = fence;
    // state 保持 InFlight，等待 pollFences 收割
}

void TextureRingBuffer::cancelSlot(int idx) {
    m_slots[idx].fence = nullptr;
    m_slots[idx].state.store(SlotState::Free, std::memory_order_release);
}

bool TextureRingBuffer::pollFences() {
    auto* f = QOpenGLContext::currentContext()->extraFunctions();
    if (!f) return false;

    bool anyReady = false;

    for (int i = 0; i < kSlotCount; ++i) {
        SlotState current = m_slots[i].state.load(std::memory_order_acquire);
        if (current != SlotState::InFlight) continue;
        if (!m_slots[i].fence) continue;

        // 零超时 — 不阻塞 GUI 线程
        const GLenum result = f->glClientWaitSync(m_slots[i].fence, 0, 0);

        if (result == 0x911A /* GL_ALREADY_SIGNALED */
            || result == 0x911C /* GL_CONDITION_SATISFIED */) {
            f->glDeleteSync(m_slots[i].fence);
            m_slots[i].fence = nullptr;

            // 同步纹理尺寸（尺寸变更的槽位变 Ready 时更新）
            if (!m_slots[i].size.isEmpty()) {
                m_textureSize = m_slots[i].size;
            }

            m_slots[i].state.store(SlotState::Ready, std::memory_order_release);
            anyReady = true;
        }
        // GL_TIMEOUT_EXPIRED: fence 尚未就绪，下一轮再检查
    }

    return anyReady;
}

int TextureRingBuffer::getReadySlot(quint64 expectedFrameId) const {
    for (int i = 0; i < kSlotCount; ++i) {
        if (m_slots[i].state.load(std::memory_order_acquire) == SlotState::Ready
            && m_slots[i].frameId == expectedFrameId) {
            return i;
        }
    }
    return -1;
}

bool TextureRingBuffer::hasReadyNewerThan(quint64 expectedFrameId) const {
    for (int i = 0; i < kSlotCount; ++i) {
        if (m_slots[i].state.load(std::memory_order_acquire) == SlotState::Ready
            && m_slots[i].frameId > expectedFrameId) {
            return true;
        }
    }
    return false;
}

qint64 TextureRingBuffer::newestReadyFrameId() const {
    qint64 best = -1;
    for (int i = 0; i < kSlotCount; ++i) {
        if (m_slots[i].state.load(std::memory_order_acquire) == SlotState::Ready) {
            if (static_cast<qint64>(m_slots[i].frameId) > best) {
                best = static_cast<qint64>(m_slots[i].frameId);
            }
        }
    }
    return best;
}

void TextureRingBuffer::releaseSlot(int idx) {
    m_slots[idx].state.store(SlotState::Free, std::memory_order_release);
}

// ---- PBO 管理 ----

bool TextureRingBuffer::tryInitPersistPbo(int requiredBytes) {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return false;

    auto* f = ctx->extraFunctions();
    if (!f) return false;

    // 检查 GL_ARB_buffer_storage 扩展
    if (!ctx->hasExtension(QByteArrayLiteral("GL_ARB_buffer_storage")))
        return false;

    using GLBufferStorageProc = void (*)(GLenum, GLsizeiptr, const void*, GLbitfield);
    auto glBufferStorageFn = reinterpret_cast<GLBufferStorageProc>(
        ctx->getProcAddress(QByteArrayLiteral("glBufferStorage")));
    if (!glBufferStorageFn) return false;

    for (int i = 0; i < 2; ++i) {
        f->glGenBuffers(1, &m_pbo[i].buffer);
        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo[i].buffer);

        constexpr GLbitfield storageFlags = 0x0002 /* GL_MAP_WRITE_BIT */
                                          | 0x0040 /* GL_MAP_PERSISTENT_BIT */
                                          | 0x0080 /* GL_MAP_COHERENT_BIT */;
        glBufferStorageFn(GL_PIXEL_UNPACK_BUFFER, requiredBytes, nullptr, storageFlags);

        constexpr GLbitfield mapFlags = 0x0002 | 0x0040 | 0x0080;
        m_pbo[i].ptr = f->glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0,
                                           requiredBytes, mapFlags);
        if (!m_pbo[i].ptr) {
            // 失败回退：释放已创建的第一个 PBO
            f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            if (i == 0) {
                f->glDeleteBuffers(1, &m_pbo[0].buffer);
                m_pbo[0].buffer = 0;
            }
            return false;
        }

        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    }

    m_pboSize        = requiredBytes;
    m_usePersistPbo  = true;
    qCInfo(lcGLViewport) << "Persistent PBOs created:"
                         << requiredBytes << "bytes x 2";
    return true;
}

void TextureRingBuffer::destroyPersistPbo() {
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    auto* f = ctx ? ctx->extraFunctions() : nullptr;

    for (int i = 0; i < 2; ++i) {
        if (m_pbo[i].buffer != 0 && f) {
            f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo[i].buffer);
            if (m_pbo[i].ptr) {
                f->glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
                m_pbo[i].ptr = nullptr;
            }
            f->glDeleteBuffers(1, &m_pbo[i].buffer);
            m_pbo[i].buffer = 0;
        } else if (m_pbo[i].buffer != 0) {
            m_pbo[i].ptr = nullptr;
            m_pbo[i].buffer = 0;
        }
    }
    m_usePersistPbo = false;
    m_pboSize       = 0;
}

// ---- GL 上传 ----

GLsync TextureRingBuffer::uploadToSlot(int idx, const QImage& image) {
    if (image.isNull() || image.format() == QImage::Format_Invalid)
        return nullptr;

    Slot& slot = m_slots[idx];

    // 确定 GL 像素格式
    GLint  internalFormat;
    GLenum format, type;
    int    bpp;
    QImage converted;
    const QImage* src = &image;

    if (!chooseGLFormat(image.format(), internalFormat, format, type, bpp)) {
        converted = image.convertedTo(QImage::Format_RGBA8888);
        chooseGLFormat(QImage::Format_RGBA8888, internalFormat, format, type, bpp);
        src = &converted;
    }

    const bool sizeChanged = (src->size() != slot.size);

    if (sizeChanged) {
        // 销毁旧纹理 + 创建新纹理
        if (slot.textureId != 0) {
            glDeleteTextures(1, &slot.textureId);
        }

        glGenTextures(1, &slot.textureId);
        glBindTexture(GL_TEXTURE_2D, slot.textureId);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, src->bytesPerLine() / bpp);
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat,
                     src->width(), src->height(), 0,
                     format, type, src->constBits());
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        slot.size = src->size();
    } else {
        const int totalBytes = src->bytesPerLine() * src->height();

        // 懒初始化持久 PBO
        if (!m_usePersistPbo && totalBytes > 0) {
            tryInitPersistPbo(totalBytes);
        }

        if (m_usePersistPbo && m_pbo[m_pboIndex].ptr) {
            // PBO 尺寸变更 → 重建
            if (totalBytes != m_pboSize) {
                destroyPersistPbo();
                tryInitPersistPbo(totalBytes);
            }

            if (m_usePersistPbo && m_pbo[m_pboIndex].ptr) {
                // 持久映射 PBO：直接 memcpy
                std::memcpy(m_pbo[m_pboIndex].ptr, src->constBits(),
                            static_cast<size_t>(totalBytes));

                auto* f = QOpenGLContext::currentContext()->extraFunctions();
                f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo[m_pboIndex].buffer);

                glBindTexture(GL_TEXTURE_2D, slot.textureId);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, src->bytesPerLine() / bpp);
                // PBO 绑定后指针为偏移量，nullptr = 偏移 0
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                src->width(), src->height(),
                                format, type, nullptr);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

                f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                m_pboIndex = (m_pboIndex + 1) % 2;
            }
        } else {
            // Fallback：CPU 直传
            glBindTexture(GL_TEXTURE_2D, slot.textureId);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, src->bytesPerLine() / bpp);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            src->width(), src->height(),
                            format, type, src->constBits());
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
    }

    // 插入 GPU fence
    auto* f = QOpenGLContext::currentContext()->extraFunctions();
    Q_ASSERT(f);
    GLsync fence = f->glFenceSync(0x9117 /* GL_SYNC_GPU_COMMANDS_COMPLETE */, 0);
    glFlush();

    return fence;
}

// ---- 清理 ----

void TextureRingBuffer::abortInFlight() {
    auto* f = QOpenGLContext::currentContext()->extraFunctions();
    for (int i = 0; i < kSlotCount; ++i) {
        SlotState expected = SlotState::InFlight;
        if (m_slots[i].state.compare_exchange_strong(expected, SlotState::Free,
                                                     std::memory_order_acq_rel)) {
            if (m_slots[i].fence && f) {
                f->glDeleteSync(m_slots[i].fence);
                m_slots[i].fence = nullptr;
            }
        }
    }
}

void TextureRingBuffer::cleanup() {
    destroyPersistPbo();

    auto* f = QOpenGLContext::currentContext()->extraFunctions();
    for (int i = 0; i < kSlotCount; ++i) {
        // 防御性清理：如果有残留 fence
        if (m_slots[i].fence && f) {
            f->glDeleteSync(m_slots[i].fence);
            m_slots[i].fence = nullptr;
        }
        if (m_slots[i].textureId != 0) {
            glDeleteTextures(1, &m_slots[i].textureId);
            m_slots[i].textureId = 0;
        }
        m_slots[i].state.store(SlotState::Free, std::memory_order_release);
        m_slots[i].size = QSize();
        m_slots[i].frameId = 0;
    }

    m_textureSize = QSize();
}

QSize TextureRingBuffer::textureSize() const {
    return m_textureSize;
}

#endif // QT_NO_OPENGL
```

- [ ] **Step 3: 提交 TextureRingBuffer**

```bash
git add src/client/window/TextureRingBuffer.h src/client/window/TextureRingBuffer.cpp
git commit -m "feat: 新增 TextureRingBuffer 纹理环缓冲区类

三槽纹理环，替代旧的双缓冲纹理 + TripleBuffer<FrameSlot> + 同步信号。
支持异步 PBO 上传、frameId 顺序门控、deltaReset。
所有 GL 操作在 GUI 线程闭环，无跨线程 fence 依赖。"
```

---

### Task 2: DecodeWorker 重写

**Files:**
- Modify: `src/client/managers/DecodeWorker.h`
- Modify: `src/client/managers/DecodeWorker.cpp`

- [ ] **Step 1: 重写 DecodeWorker.h**

完全替换文件内容：

```cpp
#pragma once

#include <QtCore/QObject>
#include <QtCore/QSize>
#include <QtGui/QImage>
#include <atomic>
#include <chrono>
#include "../../common/core/network/Protocol.h"
#include "../../common/core/threading/ThreadSafeQueue.h"
#include "../core/TripleBuffer.h"

/**
 * @brief 纯 CPU 解码 Worker — JPEG 解码后通过 TripleBuffer 输出到 GUI 线程。
 *
 * 不再触碰 GL。DecodeThread 上运行 workLoop()，产出 DecodedFrame
 * 放入 TripleBuffer，emit frameDecoded 通知 GUI 线程做 GPU 上传。
 */
class DecodeWorker : public QObject {
    Q_OBJECT

public:
    /// 解码后的帧，通过 TripleBuffer 传递给 GUI 线程
    struct DecodedFrame {
        QImage   image;
        QSize    remoteSize;
        quint64  frameId = 0;
    };

    explicit DecodeWorker(QObject* parent = nullptr);
    ~DecodeWorker() override;

    /// 由 SessionManager 调用，投递待解码的帧（线程安全）
    bool enqueueFrame(ScreenData screenData, const QSize& remoteSize);

    /// 设置输出 TripleBuffer 指针
    void setOutputBuffer(TripleBuffer<DecodedFrame>* buf);

    /// 停止工作循环
    void requestStop();

    /// 是否正在运行
    bool isRunning() const { return m_running.load(); }

signals:
    void decodeError(const QString& message);
    void stopped();
    /// 新帧已写入 TripleBuffer，通知 GUI 线程做 PreRender
    void frameDecoded(quint64 frameId);

public slots:
    void start();

private slots:
    void workLoop();

private:
    struct DecodeTask {
        ScreenData screenData;
        QSize      remoteSize;
    };

    ThreadSafeQueue<DecodeTask> m_queue{4};
    TripleBuffer<DecodedFrame>* m_outputBuffer = nullptr;
    QImage m_decodeBuffer;
    std::atomic<quint64> m_nextFrameId{1};
    std::atomic<bool> m_running{false};
};
```

- [ ] **Step 2: 重写 DecodeWorker.cpp**

完全替换文件内容：

```cpp
#include "DecodeWorker.h"
#include "../../common/core/logging/LoggingCategories.h"
#include <QtCore/QBuffer>
#include <QtCore/QImageReader>
#include <QtCore/QThread>

// ---- 构造/析构 ----

DecodeWorker::DecodeWorker(QObject* parent)
    : QObject(parent) {
}

DecodeWorker::~DecodeWorker() {
    requestStop();
}

// ---- 公共接口 ----

bool DecodeWorker::enqueueFrame(ScreenData screenData, const QSize& remoteSize) {
    if (!m_running.load()) {
        return false;
    }
    DecodeTask task;
    task.screenData = std::move(screenData);
    task.remoteSize = remoteSize;
    return m_queue.tryEnqueue(std::move(task));
}

void DecodeWorker::setOutputBuffer(TripleBuffer<DecodedFrame>* buf) {
    m_outputBuffer = buf;
}

void DecodeWorker::requestStop() {
    m_running.store(false);
}

void DecodeWorker::start() {
    m_running.store(true);
    // start() 通过 QueuedConnection 在 DecodeThread 中调用
    workLoop();
}

// ---- 工作循环 ----

void DecodeWorker::workLoop() {
    qCInfo(lcClient) << "DecodeWorker::workLoop() - Starting decode loop";

    while (m_running.load()) {
        // 1. 出队
        DecodeTask task;
        if (!m_queue.tryDequeue(task)) {
            // 队列空 — 退避 1ms 避免 CPU 空转
            QThread::msleep(1);
            continue;
        }

        // 2. JPEG 解码
        QBuffer buffer(const_cast<QByteArray*>(&task.screenData.imageData));
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "JPEG");
        reader.setAutoTransform(true);
        if (!reader.read(&m_decodeBuffer) || m_decodeBuffer.isNull()) {
            qCWarning(lcClient) << "DecodeWorker: JPEG decode failed, size:"
                                << task.screenData.imageData.size();
            emit decodeError(QStringLiteral("JPEG 解码失败"));
            continue;
        }

        // 确定 remoteSize：非缩放场景使用解码后的图像尺寸
        QSize remoteSize = task.remoteSize;
        if (!(task.screenData.flags & static_cast<quint8>(ScreenDataFlags::SCALED))
            || task.screenData.originalWidth == 0) {
            remoteSize = m_decodeBuffer.size();
        }

        // 3. 输出到 TripleBuffer（移动语义零拷贝）
        if (!m_outputBuffer) {
            qCWarning(lcClient) << "DecodeWorker: outputBuffer is null, dropping frame";
            continue;
        }

        DecodedFrame* frame = nullptr;
        int idx = m_outputBuffer->acquireWrite(frame);
        if (idx >= 0 && frame) {
            quint64 fid = m_nextFrameId.fetch_add(1, std::memory_order_relaxed);
            frame->image      = std::move(m_decodeBuffer);
            frame->remoteSize = remoteSize;
            frame->frameId    = fid;
            m_outputBuffer->commitWrite(idx);
            emit frameDecoded(fid);
        }
        // idx < 0: TripleBuffer 满，优雅丢弃帧
    }

    qCInfo(lcClient) << "DecodeWorker::workLoop() - Decode loop ended";
    emit stopped();
}
```

- [ ] **Step 3: 提交 DecodeWorker 重写**

```bash
git add src/client/managers/DecodeWorker.h src/client/managers/DecodeWorker.cpp
git commit -m "refactor: DecodeWorker 回归纯 CPU，移除所有 GL 相关代码

- 删除 m_glContext/m_glSurface/m_glUploadReady/m_glViewport
- 删除 initializeGL/cleanupGL/moveGLToThread/setGLViewport
- 删除 processOneFrame/processFrameDecodeAndUpload 复杂管线
- 删除 PredecodeSlot/predecodeSignal/renderingDoneSignal
- 新增 DecodedFrame 结构体，通过 TripleBuffer<DecodedFrame> 输出
- 新增 frameDecoded 信号通知 GUI 线程
- workLoop 简化为：出队 → JPEG 解码 → move 到 TripleBuffer → emit 信号"
```

---

### Task 3: GLTextureViewport 修改

**Files:**
- Modify: `src/client/window/GLTextureViewport.h`
- Modify: `src/client/window/GLTextureViewport.cpp`

- [ ] **Step 1: 修改 GLTextureViewport.h — 删除旧成员，新增新成员**

改动点：

**删除以下声明：**
- `static constexpr int kPboCount = 2;` 和 `static int nextPboIndex(int current)`
- `uploadFromWorker(const QImage& image)` 方法声明
- `requestRepaint()` 方法声明
- `consecutiveSkips()` 方法声明
- `attachFrameBuffer(TripleBuffer<FrameSlot>* buffer)` 方法声明
- `setRemoteSize(const QSize& size)` 方法声明 — 坐标映射由 RenderManager 接管
- `remoteSize()` 方法声明
- `mapToRemote/mapFromRemote` 方法声明
- `renderNow()` — 保留但简化
- `predecodeSignal()` / `renderingDoneSignal()` 方法声明
- `CheckForNewFrameAfterPaint()` 私有方法声明

**删除以下成员变量：**
- `GLuint m_textureId[2] = {0, 0};`
- `std::atomic<int> m_displayTexIndex{0};`
- `QOpenGLBuffer m_pbo[kPboCount]`
- `bool m_usePbo`
- `bool m_usePersistentPbo`
- `void* m_persistentPtr[kPboCount]`
- `GLuint m_persistentId[kPboCount]`
- `int m_sharedPboIndex`
- `int m_currentPbo`
- `int m_pboAllocatedBytes`
- `bool m_textureDirty`
- `std::atomic<int> m_consecutiveSkips{0}`
- `std::atomic<bool> m_needsRepaint{false}`
- `TripleBuffer<FrameSlot>* m_frameBuffer = nullptr`
- `int m_consumedSlot = -1`
- `std::atomic<bool> m_predecodeSignal{false}`
- `std::atomic<bool> m_renderingDoneSignal{false}`
- `std::chrono::steady_clock::time_point m_pendingArrivalTs{}`
- `quint64 m_metricsFrameCount`
- `qint64 m_metricsLatencyAccumUs`
- `qint64 m_metricsLatencyMaxUs`
- `static constexpr quint64 kMetricsReportInterval = 10`
- `QSize m_remoteSize` — 坐标映射移交 RenderManager
- `createPersistentPBOs()` / `destroyPersistentPBOs()` 私有方法声明

**新增：**
```cpp
// 头文件顶部新增 include
#include "../managers/DecodeWorker.h"  // DecodedFrame
#include "TextureRingBuffer.h"

// 类内新增成员
public:
    /// 对外暴露输入 TripleBuffer（SessionManager 创建管线时用）
    TripleBuffer<DecodeWorker::DecodedFrame>* inputBuffer() { return &m_inputBuffer; }

public slots:
    /// 解码线程产出新帧时通过 QueuedConnection 调用
    void doPreRender();

private:
    /// 保底定时器：没有新帧时收割残留 fence
    void onFallbackTimer();

    // === 新成员 ===
    TripleBuffer<DecodeWorker::DecodedFrame> m_inputBuffer;
    TextureRingBuffer m_ringBuffer;
    QTimer* m_fallbackTimer = nullptr;
    quint64 m_lastRenderedFrameId = 0;
```

**保留不变：**
- `m_shaderProgram`, `m_vao`, `m_vertexBuffer`
- `m_renderRect`, `m_textureSize`
- `m_glInitialized`, `m_glCleanedUp`
- `m_vsyncEnabled`, `m_pollTimer`
- `glContextReady` 信号
- `chooseGLFormat(GLPixelLayout&)` — 静态方法签名保留
- `initializeGL()`, `resizeGL()`, `paintGL()` — 签名保留，实现重写
- `cleanupGLResources()`, `cleanupGL()`
- `isVSyncEnabled()`, `setVSyncEnabled()`
- `hasTexture()`, `textureSize()`, `renderRect()`
- `configurePollTimer()`, `initializeShaders()`, `initializeGeometry()`, `updateRenderRect()`

完整修改后的头文件：

```cpp
#pragma once

#ifndef QT_NO_OPENGL

#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QtOpenGL/QOpenGLShaderProgram>
#include <QtOpenGL/QOpenGLBuffer>
#include <QtOpenGL/QOpenGLVertexArrayObject>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QImage>
#include <QtCore/QSize>
#include <QtCore/QPoint>
#include <QtCore/QRectF>
#include <QtCore/QTimer>
#include <chrono>
#include <atomic>

#include "../core/TripleBuffer.h"
#include "../managers/DecodeWorker.h"
#include "TextureRingBuffer.h"

/**
 * @brief OpenGL viewport — 纯渲染 + PreRender 上传。
 *
 * PreRender（doPreRender）由 DecodeWorker::frameDecoded 信号驱动，
 * 从 TripleBuffer<DecodedFrame> 取帧，PBO 异步上传到 TextureRingBuffer。
 *
 * paintGL 从 TextureRingBuffer 中按 frameId 顺序取出 Ready 的纹理，
 * 绑定并绘制。paintGL 不再做任何上传或 fence 等待。
 *
 * 所有 GL 操作在 GUI 线程的原生上下文中闭环。
 */
class GLTextureViewport : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT

public:
    struct GLPixelLayout {
        GLint  internalFormat;
        GLenum format;
        GLenum type;
        int    bytesPerPixel;
    };

    /// Pure function mapping QImage::Format to GL upload parameters.
    static bool chooseGLFormat(QImage::Format f, GLPixelLayout& out);

    explicit GLTextureViewport(QWidget* parent = nullptr);
    ~GLTextureViewport() override;

    void setVSyncEnabled(bool on);
    bool isVSyncEnabled() const { return m_vsyncEnabled; }

    /// Force an immediate repaint.
    void renderNow();

    bool hasTexture() const { return m_ringBuffer.textureSize().isValid(); }
    QSize textureSize() const { return m_ringBuffer.textureSize(); }
    QRectF renderRect() const;

    QPoint mapToRemote(const QPoint& localPoint) const;
    QPoint mapFromRemote(const QPoint& remotePoint) const;

    /// 对外暴露输入 TripleBuffer（SessionManager 创建解码管线时用）
    TripleBuffer<DecodeWorker::DecodedFrame>* inputBuffer() { return &m_inputBuffer; }

    void cleanupGLResources();

public slots:
    /// 解码线程产出新帧时通过 QueuedConnection 调用
    void doPreRender();

signals:
    void renderRectChanged(const QRectF& rect);
    void glContextReady(QOpenGLContext* context);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    bool initializeShaders();
    void initializeGeometry();
    void updateRenderRect();
    void configurePollTimer();
    void cleanupGL();
    void onFallbackTimer();

    // 着色器/几何
    QOpenGLShaderProgram* m_shaderProgram = nullptr;
    QOpenGLBuffer m_vertexBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_vao;

    // 渲染状态
    QRectF m_renderRect;
    bool   m_glInitialized = false;
    bool   m_glCleanedUp   = false;

    // VSync / 轮询
    bool    m_vsyncEnabled = true;
    QTimer* m_pollTimer    = nullptr;

    // === 新管线组件 ===

    // 输入：DecodeThread → GUI 线程
    TripleBuffer<DecodeWorker::DecodedFrame> m_inputBuffer;

    // 纹理环：异步 PBO 上传 + frameId 顺序门控
    TextureRingBuffer m_ringBuffer;

    // 保底定时器（16ms，无新帧时收割残留 fence）
    QTimer* m_fallbackTimer = nullptr;

    // paintGL 已渲染的最后帧序号
    quint64 m_lastRenderedFrameId = 0;
};

#endif // QT_NO_OPENGL
```

- [ ] **Step 2: 修改 GLTextureViewport.cpp — 构造/析构/initializeGL/resizeGL**

改动：构造/析构中删除旧 PBO/纹理/fence 初始化，新增 fallbackTimer。

```cpp
GLTextureViewport::GLTextureViewport(QWidget* parent)
    : QOpenGLWidget(parent)
    , m_pollTimer(new QTimer(this))
    , m_fallbackTimer(new QTimer(this))   // ← 新增
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    const auto cfg = RenderConfig::load();
    m_vsyncEnabled = cfg.gl.vsyncEnabled;
    format.setSwapInterval(m_vsyncEnabled ? 1 : 0);
    setFormat(format);

    // VSync 关闭时的轮询定时器（同旧代码）
    m_pollTimer->setInterval(16);
    m_pollTimer->setTimerType(Qt::PreciseTimer);
    connect(m_pollTimer, &QTimer::timeout, this, [this]() { update(); });

    // 保底定时器：无新帧时收割残留 fence（16ms 同轮询周期）
    m_fallbackTimer->setInterval(16);
    m_fallbackTimer->setTimerType(Qt::PreciseTimer);
    connect(m_fallbackTimer, &QTimer::timeout, this, &GLTextureViewport::onFallbackTimer);
}
```

析构函数：删除旧 PBO 清理代码，改为清理 ringBuffer：

```cpp
GLTextureViewport::~GLTextureViewport() {
    if (!m_glCleanedUp) {
        if (m_pollTimer)    m_pollTimer->stop();
        if (m_fallbackTimer) m_fallbackTimer->stop();

        if (QOpenGLContext* ctx = context()) {
            disconnect(ctx, &QOpenGLContext::aboutToBeDestroyed,
                       this, &GLTextureViewport::cleanupGL);
        }

        makeCurrent();
        cleanupGL();
        doneCurrent();
    } else {
        if (m_pollTimer)    m_pollTimer->stop();
        if (m_fallbackTimer) m_fallbackTimer->stop();
    }
}
```

`initializeGL()`: 删除 PBO 创建代码，删除 `glContextReady` 信号中的 DecodeWorker 上下文连线（不再需要共享上下文）：

```cpp
void GLTextureViewport::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    if (!initializeShaders()) {
        qCCritical(lcGLViewport) << "Failed to initialize shaders";
        return;
    }

    initializeGeometry();

    m_glInitialized = true;

    connect(context(), &QOpenGLContext::aboutToBeDestroyed,
            this, &GLTextureViewport::cleanupGL);

    qCInfo(lcGLViewport) << "OpenGL initialized:"
        << "vendor:" << reinterpret_cast<const char*>(glGetString(GL_VENDOR))
        << "renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER))
        << "version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));

    // 保留 glContextReady 信号（不依赖它的旧逻辑会被移除，但 ClientManager 不再连线）
    emit glContextReady(context());
}
```

`resizeGL()`: 不变。

- [ ] **Step 3: 修改 GLTextureViewport.cpp — 新增 doPreRender / onFallbackTimer**

```cpp
void GLTextureViewport::doPreRender() {
    // 首次调用时启动保底定时器（懒启动，避免构造时无连接也运行）
    if (m_fallbackTimer && !m_fallbackTimer->isActive()) {
        m_fallbackTimer->start();
    }

    // 从 TripleBuffer 读取解码帧（非阻塞）
    DecodeWorker::DecodedFrame* frame = nullptr;
    int idx = m_inputBuffer.getReadSlot(frame);

    if (idx >= 0 && frame && !frame->image.isNull()) {
        makeCurrent();

        // 尝试获取空闲槽位
        int slotIdx = m_ringBuffer.acquireWriteSlot(frame->frameId);
        if (slotIdx >= 0) {
            GLsync fence = m_ringBuffer.uploadToSlot(slotIdx, frame->image);
            if (fence) {
                m_ringBuffer.submitSlot(slotIdx, fence);

                // remoteSize 存储在 DecodedFrame 中，RenderManager 需要时可从
                // DecodedFrame::remoteSize 或 ringBuffer 纹理尺寸获取
            } else {
                m_ringBuffer.cancelSlot(slotIdx);
            }
        }
        // 环满：帧被优雅丢弃

        doneCurrent();
    }

    // 收割 fence
    makeCurrent();
    bool ready = m_ringBuffer.pollFences();
    doneCurrent();

    // 有新 Ready 帧 → 触发 paintGL
    if (ready) {
        update();
    }
}

void GLTextureViewport::onFallbackTimer() {
    makeCurrent();
    bool ready = m_ringBuffer.pollFences();
    doneCurrent();
    if (ready) {
        update();
    }
}
```

- [ ] **Step 4: 修改 GLTextureViewport.cpp — paintGL 重写为纯渲染**

完全替换旧的 paintGL 实现：

```cpp
void GLTextureViewport::paintGL() {
    // GL 资源已清理（窗口关闭中），跳过
    if (!m_shaderProgram) {
        return;
    }

    // 1. 找期望帧
    int idx = m_ringBuffer.getReadySlot(m_lastRenderedFrameId + 1);

    // 2. deltaReset：放弃等待旧帧，跳到最新 Ready
    if (idx < 0 && m_ringBuffer.hasReadyNewerThan(m_lastRenderedFrameId)) {
        qint64 newest = m_ringBuffer.newestReadyFrameId();
        if (newest > 0) {
            m_lastRenderedFrameId = static_cast<quint64>(newest);
            idx = m_ringBuffer.getReadySlot(m_lastRenderedFrameId);
        }
    }

    // 3. 无可用帧 → 保持上一帧画面
    if (idx < 0) {
        return;
    }

    // 4. 更新纹理尺寸（转由 ringBuffer 管理）
    if (m_renderRect.isEmpty() && m_ringBuffer.textureSize().isValid()) {
        updateRenderRect();
    }

    // 5. 纯绘制
    glClear(GL_COLOR_BUFFER_BIT);

    GLuint texId = m_ringBuffer.textureId(idx);
    if (texId == 0 || m_renderRect.isEmpty()) {
        return;
    }

    const qreal dpr = devicePixelRatioF();
    const int rx = static_cast<int>(m_renderRect.x() * dpr);
    const int ry = static_cast<int>((height() - m_renderRect.bottom()) * dpr);
    const int rw = static_cast<int>(m_renderRect.width() * dpr);
    const int rh = static_cast<int>(m_renderRect.height() * dpr);
    glViewport(rx, ry, rw, rh);

    m_shaderProgram->bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texId);
    m_shaderProgram->setUniformValue("uTexture", 0);

    QOpenGLVertexArrayObject::Binder vaoBinder(&m_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_shaderProgram->release();

    // 6. 释放槽位
    m_lastRenderedFrameId = m_ringBuffer.frameId(idx);
    m_ringBuffer.releaseSlot(idx);
}
```

- [ ] **Step 5: 修改 GLTextureViewport.cpp — cleanupGL/cleanupGLResources**

```cpp
void GLTextureViewport::cleanupGL() {
    // 清理纹理环（PBO + 纹理）
    m_ringBuffer.cleanup();

    // 原有清理逻辑
    m_vertexBuffer.destroy();
    m_vao.destroy();
    delete m_shaderProgram;
    m_shaderProgram = nullptr;
    m_glInitialized = false;
}

void GLTextureViewport::cleanupGLResources() {
    if (m_glCleanedUp) return;

    qCInfo(lcGLViewport) << "GLTextureViewport::cleanupGLResources()";

    if (m_pollTimer)    m_pollTimer->stop();
    if (m_fallbackTimer) m_fallbackTimer->stop();

    if (QOpenGLContext* ctx = context()) {
        disconnect(ctx, &QOpenGLContext::aboutToBeDestroyed,
                   this, &GLTextureViewport::cleanupGL);
    }

    makeCurrent();
    cleanupGL();
    doneCurrent();

    m_glCleanedUp = true;
}
```

- [ ] **Step 6: 修改 GLTextureViewport.cpp — 辅助方法**

`configurePollTimer()`: 改为同时管理 pollTimer 和 fallbackTimer。

```cpp
void GLTextureViewport::configurePollTimer() {
    if (!m_pollTimer || !m_fallbackTimer) return;

    // VSync 关闭 → 启用轮询
    const bool needPolling = !m_vsyncEnabled;
    if (needPolling && !m_pollTimer->isActive()) {
        m_pollTimer->start();
    } else if (!needPolling && m_pollTimer->isActive()) {
        m_pollTimer->stop();
    }

    // fallbackTimer 总是需要（收割残留 fence），在连接建立后启动
}
```

`renderNow()`: 简化为 `update()`。

```cpp
void GLTextureViewport::renderNow() {
    update();
}
```

`mapToRemote/mapFromRemote`: 使用 `m_ringBuffer.textureSize()` 替代 `m_textureSize` + `m_remoteSize`：

```cpp
QPoint GLTextureViewport::mapToRemote(const QPoint& localPoint) const {
    if (m_renderRect.isEmpty()) return localPoint;

    const QSize targetSize = m_ringBuffer.textureSize();
    if (targetSize.isEmpty()) return localPoint;

    const qreal normX = (localPoint.x() - m_renderRect.x()) / m_renderRect.width();
    const qreal normY = (localPoint.y() - m_renderRect.y()) / m_renderRect.height();
    const qreal clampedX = qBound(0.0, normX, 1.0);
    const qreal clampedY = qBound(0.0, normY, 1.0);

    return QPoint(
        static_cast<int>(clampedX * targetSize.width()),
        static_cast<int>(clampedY * targetSize.height()));
}

QPoint GLTextureViewport::mapFromRemote(const QPoint& remotePoint) const {
    if (m_renderRect.isEmpty()) return remotePoint;

    const QSize targetSize = m_ringBuffer.textureSize();
    if (targetSize.isEmpty()) return remotePoint;

    const qreal normX = static_cast<qreal>(remotePoint.x()) / targetSize.width();
    const qreal normY = static_cast<qreal>(remotePoint.y()) / targetSize.height();

    return QPoint(
        static_cast<int>(normX * m_renderRect.width() + m_renderRect.x()),
        static_cast<int>(normY * m_renderRect.height() + m_renderRect.y()));
}
```

`setVSyncEnabled`: 保留不变（修改 format + configurePollTimer）。

- [ ] **Step 7: 提交 GLTextureViewport 修改**

```bash
git add src/client/window/GLTextureViewport.h src/client/window/GLTextureViewport.cpp
git commit -m "refactor: GLTextureViewport 简化 — 新增 TextureRingBuffer，paintGL 纯渲染

- 删除双缓冲纹理 m_textureId[2]/m_displayTexIndex
- 删除旧 PBO 全部成员（m_pbo/m_persistentPbo 等）
- 删除 uploadFromWorker/requestRepaint/attachFrameBuffer 等旧 API
- 删除 predecodeSignal/renderingDoneSignal 同步信号
- 删除 CheckForNewFrameAfterPaint/fence 等待/延迟度量
- 新增 TextureRingBuffer m_ringBuffer 纹理环
- 新增 TripleBuffer<DecodedFrame> m_inputBuffer
- 新增 doPreRender() — frameDecoded 驱动
- 新增 onFallbackTimer() — 保底 fence 收割
- paintGL 简化为纯：getReadySlot → bind → draw → releaseSlot"
```

---

### Task 4: SessionManager + ClientManager 修改

**Files:**
- Modify: `src/client/managers/SessionManager.h`
- Modify: `src/client/managers/SessionManager.cpp`
- Modify: `src/client/ClientManager.cpp`

- [ ] **Step 1: 修改 SessionManager.h — 删除 GL 相关声明**

删除以下声明：
```cpp
// 删除
FrameSlot.h include (line 11)
TripleBuffer<FrameSlot>* frameBuffer() { return &m_frameBuffer; }  // 方法
void setGLContextForDecode(QOpenGLContext* context);               // 方法
void setGLViewportForUpload(GLTextureViewport* vp);                // 方法
TripleBuffer<FrameSlot> m_frameBuffer;                             // 成员
GLTextureViewport* m_glViewportForUpload = nullptr;                // 成员
QOpenGLContext* m_pendingGLContext = nullptr;                      // 成员
```

新增：
```cpp
// 方法新增（替代 frameBuffer()）
void setGLViewport(GLTextureViewport* vp);
```

修改后的关键部分：

```cpp
#pragma once

#include <QtCore/QObject>
#include <QtCore/QSize>
#include <QtCore/QDateTime>
#include "../../common/core/network/Protocol.h"
#include "../../common/core/config/UiConstants.h"
#include "../network/ConnectionManager.h"
#include <chrono>
// 注意：不再 include FrameSlot.h, TripleBuffer.h
// TripleBuffer<DecodedFrame> 由 GLTextureViewport 持有

class DecodeWorker;
class QTimer;

#ifndef QT_NO_OPENGL
class GLTextureViewport;
#endif

class SessionManager : public QObject {
    Q_OBJECT
public:
    // ... PerformanceStats, 构造/析构, connectionId, session 控制 (不变)

#ifndef QT_NO_OPENGL
    /// 存储 GLTextureViewport 引用用于 createDecodePipeline
    void setGLViewport(GLTextureViewport* vp) { m_glViewport = vp; }
#endif

    // ... 公共方法 (不变，除了删除 frameBuffer/setGLContextForDecode/setGLViewportForUpload)

private:
    // ... (不变)

    // 解码管线
    DecodeWorker* m_decodeWorker = nullptr;

#ifndef QT_NO_OPENGL
    GLTextureViewport* m_glViewport = nullptr;  // 替代 m_glViewportForUpload
#endif
};
```

- [ ] **Step 2: 修改 SessionManager.cpp — 简化 createDecodePipeline/destroyDecodePipeline**

```cpp
void SessionManager::createDecodePipeline() {
    if (m_decodeWorker) {
        qCWarning(lcClient) << "SessionManager::createDecodePipeline() - DecodeWorker already exists";
        return;
    }

    // 创建 DecodeThread
    QThread* decodeThread = new QThread();
    decodeThread->setObjectName(QString("DecodeThread-%1").arg(m_connectionId));
    decodeThread->start();

    // 创建 DecodeWorker
    m_decodeWorker = new DecodeWorker(nullptr);
    m_decodeWorker->moveToThread(decodeThread);

    // 设置输出到 GLTextureViewport 的 inputBuffer
#ifndef QT_NO_OPENGL
    if (m_glViewport) {
        m_decodeWorker->setOutputBuffer(m_glViewport->inputBuffer());

        // 连接 frameDecoded → doPreRender（QueuedConnection 保证在 GUI 线程执行）
        connect(m_decodeWorker, &DecodeWorker::frameDecoded,
                m_glViewport, &GLTextureViewport::doPreRender,
                Qt::QueuedConnection);
    }
#endif

    // 连接 stopped/decodeError 信号
    connect(m_decodeWorker, &DecodeWorker::stopped, this, [this]() {
        qCInfo(lcClient) << "SessionManager: DecodeWorker stopped for" << m_connectionId;
    });

    connect(m_decodeWorker, &DecodeWorker::decodeError, this, [this](const QString& msg) {
        qCWarning(lcClient) << "SessionManager: Decode error for" << m_connectionId << ":" << msg;
    });

    decodeThread->setParent(m_decodeWorker);

    // 启动工作循环
    QMetaObject::invokeMethod(m_decodeWorker, "start", Qt::QueuedConnection);

    qCInfo(lcClient) << "SessionManager: DecodePipeline created for" << m_connectionId
                      << "on thread" << decodeThread->objectName();
}

void SessionManager::destroyDecodePipeline() {
    if (!m_decodeWorker) return;

    qCInfo(lcClient) << "SessionManager::destroyDecodePipeline() - Stopping decode pipeline for"
                     << m_connectionId;

    // 1. 断开 frameDecoded 信号（防止在 worker 析构时触发 doPreRender）
#ifndef QT_NO_OPENGL
    disconnect(m_decodeWorker, &DecodeWorker::frameDecoded,
               m_glViewport, &GLTextureViewport::doPreRender);
#endif

    // 2. 停止队列
    m_decodeWorker->requestStop();

    // 3. 停止 DecodeThread
    QThread* decodeThread = m_decodeWorker->thread();
    if (decodeThread && decodeThread->isRunning()) {
        decodeThread->quit();
        if (!decodeThread->wait(3000)) {
            qCWarning(lcClient) << "SessionManager::destroyDecodePipeline() - DecodeThread quit timeout, forcing";
            decodeThread->terminate();
            decodeThread->wait(1000);
        }
    }

    // 4. 删除 worker
    delete m_decodeWorker;
    m_decodeWorker = nullptr;

    qCInfo(lcClient) << "SessionManager::destroyDecodePipeline() - Decode pipeline destroyed for"
                     << m_connectionId;
}
```

`resetConnection()` 中删除 `m_frameBuffer.reset()`：

```cpp
void SessionManager::resetConnection() {
    m_lastFpsTime = {};
    m_smoothedFrameDuration = 0.0;
    m_remoteScreenSize = QSize();
    resetStats();
    // 删除: m_frameBuffer.reset();
    emit connectionReset();
    qCInfo(lcClient) << "SessionManager::resetConnection() - Connection state reset complete";
}
```

删除 `setGLContextForDecode()` 方法实现（整个方法移除）。

`handleScreenData()` 中关于 `m_glViewport` 的背压读取（`consecutiveSkips`）删除——旧代码中有对 `m_glViewport->consecutiveSkips()` 的引用？让我确认——旧代码中没有直接的 consecutiveSkips 调用在 handleScreenData 中，那个在旧架构里是 paintGL 写入、Worker 读取。新架构中 frameBackpressure 由环满自动处理。`handleScreenData` 无需改动。

- [ ] **Step 3: 修改 ClientManager.cpp — 简化 GL 上下文连线**

改动点：`connectToHost()` 中 293-319 行的 GL 上下文连线改为极简版本。

旧代码（295-319 行）：
```cpp
gl->attachFrameBuffer(instance->sessionManager->frameBuffer());
instance->sessionManager->setGLViewportForUpload(gl);
QObject::connect(gl, &GLTextureViewport::glContextReady,
    instance->sessionManager,
    [sm = instance->sessionManager.data()](QOpenGLContext* ctx) {
        sm->setGLContextForDecode(ctx);
    }, Qt::QueuedConnection);
if (gl->context() && gl->context()->isValid()) {
    instance->sessionManager->setGLContextForDecode(gl->context());
}
```

新代码：
```cpp
#ifndef QT_NO_OPENGL
{
    auto* gl = instance->remoteDesktopWindow->glViewport();
    if (gl && instance->sessionManager) {
        // 仅存储 viewport 引用，createDecodePipeline 中用它设置 inputBuffer + frameDecoded 连线
        instance->sessionManager->setGLViewport(gl);
    }
}
#endif
```

- [ ] **Step 4: 提交 SessionManager + ClientManager 修改**

```bash
git add src/client/managers/SessionManager.h src/client/managers/SessionManager.cpp src/client/ClientManager.cpp
git commit -m "refactor: 简化 SessionManager/ClientManager 的 GL 上下文连线

- SessionManager: 删除 frameBuffer()/setGLContextForDecode/setGLViewportForUpload
- SessionManager: 删除 TripleBuffer<FrameSlot>/m_pendingGLContext/m_glViewportForUpload
- SessionManager: 新增 setGLViewport → createDecodePipeline 用 inputBuffer + frameDecoded 信号
- SessionManager: createDecodePipeline 不再初始化共享 GL 上下文
- ClientManager: 删除 attachFrameBuffer + glContextReady 信号连线
- ClientManager: 仅调用 setGLViewport 即可"
```

---

### Task 5: 删除 FrameSlot + 更新测试

**Files:**
- Delete: `src/client/core/FrameSlot.h`
- Modify: `test/test_gl_worker_upload.cpp`
- Modify: `test/test_session_latest_wins.cpp`
- Modify: `test/test_refresh_latency_metric.cpp`

- [ ] **Step 1: 更新 test_gl_worker_upload.cpp — 移除 FrameSlot 测试，保留 chooseGLFormat**

删除：
- `#include "../src/client/core/FrameSlot.h"`
- `testFrameSlotFenceDefault()` 测试
- `testFrameSlotFenceCanBeSet()` 测试
- `testNextPboIndexWrap()` 测试（`nextPboIndex` 不再存在）
- `testKpboCountIsTwo()` 测试（`kPboCount` 不再存在）

保留：
- `chooseGLFormat` 四个测试

修改后的文件：

```cpp
#include <QtTest/QTest>
#include <QtGui/QImage>

#include "../src/client/window/GLTextureViewport.h"

class TestGLWorkerUpload : public QObject {
    Q_OBJECT

private slots:
    void testChooseGLFormatRGB888() {
        GLTextureViewport::GLPixelLayout layout{};
        QVERIFY(GLTextureViewport::chooseGLFormat(QImage::Format_RGB888, layout));
        QCOMPARE(layout.bytesPerPixel, 3);
        QCOMPARE(layout.format, GL_RGB);
    }

    void testChooseGLFormatRGBA8888() {
        GLTextureViewport::GLPixelLayout layout{};
        QVERIFY(GLTextureViewport::chooseGLFormat(QImage::Format_RGBA8888, layout));
        QCOMPARE(layout.bytesPerPixel, 4);
        QCOMPARE(layout.format, GL_RGBA);
    }

    void testChooseGLFormatRGBA8888Premultiplied() {
        GLTextureViewport::GLPixelLayout layout{};
        QVERIFY(GLTextureViewport::chooseGLFormat(
            QImage::Format_RGBA8888_Premultiplied, layout));
        QCOMPARE(layout.bytesPerPixel, 4);
        QCOMPARE(layout.format, GL_RGBA);
    }

    void testChooseGLFormatUnsupported() {
        GLTextureViewport::GLPixelLayout layout{};
        QVERIFY(!GLTextureViewport::chooseGLFormat(QImage::Format_Mono, layout));
        QVERIFY(!GLTextureViewport::chooseGLFormat(QImage::Format_Indexed8, layout));
    }
};

QTEST_APPLESS_MAIN(TestGLWorkerUpload)
#include "test_gl_worker_upload.moc"
```

- [ ] **Step 2: 更新 test_session_latest_wins.cpp — FrameSlot → DecodeWorker::DecodedFrame**

```cpp
#include <QtTest/QTest>
#include "../src/client/managers/DecodeWorker.h"  // DecodedFrame
#include "../src/client/core/TripleBuffer.h"

class TestSessionLatestWins : public QObject {
    Q_OBJECT
private slots:
    void testTripleBufferLatestWins() {
        // TripleBuffer latest-wins semantic: consumer always sees the latest
        // committed frame. Write 3 frames and verify only the last one is readable.
        TripleBuffer<DecodeWorker::DecodedFrame> tb;
        for (int i = 0; i < 3; ++i) {
            DecodeWorker::DecodedFrame* w = nullptr;
            int idx = tb.acquireWrite(w);
            QVERIFY(idx >= 0 && idx <= 2);
            w->frameId = static_cast<quint64>(i + 1);
            tb.commitWrite(idx);
        }
        DecodeWorker::DecodedFrame* r = nullptr;
        int rs = tb.getReadSlot(r);
        QVERIFY(rs >= 0);
        QCOMPARE(r->frameId, quint64(3));
    }

    void testDecodeWorkerExposesDecodedFrame() {
        // DecodedFrame is a public struct inside DecodeWorker
        DecodeWorker::DecodedFrame frame;
        frame.frameId = 42;
        QCOMPARE(frame.frameId, quint64(42));
        QVERIFY(frame.image.isNull());
        QVERIFY(frame.remoteSize.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestSessionLatestWins)
#include "test_session_latest_wins.moc"
```

- [ ] **Step 3: 更新 test_refresh_latency_metric.cpp — FrameSlot → DecodeWorker::DecodedFrame**

```cpp
#include <QtTest/QTest>
#include "../src/client/core/TripleBuffer.h"
#include "../src/client/managers/DecodeWorker.h"

class TestRefreshLatencyMetric : public QObject {
    Q_OBJECT
private slots:
    void testTripleBufferStoresFrame() {
        TripleBuffer<DecodeWorker::DecodedFrame> tb;
        DecodeWorker::DecodedFrame* w = nullptr;
        int idx = tb.acquireWrite(w);
        QVERIFY(idx >= 0);
        w->frameId = 1;
        w->remoteSize = QSize(1920, 1080);
        tb.commitWrite(idx);

        DecodeWorker::DecodedFrame* r = nullptr;
        int ridx = tb.getReadSlot(r);
        QVERIFY(ridx >= 0);
        QCOMPARE(r->frameId, quint64(1));
        QCOMPARE(r->remoteSize, QSize(1920, 1080));
    }

    void testTripleBufferEmptyReturnsNoSlot() {
        TripleBuffer<DecodeWorker::DecodedFrame> tb;
        DecodeWorker::DecodedFrame* r = nullptr;
        QCOMPARE(tb.getReadSlot(r), -1);
    }
};

QTEST_APPLESS_MAIN(TestRefreshLatencyMetric)
#include "test_refresh_latency_metric.moc"
```

- [ ] **Step 4: 删除 FrameSlot.h**

```bash
git rm src/client/core/FrameSlot.h
```

- [ ] **Step 5: 提交测试更新**

```bash
git add test/test_gl_worker_upload.cpp test/test_session_latest_wins.cpp test/test_refresh_latency_metric.cpp
git commit -m "refactor: 删除 FrameSlot，更新测试使用 DecodedFrame

- 删除 src/client/core/FrameSlot.h
- test_gl_worker_upload: 移除 FrameSlot/kPboCount/nextPboIndex 测试
- test_session_latest_wins: TripleBuffer<DecodedFrame> + DecodedFrame 结构测试
- test_refresh_latency_metric: 同 FrameSlot → DecodedFrame"
```

---

### Task 6: CMakeLists.txt 更新 + 构建验证

**Files:**
- Modify: `src/CMakeLists.txt`（或项目根 `CMakeLists.txt`，取决于源文件列表在哪里）

- [ ] **Step 1: 找到源文件列表位置并添加 TextureRingBuffer**

需要检查主 CMakeLists.txt 中是否显式列出了 client 源文件。如果是通过 `file(GLOB ...)` 自动收集的则无需修改。先检查：

```bash
# 查找 CMakeLists.txt 中是否有显式的客户端源文件列表
grep -n "FrameSlot\|GLTextureViewport\|DecodeWorker" CMakeLists.txt src/CMakeLists.txt 2>$null
```

如果没有显式列出（使用 GLOB），则无需修改 CMakeLists。如果有，添加 `src/client/window/TextureRingBuffer.h` 和 `src/client/window/TextureRingBuffer.cpp`，移除 `src/client/core/FrameSlot.h`。

对于测试的 CMakeLists.txt（`test/CMakeLists.txt`），检查是否有显式的 `FrameSlot.h` 引用或 include 路径需要更新。

```bash
grep -n "FrameSlot" test/CMakeLists.txt 2>$null
```

- [ ] **Step 2: 构建验证**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug 2>&1
```

预期：构建成功，零警告，零错误。

- [ ] **Step 3: 运行相关测试**

```bash
cd build && ctest --output-on-failure -R "triple_buffer|session_latest|refresh_latency|gl_worker_upload"
```

预期：全部 PASS。

- [ ] **Step 4: 提交 CMakeLists 更新**

```bash
git add CMakeLists.txt test/CMakeLists.txt  # 如有修改
git commit -m "build: 添加 TextureRingBuffer 源文件，移除 FrameSlot 引用"
```

---

### 执行顺序

Task 1 → Task 2 → Task 3 → Task 4 → Task 5 → Task 6

Task 1-2 是独立的新组件，可以并行。Task 3 依赖 Task 1。Task 4 依赖 Task 3。Task 5 是独立的清理任务（可和 Task 6 合并）。
