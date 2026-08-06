// test/server/listener/test_tcplistener.cpp
#include <QtCore/QCoreApplication>
#include <QtNetwork/QTcpSocket>
#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

#include "common/threading/ThreadManager.h"
#include "server/listener/TcpListener.h"

/**
 * @brief TcpListener 集成测试 — 启动/停止生命周期 + 客户端连接 + 端口释放
 *
 * TcpListener 在独立 worker 线程中运行（通过 ThreadManager 托管），
 * 通过 QueuedConnection invokeMethod 跨线程触发 startListening/stopListening。
 */
class TcpListenerTest : public QObject {
    Q_OBJECT

private:
    ThreadManager* m_tm = nullptr;
    static constexpr quint16 kTestPort = 15921;   // 避开默认端口 5921

private slots:
    void initTestCase()
    {
        m_tm = new ThreadManager(this);
    }

    void cleanup()
    {
        m_tm->destroyAllThreads();
    }

    void startStop_lifecycle()
    {
        auto* listener = new TcpListener(nullptr, nullptr);
        QVERIFY(m_tm->createThread("test-listener", std::unique_ptr<Worker>(listener),
                                   false, false, 3));
        QSignalSpy startedSpy(listener, &TcpListener::started);
        QVERIFY(m_tm->startThread("test-listener"));
        QVERIFY(startedSpy.wait(3000));  // 等待 Worker::initialize() 完成

        QSignalSpy spy(listener, &TcpListener::listening);
        (void)QMetaObject::invokeMethod(listener, "startListening",
                                        Qt::QueuedConnection,
                                        Q_ARG(quint16, kTestPort));
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), static_cast<int>(kTestPort));

        (void)QMetaObject::invokeMethod(listener, "stopListening",
                                        Qt::QueuedConnection);
        QTest::qWait(200);

        QVERIFY(m_tm->stopThread("test-listener"));
    }

    void clientConnect_emitsNewConnection()
    {
        auto* listener = new TcpListener(nullptr, nullptr);
        QVERIFY(m_tm->createThread("test-listener2", std::unique_ptr<Worker>(listener),
                                   false, false, 3));
        QSignalSpy startedSpy(listener, &TcpListener::started);
        QVERIFY(m_tm->startThread("test-listener2"));
        QVERIFY(startedSpy.wait(3000));

        QSignalSpy listenSpy(listener, &TcpListener::listening);
        (void)QMetaObject::invokeMethod(listener, "startListening",
                                        Qt::QueuedConnection,
                                        Q_ARG(quint16, kTestPort));
        QVERIFY(listenSpy.wait(3000));

        QSignalSpy connSpy(listener, &TcpListener::newConnection);
        QTcpSocket sock;
        sock.connectToHost("127.0.0.1", kTestPort);
        QVERIFY(sock.waitForConnected(3000));

        QVERIFY(connSpy.wait(3000));
        QCOMPARE(connSpy.count(), 1);

        sock.disconnectFromHost();
        (void)QMetaObject::invokeMethod(listener, "stopListening",
                                        Qt::QueuedConnection);
        QTest::qWait(200);
        QVERIFY(m_tm->stopThread("test-listener2"));
    }

    void stop_releasesPort()
    {
        auto* listener1 = new TcpListener(nullptr, nullptr);
        QVERIFY(m_tm->createThread("test-listener3a", std::unique_ptr<Worker>(listener1),
                                   false, false, 3));
        QSignalSpy startedSpy1(listener1, &TcpListener::started);
        QVERIFY(m_tm->startThread("test-listener3a"));
        QVERIFY(startedSpy1.wait(3000));

        QSignalSpy spy1(listener1, &TcpListener::listening);
        (void)QMetaObject::invokeMethod(listener1, "startListening",
                                        Qt::QueuedConnection,
                                        Q_ARG(quint16, kTestPort));
        QVERIFY(spy1.wait(3000));

        (void)QMetaObject::invokeMethod(listener1, "stopListening",
                                        Qt::QueuedConnection);
        QTest::qWait(200);
        QVERIFY(m_tm->stopThread("test-listener3a"));
        QVERIFY(m_tm->destroyThread("test-listener3a"));

        auto* listener2 = new TcpListener(nullptr, nullptr);
        QVERIFY(m_tm->createThread("test-listener3b", std::unique_ptr<Worker>(listener2),
                                   false, false, 3));
        QSignalSpy startedSpy2(listener2, &TcpListener::started);
        QVERIFY(m_tm->startThread("test-listener3b"));
        QVERIFY(startedSpy2.wait(3000));

        QSignalSpy spy2(listener2, &TcpListener::listening);
        (void)QMetaObject::invokeMethod(listener2, "startListening",
                                        Qt::QueuedConnection,
                                        Q_ARG(quint16, kTestPort));
        QVERIFY(spy2.wait(3000));
        QCOMPARE(spy2.count(), 1);

        (void)QMetaObject::invokeMethod(listener2, "stopListening",
                                        Qt::QueuedConnection);
        QTest::qWait(200);
        QVERIFY(m_tm->stopThread("test-listener3b"));
    }
};

QTEST_MAIN(TcpListenerTest)
#include "test_tcplistener.moc"
