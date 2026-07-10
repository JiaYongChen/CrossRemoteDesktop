#pragma once

#include "../src/client/network/ConnectionManager.h"
#include "../src/common/network/Protocol.h"

/// Mock ConnectionManager — 重写 5 个 virtual 方法用于测试控制
class MockConnectionManager : public ConnectionManager {
public:
    using ConnectionManager::ConnectionManager;

    bool m_mockConnected = false;
    bool m_mockAuthenticated = false;

    bool isConnected() const override { return m_mockConnected; }
    bool isAuthenticated() const override { return m_mockAuthenticated; }
    QString currentHost() const override { return "test-host"; }
    int currentPort() const override { return 9999; }

    void sendMessage(MessageType type, const IMessageCodec&) override {
        lastSentType = type;
        messageSent = true;
    }

    MessageType lastSentType = MessageType::HANDSHAKE_REQUEST;
    bool messageSent = false;
};
