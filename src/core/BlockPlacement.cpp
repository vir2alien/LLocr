#include "core/BlockPlacement.h"

#include "core/PageIndex.h"

namespace llocr {

namespace {

// Half a percent of the page: the tolerance the coordinates of two blocks are
// compared with.
constexpr qreal kEpsilon = 0.005;

// Two rectangles share a column when their horizontal spans cover at least
// half of the narrower one's width — the vertical position says nothing about
// the column.
bool sharesColumn(const QRectF &a, const QRectF &b)
{
    const qreal narrower = qMin(a.width(), b.width());
    if (narrower <= 0.0)
        return false;
    const qreal left = qMax(a.left(), b.left());
    const qreal right = qMin(a.right(), b.right());
    return right - left >= narrower / 2.0;
}

// The share of the smaller rectangle the two cover together.
qreal overlapShare(const QRectF &a, const QRectF &b)
{
    const QRectF shared = a.intersected(b);
    const qreal area = shared.width() * shared.height();
    const qreal smaller = qMin(a.width() * a.height(), b.width() * b.height());
    return smaller > 0.0 ? area / smaller : 0.0;
}

// The block lies after the drawn one: below it, or to the right in the same
// band.
bool liesAfter(const QRectF &block, const QRectF &drawn)
{
    if (block.top() >= drawn.bottom() - kEpsilon)
        return true;
    const qreal band = qMin(block.bottom(), drawn.bottom()) - qMax(block.top(), drawn.top());
    return band > 0.0 && block.left() >= drawn.right() - kEpsilon;
}

}  // namespace

Placement placementFor(const QList<BoundingBox> &boxes, const QRectF &rect, const QList<FreeSlot> &freed)
{
    Placement placement;
    qreal bestShare = 0.0;
    for (int i = 0; i < freed.size(); ++i) {
        const qreal share = overlapShare(freed.at(i).rect, rect);
        if (share < 0.5)
            continue;
        if (placement.slot < 0 || share > bestShare + kEpsilon || (qFuzzyCompare(share, bestShare) && freed.at(i).index < freed.at(placement.slot).index)) {
            placement.slot = i;
            bestShare = share;
        }
    }
    if (placement.slot >= 0) {
        placement.index = qBound(0, freed.at(placement.slot).index, boxes.size());
        return placement;
    }

    QList<int> candidates;
    for (int i = 0; i < boxes.size(); ++i) {
        if (sharesColumn(boxes.at(i).rect, rect))
            candidates.append(i);
    }
    if (candidates.isEmpty()) {
        for (int i = 0; i < boxes.size(); ++i)
            candidates.append(i);
    }
    for (const int index : candidates) {
        if (liesAfter(boxes.at(index).rect, rect)) {
            placement.index = index;
            return placement;
        }
    }
    placement.index = candidates.isEmpty() ? boxes.size() : candidates.last() + 1;
    return placement;
}

QList<FreeSlot> slotsAfterRemoval(const QList<FreeSlot> &freed, int removedIndex, const QRectF &removedRect)
{
    QList<FreeSlot> out;
    for (const FreeSlot &slot : freed) {
        if (slot.index == removedIndex)
            continue;
        FreeSlot moved = slot;
        if (moved.index > removedIndex)
            --moved.index;
        out.append(moved);
    }
    FreeSlot place;
    place.index = removedIndex;
    place.rect = removedRect;
    out.append(place);
    return out;
}

QList<FreeSlot> slotsAfterPlacement(const QList<FreeSlot> &freed, const Placement &placement)
{
    QList<FreeSlot> out;
    for (int i = 0; i < freed.size(); ++i) {
        if (i == placement.slot)
            continue;
        FreeSlot moved = freed.at(i);
        if (moved.index >= placement.index)
            ++moved.index;
        out.append(moved);
    }
    return out;
}

QList<FreeSlot> slotsAfterMove(const QList<FreeSlot> &freed, int from, int to)
{
    QList<FreeSlot> out;
    out.reserve(freed.size());
    for (const FreeSlot &slot : freed) {
        FreeSlot moved = slot;
        moved.index = remapIndexAfterMove(moved.index, from, to);
        out.append(moved);
    }
    return out;
}

}  // namespace llocr
