#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtGui/QImage>

#include "../../src/client/window/GLTextureViewport.h"

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
        vp.resize(960, 540);
        QVERIFY(vp.decodeTarget() == nullptr);  // GL 未初始化前无解码目标
    }

    void testCoordinateMappingIdentityWithoutTexture()
    {
        // 坐标映射在没有纹理上传时使用空尺寸（此时映射为恒等）
        GLTextureViewport vp;
        vp.resize(960, 540);

        // 无纹理时 mapToRemote 为恒等映射
        QPoint local(480, 270);
        QPoint remote = vp.mapToRemote(local);
        QCOMPARE(remote, local);
    }

private:
    QApplication *m_app = nullptr;
};

QTEST_MAIN(TestRenderSurface)
#include "test_render_surface.moc"
