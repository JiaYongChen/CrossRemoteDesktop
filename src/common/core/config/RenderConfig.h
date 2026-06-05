#pragma once

#include <QtCore/QSettings>

namespace RenderConfig {

struct GL {
    bool vsyncEnabled = true;
    bool usePbo = true;
    bool usePersistentPbo = true;
};

struct Client {
    GL gl;
};

/// Load settings from QSettings (group: "RemoteDesktop/Render").
inline Client load() {
    QSettings settings;
    settings.beginGroup(QStringLiteral("RemoteDesktop/Render"));
    Client cfg;
    cfg.gl.vsyncEnabled = settings.value(QStringLiteral("VSync"), true).toBool();
    cfg.gl.usePbo = settings.value(QStringLiteral("UsePbo"), true).toBool();
    cfg.gl.usePersistentPbo = settings.value(QStringLiteral("UsePersistentPbo"), true).toBool();
    settings.endGroup();
    return cfg;
}

} // namespace RenderConfig
