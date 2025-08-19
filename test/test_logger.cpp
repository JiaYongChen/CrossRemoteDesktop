#include <QtTest/QtTest>
#include <QSignalSpy>
#include "../src/common/core/logger.h"

class TestLogger : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        Logger::instance()->setEnabled(true);
        Logger::instance()->setLogTargets(Logger::Console); // 避免实际写文件
        Logger::instance()->setLogLevel(Logger::Debug);
    }

    void emits_logMessage_signal_on_log() {
    QSignalSpy spy(Logger::instance(), &Logger::logMessage);
    Logger::instance()->info("hello-observer", "test-cat");
    QVERIFY(spy.count() >= 1);
    const auto args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), (int)Logger::Info);
        QCOMPARE(args.at(1).toString(), QString("hello-observer"));
        QCOMPARE(args.at(2).toString(), QString("test-cat"));
    }

    void emits_fileRotated_on_rotate() {
        // 打开文件目标到临时路径
        QString tmp = QDir::temp().filePath("qtlogger_test.log");
        Logger::instance()->setLogTargets(Logger::Console | Logger::File);
        Logger::instance()->setLogFile(tmp);
        Logger::instance()->setMaxFileSize(1); // 极小阈值触发

        QSignalSpy rotSpy(Logger::instance(), &Logger::fileRotated);
        // 连续写多条，触发轮转（极小阈值）
        for (int i=0; i<20; ++i) Logger::instance()->info(QString("line %1").arg(i));
        QVERIFY(rotSpy.count() >= 1);
    }
};

QTEST_APPLESS_MAIN(TestLogger)
#include "test_logger.moc"
