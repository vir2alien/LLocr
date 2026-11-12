#pragma once

#include <QList>
#include <QRectF>

#include "core/OcrResult.h"

namespace llocr {

// The place a removed block leaves behind: its position in the block list and
// the rectangle it covered. A block drawn over that rectangle takes the place
// in the text (ADR 310).
struct FreeSlot {
    int index = -1;
    QRectF rect;
};

// Where a block drawn on the page goes into the list of blocks.
struct Placement {
    int index = 0;  ///< the position the block takes in the list
    int slot = -1;  ///< the freed place the block took, -1 when it took none
};

// The place a block drawn at `rect` takes: the freed place it covers, or its
// position on the page.
Placement placementFor(const QList<BoundingBox> &boxes, const QRectF &rect, const QList<FreeSlot> &freed);

// The freed places of a page after its blocks changed.
QList<FreeSlot> slotsAfterRemoval(const QList<FreeSlot> &freed, int removedIndex, const QRectF &removedRect);
QList<FreeSlot> slotsAfterPlacement(const QList<FreeSlot> &freed, const Placement &placement);
QList<FreeSlot> slotsAfterMove(const QList<FreeSlot> &freed, int from, int to);

}  // namespace llocr
