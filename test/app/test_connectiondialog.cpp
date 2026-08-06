// 测试 ConnectionDialog：地址解析（parseHostPort）+ 参数往返（set/getConnectionParams）+ 重置默认值
//
// parseHostPort / validateConnectionInfo 已提升为 public（可测试性重构）——
// MSVC 修饰名编码访问级别，跨编译单元的 private hack（#define private public）会
// 产生符号不匹配导致 LNK2019，故测试直接经公开接口验证。

#include <QtCore/QString>
#include <QtTest/QtTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>

#include "common/config/NetworkConstants.h"
#include "common/data/ConnectionParams.h"
#include "app/ConnectionDialog.h"

class ConnectionDialogTest : public QObject {
    Q_OBJECT

private:
    QApplication* m_app = nullptr;
    ConnectionDialog* m_dlg = nullptr;

private slots:
    void initTestCase()
    {
        // QTEST_MAIN 已创建 QApplication（QTEST_MAIN 而非 QTEST_GUILESS_MAIN）
        if (!QApplication::instance()) {
            int argc = 0;
            char** argv = nullptr;
            m_app = new QApplication(argc, argv);
        }
    }

    void init()
    {
        m_dlg = new ConnectionDialog();
        m_dlg->setDefaultPort(NetworkConstants::DefaultServerPort);
    }

    void cleanup()
    {
        delete m_dlg;
        m_dlg = nullptr;
    }

    void parseHostPort_ipOnly_usesDefaultPort()
    {
        QString host;
        int port = 0;
        ConnectionDialog::parseHostPort("192.168.1.100", 5921, host, port);
        QCOMPARE(host, QString("192.168.1.100"));
        QCOMPARE(port, 5921);
    }

    void parseHostPort_ipWithPort_extractsBoth()
    {
        QString host;
        int port = 0;
        ConnectionDialog::parseHostPort("192.168.1.100:1234", 5921, host, port);
        QCOMPARE(host, QString("192.168.1.100"));
        QCOMPARE(port, 1234);
    }

    void parseHostPort_hostnameWithPort()
    {
        QString host;
        int port = 0;
        ConnectionDialog::parseHostPort("office-pc:8080", 5921, host, port);
        QCOMPARE(host, QString("office-pc"));
        QCOMPARE(port, 8080);
    }

    void parseHostPort_ipv6Bracket()
    {
        QString host;
        int port = 0;
        ConnectionDialog::parseHostPort("[::1]:5921", 5921, host, port);
        QCOMPARE(host, QString("::1"));
        QCOMPARE(port, 5921);
    }

    void validateConnectionInfo_missingHost_fails()
    {
        QString errorMsg;
        const bool ok = m_dlg->validateConnectionInfo(errorMsg);
        QVERIFY(!ok);
        QVERIFY(!errorMsg.isEmpty());
    }

    void getConnectionParams_assemblesCorrectly()
    {
        ConnectionParams in;
        in.host = "10.0.0.1";
        in.port = 1234;
        in.hostname = "MyPC";
        in.username = "admin";
        in.password = "secret";
        in.colorDepth = 16;
        in.fullScreen = true;
        in.imageQuality = 50;
        in.viewOnly = true;
        in.shareClipboard = false;
        in.showCursor = false;
        in.connectionTimeout = 15000;
        in.autoReconnect = true;
        in.reconnectInterval = 10000;

        m_dlg->setConnectionParams(in);
        const ConnectionParams out = m_dlg->getConnectionParams();

        QCOMPARE(out.host, in.host);
        QCOMPARE(out.port, in.port);
        QCOMPARE(out.hostname, in.hostname);
        QCOMPARE(out.username, in.username);
        QCOMPARE(out.password, in.password);
        QCOMPARE(out.colorDepth, in.colorDepth);
        QCOMPARE(out.fullScreen, in.fullScreen);
        QCOMPARE(out.imageQuality, in.imageQuality);
        QCOMPARE(out.viewOnly, in.viewOnly);
        QCOMPARE(out.shareClipboard, in.shareClipboard);
        QCOMPARE(out.showCursor, in.showCursor);
        QCOMPARE(out.connectionTimeout, in.connectionTimeout);
        QCOMPARE(out.autoReconnect, in.autoReconnect);
        QCOMPARE(out.reconnectInterval, in.reconnectInterval);
    }

    void resetToDefaults_clearsAll()
    {
        ConnectionParams custom;
        custom.host = "evil.com";
        custom.password = "pwned";
        m_dlg->setConnectionParams(custom);

        m_dlg->resetToDefaults();

        const ConnectionParams reset = m_dlg->getConnectionParams();
        QCOMPARE(reset.host, QString());
        QCOMPARE(reset.password, QString());
        QCOMPARE(reset.colorDepth, 32);
        QCOMPARE(reset.imageQuality, 85);
        QCOMPARE(reset.fullScreen, false);
    }
};

QTEST_MAIN(ConnectionDialogTest)
#include "test_connectiondialog.moc"
