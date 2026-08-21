#pragma once

namespace llocr {

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