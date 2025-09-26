#include <QtTest/QtTest>
#include <QtCore/QRandomGenerator>
#include <QtCore/QByteArray>

#include "../src/common/core/compression/Compression.h"

static QByteArray makeRandom(int size, quint32 seed)
{
    QByteArray data; data.resize(size);
    QRandomGenerator gen(seed);
    for (int i = 0; i < size; ++i) data[i] = static_cast<char>(gen.bounded(256));
    return data;
}

class TestCompressionDiff : public QObject {
    Q_OBJECT
private slots:
    void roundtrip_empty_previous();
    void roundtrip_identical_buffers();
    void roundtrip_small_edits_boundary_cases();
    void roundtrip_size_changes_grow_and_shrink();
    void fallback_to_full_data_when_diff_bigger();
};

void TestCompressionDiff::roundtrip_empty_previous()
{
    const QByteArray prev;
    const QByteArray curr = makeRandom(200, 42);

    const QByteArray diff = Compression::compressDifference(curr, prev);
    // 约定：previous 为空时直接返回 current
    QCOMPARE(diff, curr);

    const QByteArray applied = Compression::applyDifference(prev, diff);
    QCOMPARE(applied, curr);
}

void TestCompressionDiff::roundtrip_identical_buffers()
{
    const QByteArray prev = makeRandom(1024, 7);
    const QByteArray curr = prev;

    const QByteArray diff = Compression::compressDifference(curr, prev);
    const QByteArray applied = Compression::applyDifference(prev, diff);
    QCOMPARE(applied, curr);
}

void TestCompressionDiff::roundtrip_small_edits_boundary_cases()
{
    QByteArray prev(256, 0);
    for (int i = 0; i < prev.size(); ++i) prev[i] = static_cast<char>(i & 0xFF);

    QByteArray curr = prev;
    // 修改在块边界附近（实现使用64字节块）
    QList<int> positions{0, 63, 64, 65, 128, 255};
    for (int pos : positions) {
        curr[pos] = static_cast<char>((static_cast<unsigned char>(curr[pos]) + 1) & 0xFF);
    }

    const QByteArray diff = Compression::compressDifference(curr, prev);
    const QByteArray applied = Compression::applyDifference(prev, diff);
    QCOMPARE(applied, curr);
}

void TestCompressionDiff::roundtrip_size_changes_grow_and_shrink()
{
    // 变长（增长）
    const QByteArray prevGrow = makeRandom(256, 1001);
    QByteArray currGrow = prevGrow;
    currGrow.append(makeRandom(44, 1002)); // 增长一些字节
    currGrow[10] = static_cast<char>(currGrow[10] ^ 0x5A);

    QByteArray diffGrow = Compression::compressDifference(currGrow, prevGrow);
    QByteArray appliedGrow = Compression::applyDifference(prevGrow, diffGrow);
    QCOMPARE(appliedGrow, currGrow);

    // 变长（收缩）
    const QByteArray prevShrink = makeRandom(300, 2001);
    QByteArray currShrink = prevShrink.left(217);
    currShrink[5] = static_cast<char>(currShrink[5] ^ 0xA5);

    QByteArray diffShrink = Compression::compressDifference(currShrink, prevShrink);
    QByteArray appliedShrink = Compression::applyDifference(prevShrink, diffShrink);
    QCOMPARE(appliedShrink, currShrink);
}

void TestCompressionDiff::fallback_to_full_data_when_diff_bigger()
{
    // 对于很小的数据且差异很大时，应回退为完整数据打包
    const QByteArray prev = makeRandom(32, 3001);
    QByteArray curr = makeRandom(32, 3002);
    QVERIFY(prev != curr); // 高概率不同；若相同则两次生成不同种子保证不同

    const QByteArray diff = Compression::compressDifference(curr, prev);
    const QByteArray applied = Compression::applyDifference(prev, diff);
    QCOMPARE(applied, curr);

    // 回退时的负载通常会大于原数据（包含4字节标记头）
    QVERIFY(diff.size() > curr.size());
}

QTEST_MAIN(TestCompressionDiff)
#include "TestCompressionDiff.moc"
