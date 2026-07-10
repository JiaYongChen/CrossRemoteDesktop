#include <QtTest/QTest>
#include <QtGui/QGuiApplication>
#include <QtGui/QImage>
#include <QtCore/QElapsedTimer>
#include "../src/common/logging/LoggingCategories.h"

#ifdef Q_OS_WIN
#include "../src/server/capture/DxgiCapture.h"
#endif

/**
 * @brief DXGI Desktop Duplication capture engine tests.
 *
 * These tests verify the DxgiCapture engine's initialization,
 * frame capture, error handling, and reinitialize capabilities.
 *
 * Note: on non-Windows platforms (or CI without a real GPU),
 * all DXGI tests are skipped via QSKIP. The test binary still
 * compiles and links on all platforms.
 */
class TestDxgiCapture : public QObject {
    Q_OBJECT

private slots:
    void test_initializeAndShutdown();
    void test_captureFrame();
    void test_capturePerformance();
    void test_reinitialize();
    void test_doubleInitialize();
    void test_captureWithoutInit();
    void test_shutdownIdempotent();
    void test_desktopSize();
};

void TestDxgiCapture::test_initializeAndShutdown() {
#ifndef Q_OS_WIN
    QSKIP("DXGI Desktop Duplication is Windows-only");
#else
    DxgiCapture capture;

    QVERIFY(!capture.isInitialized());

    bool ok = capture.initialize();
    if ( !ok ) {
        // In CI environments without a GPU, DXGI init may fail — skip gracefully
        QSKIP(qPrintable(QString("DXGI init failed (expected in headless CI): %1")
            .arg(capture.lastError())));
    }

    QVERIFY(capture.isInitialized());
    QVERIFY(!capture.desktopSize().isEmpty());

    capture.shutdown();
    QVERIFY(!capture.isInitialized());
#endif
}

void TestDxgiCapture::test_captureFrame() {
#ifndef Q_OS_WIN
    QSKIP("DXGI Desktop Duplication is Windows-only");
#else
    DxgiCapture capture;
    if ( !capture.initialize() ) {
        QSKIP(qPrintable(QString("DXGI init failed: %1").arg(capture.lastError())));
    }

    // Give the desktop a moment to produce a frame
    QTest::qWait(100);

    CaptureResult result = capture.captureFrame(500);

    // Frame could be null if no desktop content changed in the timeout window
    // But typically on a real desktop with a clock/cursor, it should succeed
    if ( result.frame.isNull() ) {
        qCWarning(lcServerCaptureDxgi) << "captureFrame returned null (timeout — no desktop update)";
        // This is acceptable — not a test failure
        return;
    }

    const QImage& frame = result.frame;

    // Verify the captured image has correct properties
    QCOMPARE(frame.size(), capture.desktopSize());
    QVERIFY(frame.format() == QImage::Format_RGB32
            || frame.format() == QImage::Format_ARGB32);
    QVERIFY(!frame.isNull());
    QVERIFY(frame.sizeInBytes() > 0);
#endif
}

void TestDxgiCapture::test_capturePerformance() {
#ifndef Q_OS_WIN
    QSKIP("DXGI Desktop Duplication is Windows-only");
#else
    DxgiCapture capture;
    if ( !capture.initialize() ) {
        QSKIP(qPrintable(QString("DXGI init failed: %1").arg(capture.lastError())));
    }

    QTest::qWait(100);

    // Warm up
    capture.captureFrame(500);

    // Measure capture time over multiple frames
    constexpr int NUM_FRAMES = 10;
    QElapsedTimer timer;
    int successfulFrames = 0;
    qint64 totalTime = 0;

    for ( int i = 0; i < NUM_FRAMES; ++i ) {
        timer.restart();
        CaptureResult result = capture.captureFrame(100);
        qint64 elapsed = timer.elapsed();

        if ( !result.frame.isNull() ) {
            ++successfulFrames;
            totalTime += elapsed;
        }
    }

    if ( successfulFrames > 0 ) {
        double avgMs = static_cast<double>(totalTime) / successfulFrames;

        // Performance threshold depends on build type:
        //   - Release: DXGI should deliver <2ms/frame (no debug layer overhead).
        //   - Debug:   D3D11 Debug Layer (D3D11_CREATE_DEVICE_DEBUG) adds ~20-30ms/frame
        //              of validation overhead per API call. 50ms is a safe upper bound
        //              that still proves DXGI beats GDI (~50-100ms/frame idle).
#ifdef QT_DEBUG
        constexpr double kMaxAvgMs = 80.0;  // Debug 构建下脏矩形检测增加 GetFrameDirtyRects 开销
        const char* kBuildKind = "Debug";
#else
        constexpr double kMaxAvgMs = 5.0;
        const char* kBuildKind = "Release";
#endif

        qCInfo(lcServerCaptureDxgi) << "DXGI capture performance (" << kBuildKind << "):"
            << successfulFrames << "/" << NUM_FRAMES << "frames,"
            << "avg:" << avgMs << "ms/frame (threshold:" << kMaxAvgMs << "ms)";

        QVERIFY2(avgMs < kMaxAvgMs,
            qPrintable(QString("Average capture time %1ms exceeds %2ms threshold (%3 build)")
                .arg(avgMs, 0, 'f', 2)
                .arg(kMaxAvgMs, 0, 'f', 1)
                .arg(kBuildKind)));
    } else {
        qCWarning(lcServerCaptureDxgi) << "No frames captured during performance test (timeout)";
    }
#endif
}

