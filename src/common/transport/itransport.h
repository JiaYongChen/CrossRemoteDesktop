#ifndef ITRANSPORT_H
#define ITRANSPORT_H

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <functional>

// 轻量接口：抽象底层传输（如 QTcpSocket 封装）。
// 阶段A只引入接口，不改现有 TcpClient/ClientHandler 行为。
class ITransport : public QObject {
    Q_OBJECT
public:
    explicit ITransport(QObject* parent = nullptr) : QObject(parent) {}
    ~ITransport() override = default;

    // 连接/断开
    virtual void connectToHost(const QString& host, quint16 port) = 0;
    virtual void disconnectFromHost() = 0;
    virtual void abort() = 0;

    // 发送原始字节
    virtual qint64 write(const QByteArray& data) = 0;

    // 状态
    virtual bool isConnected() const = 0;

signals:
    void connected();
    void disconnected();
    void readyRead(const QByteArray& data);
    void errorOccurred(const QString& error);
};

#endif // ITRANSPORT_H
