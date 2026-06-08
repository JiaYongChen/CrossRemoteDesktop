#ifndef QT_NO_OPENGL

#include "TextureRingBuffer.h"
#include "../../common/core/logging/LoggingCategories.h"

#include <QtGui/QOpenGLExtraFunctions>
#include <QtGui/QOpenGLContext>
#include <algorithm>
#include <cstring>

// GL format constants not guaranteed by all GL headers
#ifndef GL_RGB8
#  define GL_RGB8  0x8051
#endif
#ifndef GL_RGBA8
#  define GL_RGBA8 0x8058
#endif
#ifndef GL_RGB
#  define GL_RGB  0x1907
#endif
#ifndef GL_RGBA
#  define GL_RGBA 0x1908
#endif

// GL fence-sync constants not guaranteed by all GL headers
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#  define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#  define GL_SYNC_FLUSH_COMMANDS_BIT    0x00000001
#endif
#ifndef GL_ALREADY_SIGNALED
#  define GL_ALREADY_SIGNALED           0x911A
#endif
#ifndef GL_CONDITION_SATISFIED
#  define GL_CONDITION_SATISFIED        0x911C
#endif
#ifndef GL_TIMEOUT_EXPIRED
#  define GL_TIMEOUT_EXPIRED            0x911B
#endif
#ifndef GL_WAIT_FAILED
#  define GL_WAIT_FAILED                0x911D
#endif

// GL_ARB_buffer_storage / GL 4.4+ constants
#ifndef GL_MAP_WRITE_BIT
#  define GL_MAP_WRITE_BIT       0x0002
#endif
#ifndef GL_MAP_PERSISTENT_BIT
#  define GL_MAP_PERSISTENT_BIT  0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#  define GL_MAP_COHERENT_BIT    0x0080
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#  define GL_MAP_INVALIDATE_BUFFER_BIT 0x0004
#endif

// ---- 静态辅助 ----

bool TextureRingBuffer::chooseGLFormat(QImage::Format f,
                                       GLint&  internalFormat,
                                       GLenum& format,
                                       GLenum& type,
                                       int&    bytesPerPixel) {
    switch (f) {
        case QImage::Format_RGB888:
            internalFormat = GL_RGB8;
            format         = GL_RGB;
            type           = GL_UNSIGNED_BYTE;
            bytesPerPixel  = 3;
            return true;
        case QImage::Format_RGBA8888:
        case QImage::Format_RGBA8888_Premultiplied:
            internalFormat = GL_RGBA8;
            format         = GL_RGBA;
            type           = GL_UNSIGNED_BYTE;
            bytesPerPixel  = 4;
            return true;
        default:
            return false;
    }
}

// ---- 构造/析构 ----

TextureRingBuffer::~TextureRingBuffer() {
    // cleanup() 应在 makeCurrent 后、析构前由 GLTextureViewport 显式调用。
    // 若此处仍有未释放的 GL 资源，说明调用方遗漏了 cleanup()。
    qCWarning(lcGLViewport) << "TextureRingBuffer destroyed without explicit cleanup()";
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
    return -1;
}

void TextureRingBuffer::submitSlot(int idx, GLsync fence) {
    m_slots[idx].fence = fence;
}

void TextureRingBuffer::cancelSlot(int idx) {
    // CAS InFlight→Free，确保与 pollFences() 无竞争。
    // 若 submitSlot() 已写入 fence，由调用方在上层负责删除——
    // cancelSlot 仅负责将状态机回退，不做 GL 资源释放。
    SlotState expected = SlotState::InFlight;
    if (m_slots[idx].state.compare_exchange_strong(expected, SlotState::Free,
                                                   std::memory_order_acq_rel)) {
        m_slots[idx].fence = nullptr;
    }
    // CAS 失败：槽位已不在 InFlight 状态（可能已被 pollFences 转为 Ready），
    // 此时调用方不应再操作该槽位。
}

