#pragma once

#ifndef QT_NO_OPENGL

#include <QtGui/qopengl.h>
#include <QtCore/QSize>
#include <QtGui/QImage>
#include <QtOpenGL/QOpenGLBuffer>
#include <atomic>
#include <array>

class QOpenGLFunctions;

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

    int acquireWriteSlot(quint64 frameId);

    GLsync uploadToSlot(int idx, const QImage& image);

    void submitSlot(int idx, GLsync fence);

    void cancelSlot(int idx);

    bool pollFences();

    // === GUI 线程 paintGL API ===

    int getReadySlot(quint64 expectedFrameId) const;

    bool hasReadyNewerThan(quint64 expectedFrameId) const;

    qint64 newestReadyFrameId() const;

    void releaseSlot(int idx);

    // === 通用访问器 ===

    GLuint  textureId(int idx) const { return m_slots[idx].textureId; }
    quint64 frameId(int idx)   const { return m_slots[idx].frameId; }
    QSize   slotSize(int idx)  const { return m_slots[idx].size; }

    void cleanup();

    void abortInFlight();

    QSize textureSize() const;

private:
    struct PboPair {
        GLuint buffer = 0;
        void*  ptr    = nullptr;
    };

    bool tryInitPersistPbo(int requiredBytes);
    void destroyPersistPbo();

    static bool chooseGLFormat(QImage::Format f,
                               GLint&  internalFormat,
                               GLenum& format,
                               GLenum& type,
                               int&    bytesPerPixel);

    std::array<Slot, kSlotCount> m_slots;

    PboPair m_pbo[2];
    int     m_pboIndex   = 0;
    int     m_pboSize    = 0;
    bool    m_usePersistPbo = false;

    QSize m_textureSize;
};

#endif // QT_NO_OPENGL
