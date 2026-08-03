#include <QtTest>

#include "client/session/DecodePipeline.h"

/// DecodePipeline 重启机制：GL 上下文重建后管线必须可经 stop() → start() 重启
/// （RemoteDesktopSession 的 glContextReady 处理器依赖此重启路径恢复解码）
class DecodePipelineRestartTest : public QObject {
    Q_OBJECT
private slots:
    void startStopStartCycleRestartsPipeline();
};

void DecodePipelineRestartTest::startStopStartCycleRestartsPipeline() {
    DecodePipeline pipeline(QStringLiteral("restart-test"));
    QVERIFY(!pipeline.isRunning());

    pipeline.start();
    QVERIFY(pipeline.isRunning());

    pipeline.stop();
    QVERIFY(!pipeline.isRunning());

    // GL 上下文重建场景：先 stop 再 start，管线必须恢复运行
    pipeline.start();
    QVERIFY(pipeline.isRunning());

    pipeline.stop();
    QVERIFY(!pipeline.isRunning());
}

QTEST_MAIN(DecodePipelineRestartTest)
#include "test_decodepipeline.moc"
