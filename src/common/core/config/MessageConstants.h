#pragma once

#include <QtCore/QString>

namespace MessageConstants {
    // 网络相关消息
    namespace Network {
        const QString ALREADY_CONNECTED = "Already connected or connecting";
        const QString NOT_CONNECTED = "Not connected to server";
        const QString HANDSHAKE_RESPONSE_RECEIVED = "Received handshake response from server";
        const QString AUTH_RESPONSE_RECEIVED = "Received authentication response from server";
        const QString AUTH_SUCCESSFUL = "Authentication successful, session ID: %1";
        const QString HANDSHAKE_REQUEST_SENT = "Sent handshake request to server";
        const QString AUTH_REQUEST_SENT = "Sent authentication request to server for user: %1";
    }

    // UI相关消息
    namespace UI {
        const QString STATUS_CONNECTING = "正在连接...";
        const QString STATUS_CONNECTED = "已连接";
        const QString STATUS_AUTHENTICATING = "正在认证...";
        const QString STATUS_AUTHENTICATED = "已认证";
        const QString STATUS_RECONNECTING = "正在重连...";
        const QString STATUS_DISCONNECTING = "正在断开连接...";
        const QString STATUS_DISCONNECTED = "未连接";
        const QString STATUS_ERROR = "连接错误";
        const QString SERVER_STATUS_TITLE = "服务器状态";
        const QString SERVER_ALREADY_RUNNING = "服务器已经在运行中。";
        const QString ERROR_TITLE = "错误";
        const QString SERVER_MANAGER_NOT_INITIALIZED = "服务器管理器未初始化。";
        const QString SERVER_NOT_RUNNING = "服务器未运行。";
        const QString INPUT_ERROR_TITLE = "输入错误";
        const QString INVALID_HOST_ADDRESS = "请输入有效的主机地址";
        const QString INVALID_PORT_RANGE = "端口号必须在1-65535之间";
        const QString VALIDATION_ERROR_TITLE = "验证错误";
    }
}