void TestDxgiCapture::test_reinitialize() {
#ifndef Q_OS_WIN
    QSKIP("DXGI Desktop Duplication is Windows-only");
#else
    DxgiCapture capture;
    if ( !capture.initialize() ) {
        QSKIP(qPrintable(QString("DXGI init failed: %1").arg(capture.lastError())));
    }

    QSize originalSize = capture.desktopSize();
    QVERIFY(capture.isInitialized());

    // Reinitialize should succeed
    bool ok = capture.reinitialize();
    QVERIFY(ok);
    QVERIFY(capture.isInitialized());
    QCOMPARE(capture.desktopSize(), originalSize);
#endif
}

void TestDxgiCapture::test_doubleInitialize() {
#ifndef Q_OS_WIN
    QSKIP("DXGI Desktop Duplication is Windows-only");
#else
    DxgiCapture capture;
    if ( !capture.initialize() ) {
        QSKIP(qPrintable(QString("DXGI init failed: %1").arg(capture.lastError())));
    }

    QVERIFY(capture.isInitialized());

    // Calling initialize() again should clean up first, then re-init
    bool ok = capture.initialize();
    QVERIFY(ok);
    QVERIFY(capture.isInitialized());
#endif
}

void TestDxgiCapture::test_captureWithoutInit() {
#ifndef Q_OS_WIN
    QSKIP("DXGI Desktop Duplication is Windows-only");
#else
    DxgiCapture capture;

    // Capture without initialization should return null
    CaptureResult result = capture.captureFrame(100);
    QVERIFY(result.frame.isNull());
#endif
}

void TestDxgiCapture::test_shutdownIdempotent() {
#ifndef Q_OS_WIN
    QSKIP("DXGI Desktop Duplication is Windows-only");
#else
    DxgiCapture capture;

    // Shutdown without init should be safe
    capture.shutdown();
    QVERIFY(!capture.isInitialized());

    // Init, shutdown, shutdown again
    if ( capture.initialize() ) {
        capture.shutdown();
        capture.shutdown();  // Second call should be harmless
        QVERIFY(!capture.isInitialized());
    }
#endif
}

void TestDxgiCapture::test_desktopSize() {
#ifndef Q_OS_WIN
    QSKIP("DXGI Desktop Duplication is Windows-only");
#else
    DxgiCapture capture;

    // Before init, desktop size should be empty
    QVERIFY(capture.desktopSize().isEmpty());

    if ( !capture.initialize() ) {
        QSKIP(qPrintable(QString("DXGI init failed: %1").arg(capture.lastError())));
    }

    // After init, desktop size must be valid
    QSize size = capture.desktopSize();
    QVERIFY(size.width() > 0);
    QVERIFY(size.height() > 0);

    // Compare with Qt's reported screen size
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if ( primaryScreen ) {
        QSize qtSize = primaryScreen->geometry().size();
        qCInfo(lcServerCaptureDxgi) << "DXGI desktop:" << size << "Qt screen:" << qtSize;
        // Should match (within DPI scaling tolerance)
        // Note: DXGI returns physical pixels, Qt returns logical pixels
        // So they might differ on HiDPI displays
        QVERIFY(size.width() >= qtSize.width());
        QVERIFY(size.height() >= qtSize.height());
    }
#endif
}

QTEST_MAIN(TestDxgiCapture)
#include "test_dxgi_capture.moc"
