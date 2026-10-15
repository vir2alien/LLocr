#pragma once

#include <QString>

namespace llocr {

// Human-readable byte size ("4.3 GiB"); empty for non-positive input, so
// callers can hide the size of a quant whose bytes are unknown.
QString formatBytes(qint64 bytes);

}  // namespace llocr
