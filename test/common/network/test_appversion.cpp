#include <QtTest/QTest>

#include "common/config/ProtocolConstants.h"
#include "common/network/AppVersion.h"

class TestAppVersion : public QObject {
    Q_OBJECT
private slots:
    // ── parse：合法输入 ──
    void parse_validThreeSegments() {
        const std::optional<AppVersion> v = AppVersion::parse(QStringLiteral("1.2.3"));
        QVERIFY(v.has_value());
        QCOMPARE(v->major, 1);
        QCOMPARE(v->minor, 2);
        QCOMPARE(v->patch, 3);
    }

    void parse_allZeros() {
        const std::optional<AppVersion> v = AppVersion::parse(QStringLiteral("0.0.0"));
        QVERIFY(v.has_value());
        QCOMPARE(v->major, 0);
        QCOMPARE(v->minor, 0);
        QCOMPARE(v->patch, 0);
    }

    void parse_maxSegmentValue999() {
        const std::optional<AppVersion> v = AppVersion::parse(QStringLiteral("999.999.999"));
        QVERIFY(v.has_value());
        QCOMPARE(v->major, 999);
        QCOMPARE(v->minor, 999);
        QCOMPARE(v->patch, 999);
    }

    // ── parse：非法输入一律 nullopt（fail-closed）──
    void parse_invalidInputs_data() {
        QTest::addColumn<QString>("text");
        QTest::newRow("空串")       << QString();
        QTest::newRow("单段")       << QStringLiteral("1");
        QTest::newRow("两段")       << QStringLiteral("1.0");
        QTest::newRow("四段")       << QStringLiteral("1.0.0.0");
        QTest::newRow("空段")       << QStringLiteral("1..0");
        QTest::newRow("非数字")     << QStringLiteral("1.0.x");
        QTest::newRow("正号")       << QStringLiteral("+1.0.0");
        QTest::newRow("负号")       << QStringLiteral("-1.0.0");
        QTest::newRow("前导空白")   << QStringLiteral(" 1.0.0");
        QTest::newRow("尾部空白")   << QStringLiteral("1.0.0 ");
        QTest::newRow("四位数段")   << QStringLiteral("1000.0.0");
        QTest::newRow("尾随连字符") << QStringLiteral("1.0.0-x");
    }

    void parse_invalidInputs() {
        QFETCH(QString, text);
        QVERIFY(!AppVersion::parse(text).has_value());
    }

    // ── 相等比较（完全相等规则）──
    void equality_identicalVersionsAreEqual() {
        QCOMPARE(*AppVersion::parse(QStringLiteral("1.2.3")),
                 *AppVersion::parse(QStringLiteral("1.2.3")));
    }

    void equality_anySegmentDiffersNotEqual() {
        const AppVersion base = *AppVersion::parse(QStringLiteral("1.2.3"));
        QVERIFY(base != *AppVersion::parse(QStringLiteral("1.2.4")));   // patch 不同
        QVERIFY(base != *AppVersion::parse(QStringLiteral("1.3.3")));   // minor 不同
        QVERIFY(base != *AppVersion::parse(QStringLiteral("2.2.3")));   // major 不同
    }

    // ── appVersionMatches：与本机 ProtocolConstants::AppVersion 完全相等 ──
    void matches_localVersionStringIsTrue() {
        QVERIFY(appVersionMatches(QString::fromLatin1(ProtocolConstants::AppVersion)));
    }

    void matches_differentVersionIsFalse() {
        // 在本机版本后追加字符构造必然不匹配的串（不硬编码版本号）
        QVERIFY(!appVersionMatches(QString::fromLatin1(ProtocolConstants::AppVersion) + QStringLiteral("-x")));
    }

    void matches_malformedPeerVersionIsFalse() {
        QVERIFY(!appVersionMatches(QStringLiteral("1.0")));
        QVERIFY(!appVersionMatches(QString()));
    }
};

QTEST_MAIN(TestAppVersion)
#include "test_appversion.moc"
