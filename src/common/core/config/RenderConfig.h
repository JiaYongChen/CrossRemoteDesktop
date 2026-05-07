#pragma once

#include <QtCore/QSettings>

namespace RenderConfig {

enum class FrameDropPolicy {
    KeepOldest,   ///< Drop new frames when queue is full (legacy behavior)
    KeepLatest,   ///< Drop oldest frame when queue is full, keep newest
    LatestOnly,   ///< Queue capacity=1, always only hold the latest frame (lowest latency)
};

struct ClientFrame {
    FrameDropPolicy dropPolicy = FrameDropPolicy::LatestOnly;
    int queueCapacity = 5;   ///< Only used by KeepLatest; LatestOnly forces 1
};

struct GL {
    bool vsyncEnabled = true;
    bool usePbo = true;
};

struct Client {
    ClientFrame frame;
    GL gl;
};

/// Load settings from QSettings (group: "RemoteDesktop/Render").
inline Client load() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("RemoteDesktop/Render"));
    Client cfg;
    const QString policy = settings.value(QStringLiteral("DropPolicy"),
                                          QStringLiteral("LatestOnly")).toString();
    if ( policy == QStringLiteral("KeepLatest") )
        cfg.frame.dropPolicy = FrameDropPolicy::KeepLatest;
    else if ( policy == QStringLiteral("KeepOldest") )
        cfg.frame.dropPolicy = FrameDropPolicy::KeepOldest;
    else
        cfg.frame.dropPolicy = FrameDropPolicy::LatestOnly;
    cfg.frame.queueCapacity = settings.value(QStringLiteral("QueueCapacity"), 5).toInt();
    cfg.gl.vsyncEnabled = settings.value(QStringLiteral("VSync"), true).toBool();
    cfg.gl.usePbo = settings.value(QStringLiteral("UsePbo"), true).toBool();
    settings.endGroup();
    return cfg;
}

} // namespace RenderConfig
