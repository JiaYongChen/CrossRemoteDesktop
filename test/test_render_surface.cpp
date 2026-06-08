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
        GLTextureViewport vp;
        vp.setRemoteSize(QSize(1920, 1080));
        vp.resize(960, 540);

        // 坐标映射仅依赖 remoteSize 和 viewport 尺寸，无需纹理上传
        QPoint remote(960, 540);
        QPoint local = vp.mapFromRemote(remote);
        QPoint back = vp.mapToRemote(local);
        // 往返映射应回到原始坐标（允许舍入误差 ±1）
        QVERIFY(qAbs(back.x() - remote.x()) <= 1);
        QVERIFY(qAbs(back.y() - remote.y()) <= 1);
    }

private:
    QApplication *m_app = nullptr;
};
#endif // QT_NO_OPENGL

QTEST_MAIN(TestRenderSurface)
#include "test_render_surface.moc"
