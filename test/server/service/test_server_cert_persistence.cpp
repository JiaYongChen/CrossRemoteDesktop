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

QTEST_MAIN(ServerCertPersistenceTest)
#include "test_server_cert_persistence.moc"
