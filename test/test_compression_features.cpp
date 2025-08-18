#include <QtTest/QtTest>
#include <QtCore/QElapsedTimer>
#include <QtCore/QRandomGenerator>
#include <QtCore/QDebug>

#include "../src/common/codec/icompressor.h"
#include "../src/common/codec/zlib_compressor.h"
#include "../src/common/codec/lz4_compressor.h"
#include "../src/common/codec/zstd_compressor.h"

static QByteArray makeCompressibleData(int size)
{
    QByteArray data; data.reserve(size);
    const QByteArray pattern = "The quick brown fox jumps over the lazy dog. ";
    while (data.size() < size) data.append(pattern);
    data.truncate(size);
    return data;
}

static QByteArray makeRandomData(int size)
{
    QByteArray data; data.resize(size);
    auto gen = QRandomGenerator::global();
    for (int i = 0; i < size; ++i) data[i] = static_cast<char>(gen->bounded(256));
    return data;
}

class TestCompressionFeatures : public QObject {
    Q_OBJECT
private slots:
    void zlib_roundtrip_and_benchmark();
    void lz4_availability_and_roundtrip();
    void zstd_availability_and_roundtrip();
};

void TestCompressionFeatures::zlib_roundtrip_and_benchmark()
{
    ZlibCompressor zlib;
    const QByteArray input = makeCompressibleData(256 * 1024);

    QElapsedTimer t; t.start();
    const QByteArray compressed = zlib.compress(input, 6);
    const qint64 ct = t.elapsed();
    QVERIFY2(!compressed.isEmpty(), "Zlib compress should produce data");

    t.restart();
    const QByteArray decompressed = zlib.decompress(compressed);
    const qint64 dt = t.elapsed();
    QCOMPARE(decompressed, input);

    const double ratio = (compressed.size() > 0) ? (double)input.size() / (double)compressed.size() : 0.0;
    qInfo() << "Zlib" << "orig" << input.size() << "cmp" << compressed.size() << "ratio" << ratio << "c(ms)" << ct << "d(ms)" << dt;
}

void TestCompressionFeatures::lz4_availability_and_roundtrip()
{
    LZ4Compressor lz4;
    const QByteArray input = makeCompressibleData(128 * 1024);
    const QByteArray compressed = lz4.compress(input, 3);
#ifdef HAVE_LZ4
    QVERIFY2(!compressed.isEmpty(), "LZ4 enabled: compress should produce data");
    const QByteArray decompressed = lz4.decompress(compressed);
    QCOMPARE(decompressed, input);
    const double ratio = (double)input.size() / (double)compressed.size();
    qInfo() << "LZ4" << "orig" << input.size() << "cmp" << compressed.size() << "ratio" << ratio;
#else
    QVERIFY2(compressed.isEmpty(), "LZ4 disabled: compress should return empty");
#endif
}

void TestCompressionFeatures::zstd_availability_and_roundtrip()
{
    ZstdCompressor zstd;
    const QByteArray input = makeCompressibleData(128 * 1024);
    const QByteArray compressed = zstd.compress(input, 3);
#ifdef HAVE_ZSTD
    QVERIFY2(!compressed.isEmpty(), "ZSTD enabled: compress should produce data");
    const QByteArray decompressed = zstd.decompress(compressed);
    QCOMPARE(decompressed, input);
    const double ratio = (double)input.size() / (double)compressed.size();
    qInfo() << "ZSTD" << "orig" << input.size() << "cmp" << compressed.size() << "ratio" << ratio;
#else
    QVERIFY2(compressed.isEmpty(), "ZSTD disabled: compress should return empty");
#endif
}

QTEST_MAIN(TestCompressionFeatures)
#include "test_compression_features.moc"
