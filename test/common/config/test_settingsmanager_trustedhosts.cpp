#include <QtTest>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>

#include <thread>

#include "common/config/SettingsManager.h"

namespace {
/// 预置空 JSON，使 SettingsManager::load() 走解析路径而非迁移路径：
/// 对不存在的路径 load() 会执行一次性遗留 QSettings 迁移，迁移成功落盘后
/// clearLegacyQSettings() 将永久擦除宿主机真实的遗留设置——测试绝不可触碰生产状态
void seedEmptyConfig(const QString& path) {
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write("{}");
}
} // namespace

class SettingsManagerTrustedHostsTest : public QObject {
    Q_OBJECT
private slots:
    void defaultEmpty();
    void roundTrip();
    void persistsAcrossReload();
    void crossThreadSetValueStillSaves();
    void loadOfSeededConfigSkipsLegacyMigration();
    void loadOfAbsentCustomPathSkipsLegacyMigration();
};

void SettingsManagerTrustedHostsTest::defaultEmpty() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    seedEmptyConfig(dir.filePath("config.json"));
    SettingsManager sm(dir.filePath("config.json"));
    sm.load();
    QVERIFY(sm.trustedHosts().isEmpty());
}

void SettingsManagerTrustedHostsTest::roundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    seedEmptyConfig(dir.filePath("config.json"));
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

    seedEmptyConfig(path);
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
    seedEmptyConfig(path);
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

void SettingsManagerTrustedHostsTest::loadOfSeededConfigSkipsLegacyMigration() {
    // 守卫：预置后的 load() 不得进入迁移分支。迁移分支两条路径都会记录含 "QSettings" 的日志
    // （"No old QSettings data..." / "Migrating from QSettings..."），解析分支则完全不碰
    // QSettings——以日志探针判定分支归属，与宿主机是否存在遗留数据无关
    static QStringList markers;
    markers.clear();
    const auto previousHandler = qInstallMessageHandler(
        [](QtMsgType, const QMessageLogContext&, const QString& msg) {
            if ( msg.contains(QLatin1String("QSettings")) ) {
                markers << msg;
            }
        });

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("config.json");
    seedEmptyConfig(path);
    SettingsManager sm(path);
    sm.load();
    qInstallMessageHandler(previousHandler);

    QVERIFY2(markers.isEmpty(),
             qPrintable(QStringLiteral("load() 进入了迁移分支: %1").arg(markers.join("; "))));
}

void SettingsManagerTrustedHostsTest::loadOfAbsentCustomPathSkipsLegacyMigration() {
    // 自定义路径文件缺失时 load() 不得进入迁移分支：迁移成功落盘会武装
    // clearLegacyQSettings() 永久擦除宿主机真实遗留设置。迁移只属于默认配置路径
    // （生产首跑）——任何测试/嵌入式路径都必须是零宿主机副作用的
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("absent.json");   // 故意不预置

    static QStringList markers;
    markers.clear();
    const auto previousHandler = qInstallMessageHandler(
        [](QtMsgType, const QMessageLogContext&, const QString& msg) {
            if ( msg.contains(QLatin1String("QSettings")) ) {
                markers << msg;
            }
        });
    SettingsManager sm(path);
    sm.load();
    qInstallMessageHandler(previousHandler);

    QVERIFY2(markers.isEmpty(),
             qPrintable(QStringLiteral("自定义路径 load() 进入了迁移分支: %1").arg(markers.join("; "))));
}

QTEST_MAIN(SettingsManagerTrustedHostsTest)
#include "test_settingsmanager_trustedhosts.moc"
