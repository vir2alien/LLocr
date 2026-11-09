#include <QtTest>

#include "core/OcrResult.h"
#include "parsers/BlockRender.h"

using namespace llocr;

class TestBlockBoxMap : public QObject
{
    Q_OBJECT

private slots:
    // No page text means no blocks: the editor gets an empty tag map.
    void emptyPageGivesEmptyMap() { QVERIFY(blockBoxMap(QString(), {}).isEmpty()); }

    // One box per top-level "\n\n"-separated block, in page order.
    // "# T" (0–2), "first" (5–9), "second" (12–17).
    void blockPerBox()
    {
        const QList<BlockTextRange> ranges = {{0, 0, 3}, {1, 5, 5}, {2, 12, 6}};
        QCOMPARE(blockBoxMap(QStringLiteral("# T\n\nfirst\n\nsecond"), ranges), (QVariantList{0, 1, 2}));
    }

    // A box whose rendered text holds blank lines spans several blocks.
    // "one" (0–2), "two" (5–7), "three" (10–14); one range covers them all.
    void multiBlockBox()
    {
        const QList<BlockTextRange> ranges = {{5, 0, 15}};
        QCOMPARE(blockBoxMap(QStringLiteral("one\n\ntwo\n\nthree"), ranges), (QVariantList{5, 5, 5}));
    }

    // Text the user inserted outside every range (or an unmapped page) is
    // owned by nobody.
    void unmappedBlocksAreMinusOne()
    {
        const QList<BlockTextRange> ranges = {{0, 0, 3}};
        QCOMPARE(blockBoxMap(QStringLiteral("aaa\n\nbbb"), ranges), (QVariantList{0, -1}));
        QCOMPARE(blockBoxMap(QStringLiteral("aaa\n\nbbb"), {}), (QVariantList{-1, -1}));
    }
};

QTEST_GUILESS_MAIN(TestBlockBoxMap)
#include "test_block_box_map.moc"
