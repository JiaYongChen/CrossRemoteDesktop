#include <QtTest/QtTest>
#include <QtCore/QDebug>
#include "../src/common/core/compression/Compression.h"

class TestCompressionBench : public QObject {
    Q_OBJECT
private slots:
    void bench_all_algorithms();
};

void TestCompressionBench::bench_all_algorithms()
{
    QByteArray data(256 * 1024, '\0');
    for (int i = 0; i < data.size(); ++i) data[i] = static_cast<char>('A' + (i % 23));
    auto results = Compression::benchmarkAllAlgorithms(data, Compression::DefaultCompression);
    QVERIFY(!results.isEmpty());
    for (const auto &r : results) {
        qInfo() << "ALG" << Compression::algorithmToString(r.algorithm)
                << "orig" << r.originalSize
                << "cmp" << r.compressedSize
                << "ratio" << r.compressionRatio
                << "c(ms)" << r.compressionTime
                << "ok" << r.success;
    }
}

QTEST_MAIN(TestCompressionBench)
#include "TestCompressionBench.moc"
