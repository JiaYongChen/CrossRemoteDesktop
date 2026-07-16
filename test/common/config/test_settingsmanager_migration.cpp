// SettingsManager 旧 QSettings → JSON 迁移单元测试
//
// 通过 INI 格式 QSettings 注入旧格式数据（并行 StringList，全部为字符串），
// 验证 readLegacyConnectionHistory() 的类型转换、条目校验和边界保护。
// 旧格式事实（源自 git 历史 8ebf055~1 的写入代码）：
//   - port/宽高等 int 存为 QString::number() 数字字符串
//   - bool 存为 "1" / "0"
//   - 时间为 Qt::ISODate 字符串
//   - 密码为 "ENC:" 前缀密文（PasswordCrypto，key=username）

#include <QtTest/QtTest>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>

#include "common/config/SettingsManager.h"
#include "common/crypto/PasswordCrypto.h"

class SettingsManagerMigrationTest : public QObject
{
    Q_OBJECT

private slots:
    void init();

    // 核心回归：类型转换必须保留真实值（非默认值）
    void testPortSurvivesMigration();
    void testIntFieldsSurviveMigration();
    void testBoolFieldsSurviveMigration();

    // 校验语义：非法条目剔除
    void testInvalidPortEntrySkipped();
    void testPortAboveRangeSkipped();
    void testInvalidTimeEntrySkipped();

    // 边界保护：并行列表长度不一致不崩溃
    void testShortPortsListDoesNotCrash();
    void testMissingOptionalListsUseDefaults();

    // 字符串字段与密码
    void testPasswordDecryptedAndReencrypted();
    void testHostnameFallbackToHost();

private:
    /// 向 INI QSettings 写入一条完整的旧格式记录
    void writeLegacyEntry(QSettings &s,
                          const QString &host, const QString &port,
                          const QString &time = QStringLiteral("2025-06-01T10:00:00"),
                          const QString &hostname = QString());

    QTemporaryDir m_dir;
    QString m_iniPath;
};

void SettingsManagerMigrationTest::init()
{
    QVERIFY(m_dir.isValid());
    // 每个测试函数用独立 INI 文件，避免 QSettings 同路径缓存干扰
    m_iniPath = m_dir.filePath(QStringLiteral("legacy_%1.ini")
                                   .arg(QLatin1String(QTest::currentTestFunction())));
}

void SettingsManagerMigrationTest::writeLegacyEntry(QSettings &s,
                                                    const QString &host, const QString &port,
                                                    const QString &time,
                                                    const QString &hostname)
{
    s.beginGroup("ConnectionHistory");
    auto appendTo = [&s](const char *key, const QString &v) {
        QStringList list = s.value(QLatin1String(key)).toStringList();
        list.append(v);
        s.setValue(QLatin1String(key), list);
    };
    appendTo("hosts", host);
    appendTo("hostnames", hostname.isEmpty() ? host : hostname);
    appendTo("ports", port);
    appendTo("times", time);
    appendTo("usernames", QStringLiteral("admin"));
    appendTo("passwords", QString());
    appendTo("fullScreens", QStringLiteral("1"));
    appendTo("windowWidths", QStringLiteral("2560"));
    appendTo("windowHeights", QStringLiteral("1440"));
    appendTo("colorDepths", QStringLiteral("24"));
    appendTo("imageQualities", QStringLiteral("70"));
    appendTo("viewOnlys", QStringLiteral("1"));
    appendTo("shareClipboards", QStringLiteral("0"));
    appendTo("showCursors", QStringLiteral("0"));
    appendTo("connectionTimeouts", QStringLiteral("15000"));
    appendTo("autoReconnects", QStringLiteral("1"));
    appendTo("reconnectIntervals", QStringLiteral("8000"));
    s.endGroup();
    s.sync();
}

// ============================================================
// 核心回归：非默认值必须存活
// ============================================================

