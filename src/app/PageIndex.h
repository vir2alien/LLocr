#pragma once

#include <QHash>
#include <utility>
#include <vector>

#include "core/PageIndex.h"

namespace llocr {

// Per-page side data (edit baselines, "edited" markers) follows the document's
// page indices, so it has to move with removePage()/movePage().
template <typename T> inline QHash<int, T> remapHashAfterRemove(const QHash<int, T> &map, int removedIndex)
{
    QHash<int, T> out;
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (it.key() == removedIndex)
            continue;
        out.insert(it.key() > removedIndex ? it.key() - 1 : it.key(), it.value());
    }
    return out;
}

template <typename T> inline QHash<int, T> remapHashAfterMove(const QHash<int, T> &map, int from, int to)
{
    QHash<int, T> out;
    out.reserve(map.size());
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        out.insert(remapIndexAfterMove(it.key(), from, to), it.value());
    return out;
}

}  // namespace llocr