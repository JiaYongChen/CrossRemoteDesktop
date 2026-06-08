#include <QtTest/QTest>
#include <QtGui/QImage>

#include "../src/client/window/GLTextureViewport.h"

class TestGLWorkerUpload : public QObject {
    Q_OBJECT

private slots:
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
        QVERIFY(!GLTextureViewport::chooseGLFormat(QImage::Format_Mono, layout));
        QVERIFY(!GLTextureViewport::chooseGLFormat(QImage::Format_Indexed8, layout));
    }
};

QTEST_APPLESS_MAIN(TestGLWorkerUpload)
#include "test_gl_worker_upload.moc"
