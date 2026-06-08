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

    void testCoordinateMappingRoundTrip()
    {
        // 坐标映射在没有纹理上传时使用空尺寸（此时映射为恒等）
        GLTextureViewport vp;
        vp.resize(960, 540);

        // 无纹理时 mapToRemote/mapFromRemote 为恒等映射
        QPoint remote(960, 540);
        QPoint local = vp.mapFromRemote(remote);
        QCOMPARE(local, remote);
    }

private:
    QApplication *m_app = nullptr;
};
#endif // QT_NO_OPENGL

QTEST_MAIN(TestRenderSurface)
#include "test_render_surface.moc"
