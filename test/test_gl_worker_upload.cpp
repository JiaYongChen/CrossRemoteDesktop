#include <QtTest/QTest>
#include <QtGui/QImage>

// Include the necessary headers for the logical components under test.
// We test only pure-logic parts that don't require a live GL context:
//  - FrameSlot::uploadFence defaults to nullptr
//  - GLTextureViewport::chooseGLFormat still works
//  - GLTextureViewport::nextPboIndex wraps correctly

#include "../src/client/window/GLTextureViewport.h"
#include "../src/client/core/FrameSlot.h"

class TestGLWorkerUpload : public QObject {
    Q_OBJECT

private slots:
    void testFrameSlotFenceDefault() {
        FrameSlot slot;
        QVERIFY(slot.uploadFence == nullptr);
    }

    void testFrameSlotFenceCanBeSet() {
        FrameSlot slot;
        // Verify the field is writable (worker writes fence after upload).
        slot.uploadFence = nullptr;
        QVERIFY(slot.uploadFence == nullptr);

        // Verify other fields still work alongside the new fence field.
        slot.frameId = 42;
        QCOMPARE(slot.frameId, quint64(42));

        QImage img(64, 64, QImage::Format_RGB888);
        slot.image = img;
        QVERIFY(!slot.image.isNull());
        QCOMPARE(slot.image.size(), QSize(64, 64));

        slot.remoteSize = QSize(1920, 1080);
        QCOMPARE(slot.remoteSize, QSize(1920, 1080));
    }

    void testChooseGLFormatRGB888() {
        GLTextureViewport::GLPixelLayout layout{};
        QVERIFY(GLTextureViewport::chooseGLFormat(QImage::Format_RGB888, layout));
        QCOMPARE(layout.bytesPerPixel, 3);
        QCOMPARE(layout.format, GL_RGB);
    }

    void testChooseGLFormatRGBA8888() {
        GLTextureViewport::GLPixelLayout layout{};
        QVERIFY(GLTextureViewport::chooseGLFormat(QImage::Format_RGBA8888, layout));
        QCOMPARE(layout.bytesPerPixel, 4);
        QCOMPARE(layout.format, GL_RGBA);
    }

    void testChooseGLFormatRGBA8888Premultiplied() {
        GLTextureViewport::GLPixelLayout layout{};
        QVERIFY(GLTextureViewport::chooseGLFormat(
            QImage::Format_RGBA8888_Premultiplied, layout));
        QCOMPARE(layout.bytesPerPixel, 4);
        QCOMPARE(layout.format, GL_RGBA);
    }

    void testChooseGLFormatUnsupported() {
        GLTextureViewport::GLPixelLayout layout{};
        // Format_Mono is not supported by chooseGLFormat
        QVERIFY(!GLTextureViewport::chooseGLFormat(QImage::Format_Mono, layout));
        // Format_Indexed8 is not supported
        QVERIFY(!GLTextureViewport::chooseGLFormat(QImage::Format_Indexed8, layout));
    }

    void testNextPboIndexWrap() {
        QCOMPARE(GLTextureViewport::nextPboIndex(0), 1);
        QCOMPARE(GLTextureViewport::nextPboIndex(1), 0);
    }

    void testKpboCountIsTwo() {
        // Double-buffered PBO ring
        QCOMPARE(GLTextureViewport::kPboCount, 2);
    }
};

QTEST_APPLESS_MAIN(TestGLWorkerUpload)
#include "test_gl_worker_upload.moc"
