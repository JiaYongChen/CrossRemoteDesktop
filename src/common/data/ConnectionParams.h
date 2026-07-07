#pragma once

#include <QtCore/QString>

/**
 * @brief 建连参数 — 从 ConnectionDialog 提取的完整连接配置
 *
 * 纯值类型，不含 QObject 继承。跨层共享（UI / 会话 / 历史持久化）。
 */
struct ConnectionParams {
    // ── 连接目标（必填）──
    QString host;
    int     port = 5921;

    // ── 连接信息标签页 ──
    QString hostname;       // 可选友好名称（用于窗口标题）
    QString username;       // 可选用户名
    QString password;       // 可选密码

    // ── 显示 & 功能标签页 ──
    bool    fullScreen     = false;
    int     windowWidth    = 1600;
    int     windowHeight   = 900;
    int     colorDepth     = 32;    // 16/24/32
    int     imageQuality   = 85;    // 1-100
    bool    viewOnly       = false;
    bool    shareClipboard = true;
    bool    showCursor     = true;

    // ── 网络标签页 ──
    int     connectionTimeout  = 30000;   // 毫秒
    bool    autoReconnect      = false;
    int     reconnectInterval  = 5000;    // 毫秒
};
