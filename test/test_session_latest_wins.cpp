#include <QtTest/QTest>
#include <QtGui/QImage>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>

#include "../src/client/managers/SessionManager.h"
#include "../src/common/core/config/RenderConfig.h"

class TestSessionLatestWins : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_tmp;
    void writeConfig(const QString& policy) {
        QSettings s(m_tmp.filePath("cfg.ini"), QSettings::IniFormat);
        s.beginGroup("RemoteDesktop/Render");
        s.setValue("DropPolicy", policy);
        s.endGroup();
        s.sync();
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_tmp.path());
    }

private slots:
    void initTestCase() {
        QVERIFY(m_tmp.isValid());
        // Use IniFormat so RenderConfig::load() (default QSettings) reads
        // from the same format that writeConfig() writes to. On Windows,
        // QSettings() defaults to NativeFormat (registry); without this,
        // the config loading path is untested.
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QCoreApplication::setOrganizationName("QrdTest");
        QCoreApplication::setApplicationName("RenderConfigTest");
    }

    void testLatestOnlyCapacityIsOne() {
        writeConfig("LatestOnly");
        SessionManager sm(QStringLiteral("conn-1"));
        // Enqueue 3 distinct images via the test helper.
        for (int i = 0; i < 3; ++i) {
            QImage img(10, 10, QImage::Format_RGB888);
            img.fill(QColor::fromHsv(i * 60, 255, 255));
            sm.enqueueForTest(img);
        }
        QImage out;
        std::chrono::steady_clock::time_point ts;
        QVERIFY(sm.dequeueScreenFrame(out, ts));
        // The only frame left should be the last (hue=120, green-ish).
        QCOMPARE(out.pixelColor(5, 5).hue(), 120);
        // And no more frames.
        QVERIFY(!sm.dequeueScreenFrame(out, ts));
    }
};

QTEST_MAIN(TestSessionLatestWins)
#include "test_session_latest_wins.moc"
