#include <QtTest>

#include "core/BlockPlacement.h"

using namespace llocr;

namespace {

BoundingBox blockAt(qreal x, qreal y, qreal w, qreal h)
{
    BoundingBox box;
    box.rect = QRectF(x, y, w, h);
    return box;
}

// Three blocks of one column: a title, a text and a text.
QList<BoundingBox> column()
{
    return {blockAt(0.08, 0.06, 0.82, 0.10), blockAt(0.08, 0.20, 0.82, 0.20), blockAt(0.08, 0.50, 0.82, 0.30)};
}

}  // namespace

class TestBlockPlacement : public QObject
{
    Q_OBJECT

private slots:
    void aBlockAboveTheFirstOneTakesTheFirstPlace();
    void aBlockBetweenTwoTakesThePlaceBetweenThem();
    void aBlockBelowAllOfThemGoesLast();
    void aBlockToTheLeftInTheSameBandGoesFirst();
    void aBlockInAnotherColumnStaysInItsColumn();
    void aBlockOverAFreedPlaceTakesThatPlace();
    void aBlockAwayFromTheFreedPlaceTakesItsGeometricPlace();
    void theBestOfSeveralFreedPlacesWins();
    void removingABlockFreesItsPlace();
    void placingABlockShiftsTheFreedPlaces();
    void movingABlockShiftsTheFreedPlaces();
};

void TestBlockPlacement::aBlockAboveTheFirstOneTakesTheFirstPlace()
{
    const Placement placement = placementFor(column(), QRectF(0.10, 0.01, 0.20, 0.03), {});
    QCOMPARE(placement.index, 0);
    QCOMPARE(placement.slot, -1);
}

void TestBlockPlacement::aBlockBetweenTwoTakesThePlaceBetweenThem()
{
    const Placement placement = placementFor(column(), QRectF(0.10, 0.42, 0.20, 0.05), {});
    QCOMPARE(placement.index, 2);
}

void TestBlockPlacement::aBlockBelowAllOfThemGoesLast()
{
    const Placement placement = placementFor(column(), QRectF(0.10, 0.85, 0.20, 0.05), {});
    QCOMPARE(placement.index, 3);
}

void TestBlockPlacement::aBlockToTheLeftInTheSameBandGoesFirst()
{
    const QList<BoundingBox> band = {blockAt(0.30, 0.10, 0.30, 0.10), blockAt(0.70, 0.10, 0.20, 0.10)};
    const Placement placement = placementFor(band, QRectF(0.05, 0.12, 0.20, 0.05), {});
    QCOMPARE(placement.index, 0);
}

void TestBlockPlacement::aBlockInAnotherColumnStaysInItsColumn()
{
    // Two columns: the reading order is column by column.
    const QList<BoundingBox> two = {blockAt(0.05, 0.10, 0.40, 0.10), blockAt(0.05, 0.30, 0.40, 0.10), blockAt(0.55, 0.10, 0.40, 0.10), blockAt(0.55, 0.30, 0.40, 0.10)};
    // The drawn block sits under the left column — it belongs after the left
    // column's blocks, not at the end of the page.
    const Placement placement = placementFor(two, QRectF(0.05, 0.50, 0.40, 0.10), {});
    QCOMPARE(placement.index, 2);
}

void TestBlockPlacement::aBlockOverAFreedPlaceTakesThatPlace()
{
    const QList<FreeSlot> freed = {FreeSlot{1, QRectF(0.08, 0.20, 0.82, 0.20)}};
    const Placement placement = placementFor(column(), QRectF(0.10, 0.25, 0.30, 0.10), freed);
    QCOMPARE(placement.index, 1);
    QCOMPARE(placement.slot, 0);
}

void TestBlockPlacement::aBlockAwayFromTheFreedPlaceTakesItsGeometricPlace()
{
    const QList<FreeSlot> freed = {FreeSlot{1, QRectF(0.08, 0.20, 0.82, 0.20)}};
    const Placement placement = placementFor(column(), QRectF(0.10, 0.85, 0.20, 0.05), freed);
    QCOMPARE(placement.index, 3);
    QCOMPARE(placement.slot, -1);
}

void TestBlockPlacement::theBestOfSeveralFreedPlacesWins()
{
    const QList<FreeSlot> freed = {FreeSlot{0, QRectF(0.05, 0.05, 0.30, 0.10)}, FreeSlot{2, QRectF(0.05, 0.45, 0.30, 0.10)}};
    const Placement placement = placementFor(column(), QRectF(0.05, 0.46, 0.30, 0.08), freed);
    QCOMPARE(placement.index, 2);
    QCOMPARE(placement.slot, 1);
}

void TestBlockPlacement::removingABlockFreesItsPlace()
{
    const QList<FreeSlot> freed = slotsAfterRemoval({}, 1, QRectF(0.08, 0.20, 0.82, 0.20));
    QCOMPARE(freed.size(), 1);
    QCOMPARE(freed.first().index, 1);

    // A place further down the list shifts with the blocks before it.
    const QList<FreeSlot> shifted = slotsAfterRemoval({FreeSlot{3, QRectF(0.05, 0.60, 0.30, 0.10)}}, 1, QRectF(0.08, 0.20, 0.82, 0.20));
    QCOMPARE(shifted.size(), 2);
    QCOMPARE(shifted.at(0).index, 2);
    QCOMPARE(shifted.at(1).index, 1);
}

void TestBlockPlacement::placingABlockShiftsTheFreedPlaces()
{
    const QList<FreeSlot> freed = {FreeSlot{1, QRectF(0.08, 0.20, 0.82, 0.20)}, FreeSlot{3, QRectF(0.05, 0.60, 0.30, 0.10)}};
    const QList<FreeSlot> shifted = slotsAfterPlacement(freed, Placement{1, -1});
    QCOMPARE(shifted.size(), 2);
    QCOMPARE(shifted.at(0).index, 2);
    QCOMPARE(shifted.at(1).index, 4);
}

void TestBlockPlacement::movingABlockShiftsTheFreedPlaces()
{
    const QList<FreeSlot> freed = {FreeSlot{2, QRectF(0.05, 0.60, 0.30, 0.10)}};
    const QList<FreeSlot> shifted = slotsAfterMove(freed, 0, 3);
    QCOMPARE(shifted.size(), 1);
    QCOMPARE(shifted.first().index, 1);
}

QTEST_MAIN(TestBlockPlacement)
#include "test_block_placement.moc"
