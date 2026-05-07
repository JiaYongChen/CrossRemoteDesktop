#include <QtTest/QTest>
#include "../src/client/window/GLTextureViewport.h"

class TestGLPboUpload : public QObject {
    Q_OBJECT
private slots:
    void testPboIndexWrapsTwo() {
        // After 0, 1, 0, 1, 0... across 10 uploads, index stays in [0, 1].
        int idx = 0;
        for (int i = 0; i < 10; ++i) {
            idx = GLTextureViewport::nextPboIndex(idx);
            QVERIFY(idx == 0 || idx == 1);
        }
    }
    void testPboIndexAlternates() {
        QCOMPARE(GLTextureViewport::nextPboIndex(0), 1);
        QCOMPARE(GLTextureViewport::nextPboIndex(1), 0);
    }
};

QTEST_APPLESS_MAIN(TestGLPboUpload)
#include "test_gl_pbo_upload.moc"