bool TextureRingBuffer::pollFences() {
    auto* f = QOpenGLContext::currentContext()->extraFunctions();
    if (!f) return false;

    bool anyReady = false;

    for (int i = 0; i < kSlotCount; ++i) {
        SlotState current = m_slots[i].state.load(std::memory_order_acquire);
        if (current != SlotState::InFlight) continue;
        if (!m_slots[i].fence) continue;

        const GLenum result = f->glClientWaitSync(m_slots[i].fence,
                                                     GL_SYNC_FLUSH_COMMANDS_BIT, 0);

        if (result == GL_ALREADY_SIGNALED
            || result == GL_CONDITION_SATISFIED) {
            f->glDeleteSync(m_slots[i].fence);
            m_slots[i].fence = nullptr;

            if (!m_slots[i].size.isEmpty()) {
                m_textureSize = m_slots[i].size;
            }

            m_slots[i].state.store(SlotState::Ready, std::memory_order_release);
            anyReady = true;
        }
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

    if (!ctx->hasExtension(QByteArrayLiteral("GL_ARB_buffer_storage")))
        return false;

    using GLBufferStorageProc = void (*)(GLenum, GLsizeiptr, const void*, GLbitfield);
    auto glBufferStorageFn = reinterpret_cast<GLBufferStorageProc>(
        ctx->getProcAddress(QByteArrayLiteral("glBufferStorage")));
    if (!glBufferStorageFn) return false;

    for (int i = 0; i < 2; ++i) {
        f->glGenBuffers(1, &m_pbo[i].buffer);
        f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo[i].buffer);

        constexpr GLbitfield storageFlags = GL_MAP_WRITE_BIT
                                          | GL_MAP_PERSISTENT_BIT
                                          | GL_MAP_COHERENT_BIT;
        glBufferStorageFn(GL_PIXEL_UNPACK_BUFFER, requiredBytes, nullptr, storageFlags);

        constexpr GLbitfield mapFlags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        m_pbo[i].ptr = f->glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0,
                                           requiredBytes, mapFlags);
        if (!m_pbo[i].ptr) {
            f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            // PBO[i] 映射失败：清理已创建的 PBO[0..i]
            for (int j = 0; j <= i; ++j) {
                if (m_pbo[j].buffer != 0) {
                    f->glDeleteBuffers(1, &m_pbo[j].buffer);
                    m_pbo[j].buffer = 0;
                }
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

        if (!m_usePersistPbo && totalBytes > 0) {
            tryInitPersistPbo(totalBytes);
        }

        if (m_usePersistPbo && m_pbo[m_pboIndex].ptr) {
            if (totalBytes != m_pboSize) {
                destroyPersistPbo();
                tryInitPersistPbo(totalBytes);
            }

            if (m_usePersistPbo && m_pbo[m_pboIndex].ptr) {
                std::memcpy(m_pbo[m_pboIndex].ptr, src->constBits(),
                            static_cast<size_t>(totalBytes));

                auto* f = QOpenGLContext::currentContext()->extraFunctions();
                f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_pbo[m_pboIndex].buffer);

                glBindTexture(GL_TEXTURE_2D, slot.textureId);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, src->bytesPerLine() / bpp);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                src->width(), src->height(),
                                format, type, nullptr);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

                f->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                m_pboIndex = (m_pboIndex + 1) % 2;
            }
        } else {
            glBindTexture(GL_TEXTURE_2D, slot.textureId);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, src->bytesPerLine() / bpp);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            src->width(), src->height(),
                            format, type, src->constBits());
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
    }

    auto* f = QOpenGLContext::currentContext()->extraFunctions();
    if (!f) {
        qCWarning(lcGLViewport) << "uploadToSlot: extraFunctions() returned null";
        return nullptr;
    }
    GLsync fence = f->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
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
    m_pboIndex = 0;
}

QSize TextureRingBuffer::textureSize() const {
    return m_textureSize;
}

#endif // QT_NO_OPENGL
