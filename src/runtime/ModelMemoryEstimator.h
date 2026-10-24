#pragma once

#include <QString>

#include "core/LaunchProfile.h"
#include "core/ModelProfiles.h"

namespace llocr {

// What the model is: its weights, its projector, and the architecture the
// server reserves the KV cache and the working set for. The shape comes from
// the GGUF header when the model is on disk and from the model profile when
// the quantization is still a download (ADR 191).
struct ModelMemoryShape {
    qint64 weightsBytes = 0;
    qint64 projectorBytes = 0;

    int nLayer = 0;
    int nKvLayers = 0;  // the layers that keep a K/V cache (0 = all of them)
    int nHead = 0;
    int nKvHead = 0;
    int headDim = 0;
    int nEmbd = 0;
    int nFf = 0;
    int nMoeLayers = 0;   // the layers that route a token to experts
    int nExpertUsed = 0;  // the experts a token activates
    int nExpertFf = 0;    // the expert's own feed-forward width

    qint64 visionBytesPerToken = 0;  // the encoder's working set per image token
    qint64 visionBufferBytes = 0;    // the encoder's whole working set (0 = follow the token count)

    // A draft that is a model of its own runs in its own context: it holds a
    // KV cache and a compute buffer of its own.
    qint64 draftKvBytesPerToken = 0;  // that context's KV cache per context token
    qint64 draftContextBytes = 0;     // what it holds besides that

    // The speculative decoding draft: a second model the server loads next
    // to the target.
    qint64 draftBytes = 0;

    // llama.cpp's own numbers, where its scheduler reserves more than the
    // shape predicts: a sliding-window hybrid and a mixture of experts both
    // cost more than the plain decoder layers do.
    qint64 workingSetBytesPerToken = 0;  // 0 = derive it from the shape
    qint64 workingSetContextBytes = 0;   // per context token of the attention mask
    qint64 kvBytesPerToken = 0;          // 0 = derive it from the shape
    qint64 kvBytesFixed = 0;             // the sliding layers' cells (0 = the cache follows the context)
};

// What the server is asked to do: the context window, the ubatch and the
// image budget.
struct ModelMemoryBudget {
    int ctxSize = 8192;
    int ubatchTokens = 0;
    int bytesPerValue = 2;  // 2 = f16, 4 = f32
    int visionTokens = 0;
};

struct ModelMemoryEstimate {
    qint64 modelBytes = 0;          // GGUF file size on disk
    qint64 mmprojBytes = 0;         // vision projector file size (0 when the model has none)
    qint64 draftBytes = 0;          // the speculative decoding draft (0 when there is none)
    qint64 draftContextBytes = 0;   // the draft context's own KV cache and compute buffer
    qint64 kvCacheBytes = 0;        // KV-cache estimate (f16 unless cache-type f32)
    qint64 computeBufferBytes = 0;  // graph working set reserved for the ubatch
    qint64 visionBufferBytes = 0;   // the vision encoder's working set
    qint64 overheadBytes = 0;       // the server's own allocations
    qint64 totalBytes = 0;          // the six above
    qint64 requiredVramBytes = 0;   // the card that runs it: the total plus the margin
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

ModelMemoryEstimate estimateMemory(const ModelMemoryShape &shape, const ModelMemoryBudget &budget);

// The shape a model file declares: the architecture the GGUF header carries.
ModelMemoryShape shapeFromFile(const QString &modelPath, bool *ok = nullptr, QString *error = nullptr);

ModelMemoryEstimate estimateModelMemory(const QString &modelPath, int ctxSize, const QString &cacheTypeK, const QString &cacheTypeV, const QString &mmprojPath = QString(), int ubatchTokens = 0);

// The shape of a profile's model: the architecture the profile ships plus the
// numbers measured against llama.cpp (ADR 191). The caller adds the sizes of
// the files it knows.
ModelMemoryShape shapeFromProfile(const ModelProfiles::Memory &memory);

// The budget the launch profile asks for: the context window, the ubatch and
// the cache types. A profile that says nothing about the ubatch gets
// llama.cpp's own default.
ModelMemoryBudget budgetFromLaunch(const LaunchProfile &launch, int visionTokens);

qint64 systemPhysicalRamBytes();

}  // namespace llocr
