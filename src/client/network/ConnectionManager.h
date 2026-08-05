#pragma once

#include <memory>

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtNetwork/QSslCertificate>

#include "common/error/RdError.h"
#include "common/network/Protocol.h"

class IMessageCodec;
class ServerTrustStore;
class SettingsManager;
class TcpClient;

/**
 * @brief ConnectionManager 负责管理连接、版本交换和认证
 *
 * 职责：
 * - 管理 TCP/TLS 连接和自动重连
 * - 处理版本交换和认证逻辑
 * - 提供消息发送接口
 * - 转发消息给上层处理
 *
 * 不负责：
 * - 业务数据处理（屏幕数据、输入事件等）
 * - 这些由 ProtocolSession 处理
 *
 * 状态模型：不暴露状态枚举，以语义化事件信号（管道事件/会话事件/故障事件）
 * 驱动上层。内部仅保留最小标志（m_authenticated、m_reconnectArmed）。
 */
class ConnectionManager : public QObject {
    Q_OBJECT

public:
    explicit ConnectionManager(QObject* parent = nullptr, SettingsManager* settings = nullptr);
    ~ConnectionManager();

    // 连接控制
    void connectToHost(const QString& host, int port);
    void disconnectFromHost();

    // 状态查询
    virtual bool isConnected() const;
    virtual bool isAuthenticated() const;

    /// 预设认证凭证（在 connectToHost 前调用），不触发认证流程
    void setCredentials(const QString& username, const QString& password);

    /// 更新凭据（用于认证失败后重试，跨线程安全）
    Q_INVOKABLE void updateCredentials(const QString& username, const QString& password);

    /// 设置客户端颜色深度（在 connectToHost 前调用，由握手携带）
    void setColorDepth(int depth);
    /// 设置 JPEG 图像质量（在 connectToHost 前调用，由握手携带）
    void setImageQuality(int quality);

    // 消息发送接口
    virtual void sendMessage(MessageType type, const IMessageCodec& message);

    // 自动重连管理
    void setAutoReconnect(bool enable);
    void setReconnectInterval(int msecs);

    // 连接超时管理
    void setConnectionTimeout(int msecs);

    /// 当前凭据（认证成功后即实际通过的凭据）
    QString username() const { return m_username; }
    QString password() const { return m_password; }

signals:
    // 管道事件（TCP/TLS 生命周期）
    void connecting();      ///< 连接已发起（含自动重连）
    void connected();       ///< PKI 验证通过，管道就绪
    void disconnected();    ///< 管道断开（用户主动或意外）
    void reconnecting();    ///< 自动重连定时器已武装

    // 会话事件（版本交换/认证的预期结果，非系统故障）
    void authenticated();
    void authenticationFailed(AuthResult result, const QString& message);
    void versionMismatched(const QString& serverVer, const QString& localVer);

    /// 连接层故障（携带可读原因，如网络错误、TLS 失败、解码失败）
    void errorOccurred(const RdError& error);

    // 通用消息转发信号 - 供上层业务处理
    void messageReceived(MessageType type, const QByteArray& payload);

private slots:
    void onTcpConnected();
    void onTcpDisconnected();
    void onTcpError(const RdError& error);
    void onConnectionTimeout();
    void onReconnectTimer();
    void onTcpMessageReceived(MessageType type, const QByteArray& payload);

private:
    void setupTcpClient();
    void cleanupConnection();
    /// @return true 表示重连定时器已武装（含已活跃），false 表示不重连
    [[nodiscard]] bool startAutoReconnect();
    void stopAutoReconnect();

    // 连接相关处理方法（版本交换和认证）
    void handleVersionExchangeResponse(const QByteArray& data);
    void handleAuthenticationResponse(const QByteArray& data);

    /// 管道就绪判定：TCP+TLS 已建立即可处理服务端消息
    [[nodiscard]] bool mayProcessServerMessages() const;

    void sendVersionExchange();
    void sendEncodePrefs();
    /// 以当前凭据 + 本次下发的认证参数派生 PBKDF2 并发送认证请求（参数不缓存）
    void sendAuthenticationRequest(const QByteArray& salt, quint32 iterations, quint32 keyLength);
    QString getClientOS();

    /// 内部：保持 host/port 并发起 TCP 连接（供 onReconnectTimer 使用，
    /// 绕过 public connectToHost 的用户意图复位）
    void startConnection(const QString& host, int port);

private:
    TcpClient* m_tcpClient;
    QString m_currentHost;
    int m_currentPort;
    QTimer* m_connectionTimer;

    // 认证信息
    QString m_username;
    QString m_password;

    // 显示参数（由握手携带到服务端）
    int m_colorDepth = 32;
    int m_imageQuality = 85;

    // 自动重连相关
    QTimer* m_reconnectTimer;
    bool m_autoReconnect;
    int m_reconnectInterval;
    int m_maxReconnectAttempts;
    int m_currentReconnectAttempts;

    // 连接超时
    int m_connectionTimeout;

    /// 一次性用户意图标记：disconnectFromHost 无条件置 true，
    /// onTcpDisconnected 消费后清零。connectToHost 在 abort 后二次清零。
    /// 不持久化——不会被 onTcpError/onConnectionTimeout 读取或修改。
    bool m_userInitiatedDisconnect = false;

    /// 会话认证状态：认证成功后置 true，断连自动复位。仅服务于 isAuthenticated() 查询
    bool m_authenticated = false;

    /// 自动重连定时器已武装（startAutoReconnect 成功置位，连接建立/重连放弃复位）
    bool m_reconnectArmed = false;

    std::unique_ptr<ServerTrustStore> m_trustStore;   ///< TOFU 信任库（未注入 settings 时为空 → 旧行为）

    // 挂起的信任决策上下文（首次连接 VerifyNone 通过后暂存服务端证书，
    // 于 onTcpConnected 自动记入信任库；填充由证书获取通道提供）
    QSslCertificate m_pendingTrustCert;
    QString m_pendingTrustEndpoint;

};
