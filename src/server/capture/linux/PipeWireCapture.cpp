// PipeWireCapture.cpp — Linux PipeWire 屏幕捕获实现
#ifdef Q_OS_LINUX

#include "PipeWireCapture.h"
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/pod/builder.h>
#include <QScreen>
#include <QGuiApplication>
#include <QThread>
#include <QtDebug>

// ---- PipeWire 流事件回调 ----

/// 构建 SPA POD 格式参数（视频流协商）
static const spa_pod* buildFormatParams(spa_pod_builder* builder)
{
    spa_rectangle minSize = SPA_RECTANGLE(1, 1);
    spa_rectangle maxSize = SPA_RECTANGLE(7680, 4320);
    spa_fraction framerate = SPA_FRACTION(0, 1);

    spa_pod_frame formatFrame;
    spa_pod_builder_push_object(builder, &formatFrame,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(builder,
        SPA_FORMAT_mediaType,    SPA_POD_Id(SPA_MEDIA_TYPE_VIDEO),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_RGB),
        0);
    spa_pod_builder_prop(builder, SPA_FORMAT_VIDEO_size,
        SPA_POD_CHOICE_RANGE_Rectangle(&maxSize, &minSize, &maxSize));
    spa_pod_builder_prop(builder, SPA_FORMAT_VIDEO_framerate,
        SPA_POD_CHOICE_RANGE_Fraction(&framerate, &framerate, &framerate));

    return static_cast<const spa_pod*>(spa_pod_builder_pop(builder, &formatFrame));
}

static void onStreamParamChanged(void* userData, uint32_t id,
                                  const struct spa_pod* param)
{
    if (!param || id != SPA_PARAM_Format)
        return;

    auto* self = static_cast<PipeWireCapture*>(userData);

    uint32_t mediaType = 0, mediaSubtype = 0;
    if (spa_format_parse(param, &mediaType, &mediaSubtype) < 0)
        return;

    if (mediaType != SPA_MEDIA_TYPE_VIDEO ||
        mediaSubtype != SPA_MEDIA_SUBTYPE_RGB)
        return;

    spa_format_video_raw_parse(param, &self->m_format);
    self->m_width  = self->m_format.size.width;
    self->m_height = self->m_format.size.height;
    self->m_stride = self->m_format.size.width * 4;  // RGBA 默认步幅
}

static void onStreamProcess(void* userData)
{
    auto* self = static_cast<PipeWireCapture*>(userData);
    struct pw_buffer* b = pw_stream_dequeue_buffer(self->m_pwStream);
    if (!b) return;

    struct spa_buffer* buf = b->buffer;
    void* src = buf->datas[0].data;
    if (src) {
        int w = self->m_width;
        int h = self->m_height;
        int stride = self->m_stride > 0 ? self->m_stride : w * 4;

        QImage img(static_cast<const uchar*>(src), w, h, stride,
                   QImage::Format_RGBA8888);

        QMutexLocker locker(&self->m_mutex);
        self->m_currentFrame = img.copy();  // 深拷贝（流缓冲生命周期短）
        self->m_frameReady = true;
    }

    pw_stream_queue_buffer(self->m_pwStream, b);
}

// ---- PipeWireCapture 实现 ----

PipeWireCapture::PipeWireCapture() = default;

PipeWireCapture::~PipeWireCapture()
{
    shutdown();
}

bool PipeWireCapture::isAvailable()
{
    // 运行时检测 PipeWire 是否可用
    return !qgetenv("PIPEWIRE_REMOTE").isEmpty()
        || !qgetenv("WAYLAND_DISPLAY").isEmpty();
}

