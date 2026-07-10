#include <QtTest/QTest>
#include <QtGui/QImage>
#include <memory>
#include <utility>

#include "../../src/server/dataflow/DataFlowStructures.h"

/**
 * @brief Unit tests for CapturedFrame after zero-copy refactor.
 *
 * CapturedFrame now holds the underlying QImage via std::shared_ptr<QImage>
 * so copies of a CapturedFrame share the same underlying image buffer.
 */
class TestCapturedFrame : public QObject {
    Q_OBJECT

private:
    // Build a deterministic, non-empty QImage for tests.
    static QImage makeImage(int width = 100, int height = 100) {
        QImage img(width, height, QImage::Format_RGB888);
        img.fill(Qt::red);
        return img;
    }

private slots:
    // Default-constructed frame has no image and must be reported invalid.
    void testDefaultConstructInvalid() {
        CapturedFrame frame;
        QVERIFY(!frame.isValid());
        QVERIFY(!frame.image);                 // empty shared_ptr
        QCOMPARE(frame.dataSize(), qint64(0));
        QCOMPARE(frame.frameId, quint64(0));
    }

    // Constructing from a QImage yields a valid frame with non-zero data size.
    void testConstructFromQImage() {
        QImage img = makeImage(100, 100);
        CapturedFrame frame(img, 7);

        QVERIFY(frame.isValid());
        QVERIFY(frame.image);                  // shared_ptr not null
        QVERIFY(frame.image.get() != nullptr);
        QCOMPARE(frame.frameId, quint64(7));
        QCOMPARE(frame.originalSize, QSize(100, 100));
        QVERIFY(frame.dataSize() > 0);
    }

    // Copying a CapturedFrame must NOT deep-copy the underlying QImage:
    // both copies should point to the same QImage instance (zero-copy).
    void testCopySharesUnderlyingImage() {
        QImage img = makeImage(64, 64);
        CapturedFrame original(img, 1);
        CapturedFrame copy = original;

        QVERIFY(original.image);
        QVERIFY(copy.image);
        QCOMPARE(copy.image.get(), original.image.get());
        QCOMPARE(copy.frameId, original.frameId);
    }

    // Move-construction should leave the source's shared_ptr null.
    void testMoveLeavesSourceNull() {
        QImage img = makeImage(32, 32);
        CapturedFrame src(img, 2);
        QVERIFY(src.image);

        CapturedFrame dst(std::move(src));
        QVERIFY(!src.image);                   // moved-from: null
        QVERIFY(dst.image);
        QCOMPARE(dst.frameId, quint64(2));
    }

    // The new shared_ptr-taking constructor must accept a pre-built
    // std::shared_ptr<QImage> and reuse it without allocating a new QImage.
    void testSharedPtrCtor() {
        auto shared = std::make_shared<QImage>(makeImage(80, 60));
        QImage* raw = shared.get();

        CapturedFrame frame(shared, 42);

        QVERIFY(frame.isValid());
        QCOMPARE(frame.image.get(), raw);      // same underlying QImage
        QCOMPARE(frame.frameId, quint64(42));
        QCOMPARE(frame.originalSize, QSize(80, 60));
    }
};

QTEST_MAIN(TestCapturedFrame)
#include "test_captured_frame.moc"
