#pragma once

namespace llocr {

// The index of a key (a page or a block) after the item at `from` moved to
// `to`: the moved item takes the new index, the items between the two shift
// by one.
inline int remapIndexAfterMove(int key, int from, int to)
{
    if (key == from)
        return to;
    if (from < to && key > from && key <= to)
        return key - 1;
    if (from > to && key >= to && key < from)
        return key + 1;
    return key;
}

}  // namespace llocr
