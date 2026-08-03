#include <QtTest>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>

#include <thread>

#include "common/config/SettingsManager.h"

class SettingsManagerTrustedHostsTest : public QObject {
    Q_OBJECT
private slots:
    void defaultEmpty();
    void roundTrip();
    void persistsAcrossReload();
    void crossThreadSetValueStillSaves();
};

void SettingsManagerTrustedHostsTest::defaultEmpty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsManager sm(dir.filePath("config.json"));
    sm.load();
    QVERIFY(sm.trustedHosts().isEmpty());
}

void SettingsManagerTrustedHostsTest::roundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsManager sm(dir.filePath("config.json"));
    sm.load();

    QJsonArray entries;
    QJsonObject e;
    e["endpoint"] = "192.168.1.10:5921";
    e["fingerprint"] = "ab12";
    entries.append(e);

    sm.setTrustedHosts(entries);
    QCOMPARE(sm.trustedHosts(), entries);
}

void SettingsManagerTrustedHostsTest::persistsAcrossReload() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("config.json");

    QJsonArray entries;
    QJsonObject e;
    e["endpoint"] = "host:1";
    e["fingerprint"] = "ff";
    entries.append(e);

    {
        SettingsManager sm(path);
        sm.load();
        sm.setTrustedHosts(entries);
        QVERIFY(sm.save());
    }
    {
        SettingsManager sm(path);
        sm.load();
        QCOMPARE(sm.trustedHosts(), entries);
    }
}

void SettingsManagerTrustedHostsTest::crossThreadSetValueStillSaves() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("config.json");
    SettingsManager sm(path);
    sm.load();

    // 模拟服务端工作线程写入（如 TcpServer 在 TcpListener 线程持久化证书）：
    // 去抖定时器属于 SettingsManager 的创建线程，跨线程 setValue 必须能投递回属主线程启动，
    // 否则 Qt 拒绝 startTimer、保存丢失（日志出现 "Timers cannot be started from another thread"）
    std::thread writer([&sm]() {
        sm.setString("General/flag", "on");
    });
    writer.join();

    // 不显式调用 save()：去抖保存必须在属主线程触发并最终落盘
    QTRY_VERIFY_WITH_TIMEOUT([&]() {
        SettingsManager probe(path);
        probe.load();
        return probe.getString("General/flag") == QLatin1String("on");
    }(), 3000);
}

QTEST_MAIN(SettingsManagerTrustedHostsTest)
#include "test_settingsmanager_trustedhosts.moc"
