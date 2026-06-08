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

        const GLenum result = f->glClientWaitSync(m_slots[i].fence, 0, 0);

        if (result == 0x911A /* GL_ALREADY_SIGNALED */
            || result == 0x911C /* GL_CONDITION_SATISFIED */) {
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

        constexpr GLbitfield storageFlags = 0x0002 /* GL_MAP_WRITE_BIT */
                                          | 0x0040 /* GL_MAP_PERSISTENT_BIT */
                                          | 0x0080 /* GL_MAP_COHERENT_BIT */;
        glBufferStorageFn(GL_PIXEL_UNPACK_BUFFER, requiredBytes, nullptr, storageFlags);

        constexpr GLbitfield mapFlags = 0x0002 | 0x0040 | 0x0080;
        m_pbo[i].ptr = f->glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0,
                                           requiredBytes, mapFlags);
        if (!m_pbo[i].ptr) {
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
