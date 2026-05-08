#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtGui/QImage>

#ifndef QT_NO_OPENGL
#include "../src/client/window/GLTextureViewport.h"

class TestRenderSurface : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (!QApplication::instance()) {
            int argc = 0;
            char **argv = nullptr;
            m_app = new QApplication(argc, argv);
        }
    }

    void cleanupTestCase()
    {
        delete m_app;
        m_app = nullptr;
    }

    void testViewportCreatesWithoutCrash()
    {
        GLTextureViewport vp;
        QVERIFY(!vp.hasTexture());
        QVERIFY(vp.textureSize().isEmpty());
    }

    void testUploadFrameSetsTexture()
    {
        GLTextureViewport vp;
        vp.resize(256, 256);
        // makeCurrent() forces GL context creation + initializeGL() call
        // without needing to show() the widget, which works in headless.
        vp.makeCurrent();

        // Use a supported format that GLTextureViewport::chooseGLFormat accepts
        QImage img(64, 48, QImage::Format_RGB888);
        img.fill(Qt::blue);
        vp.uploadFrame(img);
        // After upload, texture should exist
        QVERIFY(vp.hasTexture());
        QCOMPARE(vp.textureSize(), QSize(64, 48));

        vp.doneCurrent();
    }

    void testCoordinateMappingRoundTrip()
    {
        GLTextureViewport vp;
        vp.setRemoteSize(QSize(1920, 1080));
        vp.resize(960, 540);
        vp.makeCurrent();

        // Upload a dummy texture so render rect computation has a valid
        // texture aspect ratio to work with
        QImage img(1920, 1080, QImage::Format_RGB888);
        img.fill(Qt::green);
        vp.uploadFrame(img);

        QPoint remote(960, 540);
        QPoint local = vp.mapFromRemote(remote);
        QPoint back = vp.mapToRemote(local);
        // After round-trip through normalized coords, we should get back
        // approximately the original remote point (within rounding error)
        QVERIFY(qAbs(back.x() - remote.x()) <= 1);
        QVERIFY(qAbs(back.y() - remote.y()) <= 1);

        vp.doneCurrent();
    }

    void testRemoteSizeConvenience()
    {
        GLTextureViewport vp;
        vp.resize(128, 128);
        vp.makeCurrent();

        QImage img(32, 32, QImage::Format_RGB888);
        img.fill(Qt::red);
        vp.setRemoteScreen(img);
        QVERIFY(vp.hasTexture());
        QCOMPARE(vp.textureSize(), QSize(32, 32));

        vp.doneCurrent();
    }

private:
    QApplication *m_app = nullptr;
};
#endif // QT_NO_OPENGL

QTEST_MAIN(TestRenderSurface)
#include "test_render_surface.moc"
