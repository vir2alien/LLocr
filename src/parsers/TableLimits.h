#pragma once

namespace llocr {

// A hallucinating model — or a page whose printed markers it transcribes —
// can repeat table markers with huge values; the formatter's grid is bounded
// so one reply cannot drive it into a multi-GB allocation or a GUI hang.
constexpr int kMaxTableColumns = 512;
constexpr int kMaxTableRows = 4096;

}  // namespace llocr
