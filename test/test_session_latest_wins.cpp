#include <QtTest/QTest>
#include <QtGui/QImage>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>

#include "../src/client/managers/SessionManager.h"
#include "../src/client/core/TripleBuffer.h"
#include "../src/client/core/FrameSlot.h"
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
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QCoreApplication::setOrganizationName("QrdTest");
        QCoreApplication::setApplicationName("RenderConfigTest");
    }

    void testLatestOnlyViaTripleBuffer() {
        // TripleBuffer inherently provides LatestOnly semantics — the consumer
        // always sees the latest committed frame. Write 3 frames and verify
        // only the last one is readable.
        TripleBuffer<FrameSlot> tb;
        for (int i = 0; i < 3; ++i) {
            FrameSlot* w = nullptr;
            int idx = tb.acquireWrite(w);
            QVERIFY(idx >= 0 && idx <= 2);
            w->image = QImage(10, 10, QImage::Format_RGB888);
            w->image.fill(QColor::fromHsv(i * 60, 255, 255));
            w->frameId = static_cast<quint64>(i);
            tb.commitWrite(idx);
        }
        FrameSlot* r = nullptr;
        int rs = tb.getReadSlot(r);
        QVERIFY(rs >= 0);
        QVERIFY(r != nullptr);
        // The only frame available should be the last (hue=120, green-ish).
        QCOMPARE(r->image.pixelColor(5, 5).hue(), 120);
        QCOMPARE(r->frameId, static_cast<quint64>(2));
        // And no more frames.
        QCOMPARE(tb.getReadSlot(r), -1);
    }

    void testSessionManagerExposesTripleBuffer() {
        SessionManager sm(QStringLiteral("conn-1"));
        TripleBuffer<FrameSlot>* tb = sm.frameBuffer();
        QVERIFY(tb != nullptr);
        // Initial state: no ready frame
        FrameSlot* r = nullptr;
        QCOMPARE(tb->getReadSlot(r), -1);
    }
};

QTEST_MAIN(TestSessionLatestWins)
#include "test_session_latest_wins.moc"
