#pragma once

#include <QString>

namespace llocr {

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

ModelMemoryEstimate estimateModelMemory(const QString &modelPath, int ctxSize,
                                        const QString &cacheTypeK,
                                        const QString &cacheTypeV);

qint64 systemPhysicalRamBytes();

}  // namespace llocr