void SettingsManagerMigrationTest::testPortSurvivesMigration()
{
    QSettings legacy(m_iniPath, QSettings::IniFormat);
    writeLegacyEntry(legacy, QStringLiteral("192.168.1.10"), QStringLiteral("5900"));

    const QJsonArray result = SettingsManager::readLegacyConnectionHistory(legacy);

    QCOMPARE(result.size(), 1);
    const QJsonObject entry = result.at(0).toObject();
    QCOMPARE(entry.value("host").toString(), QStringLiteral("192.168.1.10"));
    // 关键断言：真实端口 5900 必须存活，不能被默认值 5921 覆盖
    QCOMPARE(entry.value("port").toInt(), 5900);
}

void SettingsManagerMigrationTest::testIntFieldsSurviveMigration()
{
    QSettings legacy(m_iniPath, QSettings::IniFormat);
    writeLegacyEntry(legacy, QStringLiteral("10.0.0.1"), QStringLiteral("5901"));

    const QJsonArray result = SettingsManager::readLegacyConnectionHistory(legacy);

    QCOMPARE(result.size(), 1);
    const QJsonObject entry = result.at(0).toObject();
    QCOMPARE(entry.value("windowWidth").toInt(),      2560);
    QCOMPARE(entry.value("windowHeight").toInt(),     1440);
    QCOMPARE(entry.value("colorDepth").toInt(),       24);
    QCOMPARE(entry.value("imageQuality").toInt(),     70);
    QCOMPARE(entry.value("connectionTimeout").toInt(), 15000);
    QCOMPARE(entry.value("reconnectInterval").toInt(), 8000);
}

void SettingsManagerMigrationTest::testBoolFieldsSurviveMigration()
{
    QSettings legacy(m_iniPath, QSettings::IniFormat);
    writeLegacyEntry(legacy, QStringLiteral("10.0.0.2"), QStringLiteral("5902"));

    const QJsonArray result = SettingsManager::readLegacyConnectionHistory(legacy);

    QCOMPARE(result.size(), 1);
    const QJsonObject entry = result.at(0).toObject();
    // 旧数据："1" → true，"0" → false；load() 默认值为 false/false/true/true/false，
    // 测试数据故意全部取反以暴露"回落默认值"的回归
    QCOMPARE(entry.value("fullScreen").toBool(),     true);
    QCOMPARE(entry.value("viewOnly").toBool(),       true);
    QCOMPARE(entry.value("shareClipboard").toBool(), false);
    QCOMPARE(entry.value("showCursor").toBool(),     false);
    QCOMPARE(entry.value("autoReconnect").toBool(),  true);
}

// ============================================================
// 校验语义
// ============================================================

void SettingsManagerMigrationTest::testInvalidPortEntrySkipped()
{
    QSettings legacy(m_iniPath, QSettings::IniFormat);
    writeLegacyEntry(legacy, QStringLiteral("bad.example"), QStringLiteral("not_a_number"));
    writeLegacyEntry(legacy, QStringLiteral("good.example"), QStringLiteral("5921"));

    const QJsonArray result = SettingsManager::readLegacyConnectionHistory(legacy);

    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).toObject().value("host").toString(),
             QStringLiteral("good.example"));
}

void SettingsManagerMigrationTest::testPortAboveRangeSkipped()
{
    QSettings legacy(m_iniPath, QSettings::IniFormat);
    writeLegacyEntry(legacy, QStringLiteral("overflow.example"), QStringLiteral("99999"));

    const QJsonArray result = SettingsManager::readLegacyConnectionHistory(legacy);

    QCOMPARE(result.size(), 0);
}

void SettingsManagerMigrationTest::testInvalidTimeEntrySkipped()
{
    QSettings legacy(m_iniPath, QSettings::IniFormat);
    writeLegacyEntry(legacy, QStringLiteral("badtime.example"), QStringLiteral("5921"),
                     QStringLiteral("not-a-date"));

    const QJsonArray result = SettingsManager::readLegacyConnectionHistory(legacy);

    QCOMPARE(result.size(), 0);
}

// ============================================================
// 边界保护
// ============================================================