bool PipeWireCapture::initialize(int outputIndex)
{
    Q_UNUSED(outputIndex);

    if (m_initialized) return true;

    pw_init(nullptr, nullptr);

    m_pwLoop = pw_thread_loop_new("PipeWireCapture", nullptr);
    if (!m_pwLoop) {
        m_lastError = QStringLiteral("PipeWire: 无法创建线程循环");
        return false;
    }

    m_pwContext = pw_context_new(pw_thread_loop_get_loop(m_pwLoop), nullptr, 0);
    if (!m_pwContext) {
        m_lastError = QStringLiteral("PipeWire: 无法创建上下文");
        return false;
    }

    m_pwCore = pw_context_connect(m_pwContext, nullptr, 0);
    if (!m_pwCore) {
        m_lastError = QStringLiteral("PipeWire: 无法连接到 PipeWire 守护进程");
        return false;
    }

    // 创建视频流
    m_pwStream = pw_stream_new(m_pwCore, "CrossRemoteDesktop",
                                pw_properties_new(
                                    PW_KEY_MEDIA_TYPE, "Video",
                                    PW_KEY_MEDIA_CATEGORY, "Capture",
                                    PW_KEY_MEDIA_ROLE, "Screen",
                                    nullptr));
    if (!m_pwStream) {
        m_lastError = QStringLiteral("PipeWire: 无法创建流");
        return false;
    }

    static const pw_stream_events streamEvents = {
        PW_VERSION_STREAM_EVENTS,
        .param_changed = onStreamParamChanged,
        .process = onStreamProcess,
    };

    // 分配监听器句柄在堆上（生命周期匹配 m_pwStream）
    m_streamListener = static_cast<spa_hook*>(calloc(1, sizeof(spa_hook)));
    pw_stream_add_listener(m_pwStream, m_streamListener,
                           &streamEvents, this);

    // 构造格式参数并连接流
    uint8_t buffer[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod* params = buildFormatParams(&builder);

    int ret = pw_stream_connect(m_pwStream, PW_DIRECTION_INPUT,
                                 PW_ID_ANY,
                                 static_cast<pw_stream_flags>(
                                     PW_STREAM_FLAG_AUTOCONNECT |
                                     PW_STREAM_FLAG_MAP_BUFFERS),
                                 &params, 1);
    if (ret < 0) {
        m_lastError = QString("PipeWire: 流连接失败 (%1)").arg(ret);
        return false;
    }

    // 启动事件循环
    if (pw_thread_loop_start(m_pwLoop) < 0) {
        m_lastError = QStringLiteral("PipeWire: 线程循环启动失败");
        return false;
    }

    // 等待首帧
    int waited = 0;
    const int maxWaitMs = 3000;
    while (!m_frameReady && waited < maxWaitMs) {
        QThread::msleep(50);
        waited += 50;
    }

    if (!m_frameReady) {
        m_lastError = QStringLiteral("PipeWire: 等待首帧超时");
        return false;
    }

    m_desktopSize = m_currentFrame.size();
    m_initialized = true;
    return true;
}

void PipeWireCapture::shutdown()
{
    if (m_pwStream) {
        pw_stream_destroy(m_pwStream);
        m_pwStream = nullptr;
    }
    if (m_streamListener) {
        free(m_streamListener);
        m_streamListener = nullptr;
    }
    if (m_pwCore) {
        pw_core_disconnect(m_pwCore);
        m_pwCore = nullptr;
    }
    if (m_pwContext) {
        pw_context_destroy(m_pwContext);
        m_pwContext = nullptr;
    }
    if (m_pwLoop) {
        pw_thread_loop_stop(m_pwLoop);
        pw_thread_loop_destroy(m_pwLoop);
        m_pwLoop = nullptr;
    }
    m_initialized = false;
}

bool PipeWireCapture::isInitialized() const
{
    return m_initialized;
}

CaptureResult PipeWireCapture::captureFrame(int timeoutMs)
{
    Q_UNUSED(timeoutMs);
    CaptureResult result;
    if (!m_initialized)
        return result;

    QMutexLocker locker(&m_mutex);
    if (m_frameReady) {
        result.frame = m_currentFrame;
        m_frameReady = false;
    }
    return result;
}

CursorMessage PipeWireCapture::sampleCursorPosition() const
{
    return {};
}

QSize PipeWireCapture::desktopSize() const
{
    return m_desktopSize;
}

QString PipeWireCapture::lastError() const
{
    return m_lastError;
}

bool PipeWireCapture::reinitialize()
{
    shutdown();
    return initialize(0);
}

#endif // Q_OS_LINUX
