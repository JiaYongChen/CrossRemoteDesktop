#ifndef QT_NO_OPENGL

#include <QtTest/QTest>
#include <QtGui/QImage>

// Expose the pure-function helper for testing via header-only include.
#include "../../src/client/windows/GLTextureViewport.h"

class TestGLUploadFormats : public QObject {
    Q_OBJECT
private slots:
    void testRGB888_MapsToGL_RGB() {
        GLTextureViewport::GLPixelLayout layout;
        QVERIFY(GLTextureViewport::chooseGLFormat(QImage::Format_RGB888, layout));
        QCOMPARE(layout.internalFormat, GLint(0x8051)); // GL_RGB8
        QCOMPARE(layout.format, GLenum(0x1907));        // GL_RGB
        QCOMPARE(layout.type, GLenum(0x1401));          // GL_UNSIGNED_BYTE
        QCOMPARE(layout.bytesPerPixel, 3);
    }
    void testRGBA8888_MapsToGL_RGBA() {
        GLTextureViewport::GLPixelLayout layout;
        QVERIFY(GLTextureViewport::chooseGLFormat(QImage::Format_RGBA8888, layout));
        QCOMPARE(layout.internalFormat, GLint(0x8058)); // GL_RGBA8
        QCOMPARE(layout.format, GLenum(0x1908));        // GL_RGBA
        QCOMPARE(layout.type, GLenum(0x1401));          // GL_UNSIGNED_BYTE
        QCOMPARE(layout.bytesPerPixel, 4);
    }
    void testUnsupportedFormatReturnsFalse() {
        GLTextureViewport::GLPixelLayout layout;
        QVERIFY(!GLTextureViewport::chooseGLFormat(QImage::Format_Mono, layout));
    }
};

QTEST_APPLESS_MAIN(TestGLUploadFormats)
#include "test_gl_upload_formats.moc"

#endif // QT_NO_OPENGL
