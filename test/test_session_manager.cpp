#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include "../src/client/managers/sessionmanager.h"
#include "../src/client/managers/connectionmanager.h"
#include "../src/common/core/logger.h"

class FakeConnectionManager : public ConnectionManager {
    Q_OBJECT
public:
    explicit FakeConnectionManager(QObject* parent=nullptr) : ConnectionManager(parent) {}
    void forceState(ConnectionState s) { setProperty("__force_state", (int)s); QMetaObject::invokeMethod(this, "setConnectionState", Qt::DirectConnection, Q_ARG(ConnectionManager::ConnectionState, s)); }
};

class TestSessionManager : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        Logger::instance()->setLogTargets(Logger::Console);
        Logger::instance()->setLogLevel(Logger::Debug);
    }

    void start_requires_authenticated_connection() {
        ConnectionManager cm; // real manager, will be Disconnected
        SessionManager sm(&cm);
        QSignalSpy errSpy(&sm, &SessionManager::sessionError);
        sm.startSession();
        QVERIFY(errSpy.count() == 1);
        QCOMPARE(sm.sessionState(), SessionManager::Inactive);
    }

    void start_active_and_receive_frames_updates_pixmap() {
        // Use real ConnectionManager but simulate authenticated via slots
        ConnectionManager cm;
        // Emulate connection/auth flow
        QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpAuthenticated", Qt::DirectConnection));
        QVERIFY(cm.isAuthenticated());

        SessionManager sm(&cm);
        QSignalSpy stateSpy(&sm, &SessionManager::sessionStateChanged);
        QSignalSpy screenSpy(&sm, &SessionManager::screenUpdated);

        sm.startSession();
        QVERIFY(sm.isActive());
        QVERIFY(stateSpy.count() >= 2); // Initializing -> Active

        // Send a fake image via the slot
        QImage img(64, 32, QImage::Format_ARGB32);
        img.fill(Qt::blue);
        QVERIFY(QMetaObject::invokeMethod(&sm, "onScreenDataReceived", Qt::DirectConnection, Q_ARG(QImage, img)));

        // Should update internal pixmap and emit signal
        QVERIFY(screenSpy.count() == 1);
        QPixmap px = sm.currentScreen();
        QVERIFY(!px.isNull());
        QCOMPARE(px.size(), img.size());
        QVERIFY(sm.remoteScreenSize() == img.size());

        // Terminate and ensure state resets to Inactive
        sm.terminateSession();
        QCOMPARE(sm.sessionState(), SessionManager::Inactive);
    }

    void fps_calculation_monotonic() {
        ConnectionManager cm;
        QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpConnected", Qt::DirectConnection));
        QVERIFY(QMetaObject::invokeMethod(&cm, "onTcpAuthenticated", Qt::DirectConnection));
        SessionManager sm(&cm);
        sm.startSession();

        // Feed several frames to build history
        for (int i=0; i<5; ++i) {
            QImage img(16, 16, QImage::Format_ARGB32);
            img.fill(Qt::white);
            QVERIFY(QMetaObject::invokeMethod(&sm, "onScreenDataReceived", Qt::DirectConnection, Q_ARG(QImage, img)));
        }
        auto stats1 = sm.performanceStats();
        QTest::qSleep(30);
        for (int i=0; i<5; ++i) {
            QImage img(16, 16, QImage::Format_ARGB32);
            img.fill(Qt::white);
            QVERIFY(QMetaObject::invokeMethod(&sm, "onScreenDataReceived", Qt::DirectConnection, Q_ARG(QImage, img)));
        }
        auto stats2 = sm.performanceStats();
        QVERIFY(stats2.frameCount > stats1.frameCount);
        QVERIFY(stats2.currentFPS >= 0.0);
    }
};

QTEST_MAIN(TestSessionManager)
#include "test_session_manager.moc"
