#include <QtTest>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>

#include "common/config/SettingsManager.h"

class SettingsManagerTrustedHostsTest : public QObject {
    Q_OBJECT
private slots:
    void defaultEmpty();
    void roundTrip();
    void persistsAcrossReload();
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

QTEST_MAIN(SettingsManagerTrustedHostsTest)
#include "test_settingsmanager_trustedhosts.moc"
