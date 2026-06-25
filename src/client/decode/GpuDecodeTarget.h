#pragma once

#ifndef QT_NO_OPENGL

#include "IDecodeTarget.h"
#include <QtOpenGL/QOpenGLBuffer>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QOffscreenSurface>
#include <QtCore/QSize>
#include <atomic>

class QOpenGLContext;

/**
 * @brief IDecodeTarget 的 OpenGL 实现
 *
 * 从 GLTextureViewport 剥离 PBO 双缓冲 + 持久映射 + 纹理管理，
 * 使解码器通过抽象接口写入，不耦合 QOpenGLWidget。
 *
 * 双缓冲模型（消除 Worker/GUI 读写竞争）：
 *   - Worker 线程写入 texture[1 - displayIdx] 对应的 PBO
 *   - GUI 线程 (paintGL) 从 texture[displayIdx] 渲染
 *   - swapDisplay() 原子交换 displayIdx
 */
class GpuDecodeTarget : public IDecodeTarget,
                         protected QOpenGLFunctions {
public:
    static constexpr int kPboCount = 2;
    static constexpr int kTexCount = 2;
    static constexpr int kRGB = 3;

    explicit GpuDecodeTarget(QOpenGLContext* shareContext);
    ~GpuDecodeTarget() override;

    // ── IDecodeTarget 接口 ──
    [[nodiscard]] unsigned char* mapWriteBuffer(int width, int height) override;
    [[nodiscard]] GLsync commitWriteBuffer() override;
    [[nodiscard]] GLsync uploadPixels(const unsigned char* data,
                                       int width, int height) override;
    void swapDisplay() override;
    [[nodiscard]] GLuint displayTexture() const override;
    [[nodiscard]] int textureWidth() const override  { return m_texWidth; }
    [[nodiscard]] int textureHeight() const override { return m_texHeight; }

    // ── 生命周期 ──
    [[nodiscard]] bool initialize();
    void cleanup();
    [[nodiscard]] bool isReady() const { return m_ready; }
    [[nodiscard]] QOpenGLContext* workerContext() const { return m_workerContext; }
    [[nodiscard]] QOffscreenSurface* offscreenSurface() const { return m_offSurface; }
    /// 延迟创建工作线程 GL 上下文（在首次使用的线程上创建）
    [[nodiscard]] bool ensureWorkerContext();

private:
    bool ensureTextureSize(int width, int height);
    bool ensurePboSize(int width, int height);
    void createPersistentPBOs(int size);
    void destroyPersistentPBOs();

    QOpenGLContext*    m_shareContext = nullptr;
    QOpenGLContext*    m_workerContext = nullptr;
    QOffscreenSurface* m_offSurface = nullptr;
    GLuint             m_textureId[kTexCount] = {0, 0};
    std::atomic<int>   m_displayTexIndex{0};
    QOpenGLBuffer      m_pbo[kPboCount] = {
        QOpenGLBuffer(QOpenGLBuffer::PixelUnpackBuffer),
        QOpenGLBuffer(QOpenGLBuffer::PixelUnpackBuffer)
    };
    int                m_pboWriteIdx = 0;
    int                m_pboAllocatedBytes = 0;
    bool               m_usePersistent = false;
    GLuint             m_persistentPboId[kPboCount] = {0, 0};
    void*              m_persistentPtr[kPboCount] = {nullptr, nullptr};
    int                m_texWidth = 0;
    int                m_texHeight = 0;
    int                m_pendingWidth = 0;
    int                m_pendingHeight = 0;
    bool               m_ready = false;
};

#endif // QT_NO_OPENGL
