#ifndef ITCPTRANSPORT_H
#define ITCPTRANSPORT_H

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>

// 轻量接口：抽象 TCP 传输（阶段A只定义接口，不影响现有实现）。
class ITcpTransport : public QObject {
    Q_OBJECT
public:
    explicit ITcpTransport(QObject* parent = nullptr) : QObject(parent) {}
    ~ITcpTransport() override = default;

    virtual void connectToHost(const QString& host, quint16 port) = 0;
    virtual void disconnectFromHost() = 0;
    virtual void abort() = 0;

    virtual qint64 write(const QByteArray& data) = 0;
    virtual bool isConnected() const = 0;

signals:
    void connected();
    void disconnected();
    void readyRead(const QByteArray& data);
    void errorOccurred(const QString& error);
};

#endif // ITCPTRANSPORT_H
