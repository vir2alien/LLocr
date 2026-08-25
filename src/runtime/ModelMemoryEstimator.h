#pragma once

#include <QString>

namespace llocr {

// Best-effort memory estimate for a managed model launch (§H.2).
//
// The GGUF header stores the hyperparameters needed by the KV-cache formula
//    2 × n_layer × n_kv_head × head_dim × ctx_size × bytes_per_value
// under architecture-prefixed keys (e.g. "llama.block_count"). Reading them is
// best-effort: when a key is missing (or the file is not a GGUF), those
// unknowns are reported and the estimate falls back to a documented default so
// the UI can still show a rough number. All MMKV-cache sizes are approximate by
// design; the plan only requires a warning when the number clearly exceeds RAM.
struct ModelMemoryEstimate {
    qint64 modelBytes = 0;    // GGUF file size on disk
    qint64 kvCacheBytes = 0;  // KV-cache estimate (f16 unless cache-type f32)
    qint64 totalBytes = 0;    // modelBytes + kvCacheBytes
    qint64 systemRamBytes = 0; // total physical RAM of this machine

    int nLayer = 0;
    int nKvHead = 0;
    int headDim = 0;
    int ctxSize = 0;
    int bytesPerValue = 2;    // 2 = f16, 4 = f32

    bool valid = false;       // metadata parsed (ok for a precise KV estimate)
    QString error;            // read/parse problem, detail for debugging
};

// Computes an estimate for `modelPath` at `ctxSize`, honoring the on-disk cache
// type ("f16"/"f32") from cacheTypeK/cacheTypeV when set.
ModelMemoryEstimate estimateModelMemory(const QString &modelPath, int ctxSize,
                                        const QString &cacheTypeK,
                                        const QString &cacheTypeV);

// Total installed physical RAM in bytes (platform). Used as the comparison
// ceiling for the warning.
qint64 systemPhysicalRamBytes();

}  // namespace llocr