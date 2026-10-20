#pragma once

#include <QString>

namespace llocr {

struct ModelMemoryEstimate {
    qint64 modelBytes = 0;          // GGUF file size on disk
    qint64 mmprojBytes = 0;         // vision projector file size (0 when the model has none)
    qint64 kvCacheBytes = 0;        // KV-cache estimate (f16 unless cache-type f32)
    qint64 computeBufferBytes = 0;  // graph working set reserved for the ubatch
    qint64 totalBytes = 0;          // the four above
    qint64 systemRamBytes = 0;      // total physical RAM of this machine

    int nLayer = 0;
    int nKvLayer = 0;  // the layers that keep a KV cache (a hybrid architecture keeps fewer)
    int nKvHead = 0;
    int headDim = 0;
    int ctxSize = 0;
    int bytesPerValue = 2;  // 2 = f16, 4 = f32

    bool valid = false;  // metadata parsed (ok for a precise KV estimate)
    QString error;       // read/parse problem, detail for debugging
};

ModelMemoryEstimate estimateModelMemory(const QString &modelPath, int ctxSize, const QString &cacheTypeK, const QString &cacheTypeV, const QString &mmprojPath = QString(), int ubatchTokens = 0);

qint64 systemPhysicalRamBytes();

}  // namespace llocr
