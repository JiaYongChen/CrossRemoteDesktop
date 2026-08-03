#include <QtTest>
#include <QtCore/QCryptographicHash>
#include <QtCore/QTemporaryDir>
#include <QtNetwork/QSslCertificate>

#include "common/config/SettingsManager.h"
#include "server/service/TcpServer.h"

class ServerCertPersistenceTest : public QObject {
    Q_OBJECT
private slots:
    void certificateStableAcrossRestart();
    void certificatePersistedWithoutExplicitSave();
};

void ServerCertPersistenceTest::certificateStableAcrossRestart() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cfg = dir.filePath("config.json");

    QByteArray fp1;
    {
        SettingsManager sm(cfg);
        sm.load();
        TcpServer server(nullptr, &sm);
        QVERIFY(server.startServer(0));
        fp1 = server.sslCertificate().digest(QCryptographicHash::Sha256);
        server.stopServer(true);
        QVERIFY(sm.save());
    }

    QByteArray fp2;
    {
        SettingsManager sm(cfg);
        sm.load();
        TcpServer server(nullptr, &sm);
        QVERIFY(server.startServer(0));
        fp2 = server.sslCertificate().digest(QCryptographicHash::Sha256);
        server.stopServer(true);
    }

    QVERIFY(!fp1.isEmpty());
    QCOMPARE(fp1, fp2);   // 重启后复用同一证书——TOFU 基石断言
}

void ServerCertPersistenceTest::certificatePersistedWithoutExplicitSave() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cfg = dir.filePath("config.json");

    SettingsManager sm(cfg);
    sm.load();
    {
        TcpServer server(nullptr, &sm);
        QVERIFY(server.startServer(0));
        server.stopServer(true);
    }
    // 不调用 save()，也不自旋事件循环等待去抖定时器：
    // 证书是关键安全状态，必须在生成时同步写穿——否则应用在任一次主线程保存前被强杀
    // 即丢失证书 → 下次启动重新生成 → 所有客户端收到虚假"身份变更"警告

    SettingsManager probe(cfg);
    probe.load();
    QVERIFY(!probe.getString("Server/tlsCertPem").isEmpty());
    QVERIFY(!probe.getString("Server/tlsKeyPem").isEmpty());
}

QTEST_MAIN(ServerCertPersistenceTest)
#include "test_server_cert_persistence.moc"