void SettingsManagerMigrationTest::testShortPortsListDoesNotCrash()
{
    QSettings legacy(m_iniPath, QSettings::IniFormat);
    legacy.beginGroup("ConnectionHistory");
    // hosts 有 2 条，ports 只有 1 条（模拟部分写入损坏）
    legacy.setValue("hosts", QStringList{QStringLiteral("a.example"), QStringLiteral("b.example")});
    legacy.setValue("ports", QStringList{QStringLiteral("5921")});
    legacy.setValue("times", QStringList{QStringLiteral("2025-06-01T10:00:00"),
                                         QStringLiteral("2025-06-01T11:00:00")});
    legacy.endGroup();
    legacy.sync();

    // 不崩溃，且缺 port 的第二条被安全剔除
    const QJsonArray result = SettingsManager::readLegacyConnectionHistory(legacy);
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).toObject().value("host").toString(), QStringLiteral("a.example"));
}

void SettingsManagerMigrationTest::testMissingOptionalListsUseDefaults()
{
    QSettings legacy(m_iniPath, QSettings::IniFormat);
    legacy.beginGroup("ConnectionHistory");
    // 仅必需字段，全部可选列表缺失
    legacy.setValue("hosts", QStringList{QStringLiteral("minimal.example")});
    legacy.setValue("ports", QStringList{QStringLiteral("5930")});
    legacy.setValue("times", QStringList{QStringLiteral("2025-06-01T10:00:00")});
    legacy.endGroup();
    legacy.sync();

    const QJsonArray result = SettingsManager::readLegacyConnectionHistory(legacy);

    QCOMPARE(result.size(), 1);
    const QJsonObject entry = result.at(0).toObject();
    QCOMPARE(entry.value("port").toInt(), 5930);
    // 缺失的可选字段回落到 ConnectionHistory::load() 的默认值
    QCOMPARE(entry.value("windowWidth").toInt(),    1600);
    QCOMPARE(entry.value("windowHeight").toInt(),   900);
    QCOMPARE(entry.value("shareClipboard").toBool(), true);
    QCOMPARE(entry.value("showCursor").toBool(),     true);
    QCOMPARE(entry.value("fullScreen").toBool(),     false);
}

// ============================================================
// 字符串字段与密码
// ============================================================

void SettingsManagerMigrationTest::testPasswordDecryptedAndReencrypted()
{
    const QString username = QStringLiteral("admin");
    const QString plainPassword = QStringLiteral("s3cret!");
    const QString encStored = QLatin1String("ENC:")
                              + PasswordCrypto::encrypt(username, plainPassword);

    QSettings legacy(m_iniPath, QSettings::IniFormat);
    legacy.beginGroup("ConnectionHistory");
    legacy.setValue("hosts", QStringList{QStringLiteral("secure.example")});
    legacy.setValue("ports", QStringList{QStringLiteral("5921")});
    legacy.setValue("times", QStringList{QStringLiteral("2025-06-01T10:00:00")});
    legacy.setValue("usernames", QStringList{username});
    legacy.setValue("passwords", QStringList{encStored});
    legacy.endGroup();
    legacy.sync();

    const QJsonArray result = SettingsManager::readLegacyConnectionHistory(legacy);

    QCOMPARE(result.size(), 1);
    const QString migrated = result.at(0).toObject().value("password").toString();
    // 输出必须仍为 ENC: 格式且能解密回原文（IV 随机，密文本身会变化）
    QVERIFY(migrated.startsWith(QLatin1String("ENC:")));
    QCOMPARE(PasswordCrypto::decrypt(username, migrated.mid(4)), plainPassword);
}

void SettingsManagerMigrationTest::testHostnameFallbackToHost()
{
    QSettings legacy(m_iniPath, QSettings::IniFormat);
    legacy.beginGroup("ConnectionHistory");
    legacy.setValue("hosts", QStringList{QStringLiteral("192.168.1.20")});
    legacy.setValue("ports", QStringList{QStringLiteral("5921")});
    legacy.setValue("times", QStringList{QStringLiteral("2025-06-01T10:00:00")});
    legacy.setValue("hostnames", QStringList{QString()});   // 空 hostname
    legacy.endGroup();
    legacy.sync();

    const QJsonArray result = SettingsManager::readLegacyConnectionHistory(legacy);

    QCOMPARE(result.size(), 1);
    QCOMPARE(result.at(0).toObject().value("hostname").toString(),
             QStringLiteral("192.168.1.20"));
}

QTEST_GUILESS_MAIN(SettingsManagerMigrationTest)
#include "test_settingsmanager_migration.moc"
