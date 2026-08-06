#include <QtCore/QSize>
#include <QtCore/QThread>
#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

#include "client/managers/DecodeWorker.h"
#include "common/error/RdError.h"
#include "common/network/Protocol.h"

/// DecodeWorker 生命周期闸门 + decodeError 信号测试。
/// 不测试 GL 相关路径（initializeGL/cleanupGL 需要真实 GL 上下文）。
/// 注意：start() 是同步阻塞的 slot（内部直接运行 workLoop 直至停止），
/// 必须经 moveToThread + QueuedConnection 在独立线程中调用，否则测试线程死锁。
namespace {

/// RAII 作用域：将 DecodeWorker 迁移到独立线程并排队启动工作循环，
/// 析构时 requestStop → quit → wait。断言失败提前返回时也能安全清理线程。
class WorkerThreadScope {
public:
    explicit WorkerThreadScope(DecodeWorker& worker)
        : m_worker(worker)
        , m_thread(new QThread) {
        m_worker.moveToThread(m_thread);
        m_thread->start();
        QMetaObject::invokeMethod(&m_worker, "start", Qt::QueuedConnection);
    }

    ~WorkerThreadScope() {
        m_worker.requestStop();
        m_thread->quit();
        m_thread->wait(5000);
        delete m_thread;
    }

private:
    DecodeWorker& m_worker;
    QThread* m_thread;
};

}  // namespace

class DecodeWorkerTest : public QObject {
    Q_OBJECT

private:
    /// 构造空数据帧（imageData 为空——无有效 JPEG 载荷）
    static ScreenData makeDummyFrame()
    {
        ScreenData sd;
        sd.x = 0;
        sd.y = 0;
        sd.width = 16;
        sd.height = 16;
        sd.originalWidth = 16;
        sd.originalHeight = 16;
        sd.dataSize = 0;
        sd.flags = 0;
        sd.captureTimestamp = 0;
        return sd;
    }

private slots:
    void enqueueFrame_returnsFalse_whenNotRunning()
    {
        DecodeWorker worker;
        const bool ok = worker.enqueueFrame(makeDummyFrame(), QSize(16, 16));
        QVERIFY(!ok);  // m_running 默认 false，enqueueFrame 应拒绝
    }

    void enqueueFrame_returnsTrue_whenRunning()
    {
        DecodeWorker worker;
        WorkerThreadScope scope(worker);

        // 轮询入队：仅当 workLoop 启动（m_running==true）后首次成功；
        // 未运行时的尝试返回 false 且无副作用（不产生入队）
        QTRY_VERIFY_WITH_TIMEOUT(worker.enqueueFrame(makeDummyFrame(), QSize(16, 16)), 3000);
    }

    void requestStop_preventsStartRevival()
    {
        DecodeWorker worker;

        worker.requestStop();
        worker.start();  // m_stopRequested 短路：start 直接返回，不复活工作循环

        const bool ok = worker.enqueueFrame(makeDummyFrame(), QSize(16, 16));
        QVERIFY(!ok);  // m_running 仍为 false
    }

    void decodeError_emitsOnFirstFailure()
    {
        DecodeWorker worker;
        QSignalSpy spy(&worker, &DecodeWorker::decodeError);
        WorkerThreadScope scope(worker);

        // 投递非 JPEG 数据 → 解码失败 → 失败流首帧（m_decodeFailStreak==1）发射 decodeError
        ScreenData badFrame = makeDummyFrame();
        badFrame.imageData = QByteArray(64, '\x00');
        QTRY_VERIFY_WITH_TIMEOUT(worker.enqueueFrame(badFrame, QSize(16, 16)), 3000);

        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 3000);
        const RdError err = spy.at(0).at(0).value<RdError>();
        QCOMPARE(err.code, ErrorCode::DecodeFailed);
        QVERIFY(!err.message.isEmpty());
    }
};

QTEST_MAIN(DecodeWorkerTest)
#include "test_decodeworker.moc"
