#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVariant>

#include <cstring>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
#include <unistd.h>
#if defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#endif
#endif

#include "runtime/ModelMemoryEstimator.h"

namespace llocr {

namespace {

enum GGUFType : quint32 {
    Uint8 = 0,
    Int8 = 1,
    Uint16 = 2,
    Int16 = 3,
    Uint32 = 4,
    Int32 = 5,
    Float32 = 6,
    Bool = 7,
    String = 8,
    Array = 9,
    Uint64 = 10,
    Int64 = 11,
    Float64 = 12,
};

// The header of a GGUF holds the metadata and the tensor table; a vision
// model's header reaches tens of megabytes (the chat template alone is a few
// hundred kilobytes), so the reader grows its window instead of assuming one
// size.
constexpr qint64 kInitialHeaderBytes = 4 * 1024 * 1024;
constexpr qint64 kMaxHeaderBytes = 96 * 1024 * 1024;

// llama.cpp's scheduler reserves a little more than the shape predicts
// (ADR 191): the working set of the shipped models runs up to 12 % over the
// derived one, and the margin turns the estimate into a requirement — the
// card that runs the model, not the card it barely starts on.
constexpr qint64 kWorkingSetPercent = 112;

// The attention mask llama.cpp builds for the whole context: f16 per context
// token and ubatch token.
constexpr qint64 kMaskBytesPerContextToken = 2;

// llama.cpp's own defaults, used when the launch profile says nothing: the
// server's batch of 2048 tokens and an ubatch of 512.
constexpr int kDefaultCtxSize = 8192;
constexpr int kDefaultUbatchTokens = 512;

// The server's own allocations on top of the buffers: the logits buffer, the
// samplers, the graph metadata and the HTTP layer.
constexpr qint64 kServerOverheadBytes = 64 * 1024 * 1024;

// The allocator's slack: the caching allocator keeps the blocks it has
// handed out, and the freed ones do not go back to the card at once.
constexpr qint64 kOverheadShare = 8;  // per cent

class GgufReader
{
public:
    explicit GgufReader(const QString &path) : m_file(path) {}

    bool run(QHash<QString, QVariant> &out, QStringList &tensorNames, QString &error)
    {
        qint64 window = kInitialHeaderBytes;
        while (true) {
            if (!m_file.open(QIODevice::ReadOnly)) {
                error = m_file.errorString();
                return false;
            }
            m_data = m_file.read(window);
            m_file.close();

            m_truncated = false;
            m_tensorCount = 0;
            QString localError;
            const bool ok = parse(out, tensorNames, localError);
            if (!m_truncated) {
                if (!ok)
                    error = localError;
                return ok;
            }
            if (window >= kMaxHeaderBytes) {
                error = QStringLiteral("header larger than %1 bytes").arg(kMaxHeaderBytes);
                return false;
            }
            window *= 4;
        }
    }

private:
    bool parse(QHash<QString, QVariant> &out, QStringList &tensorNames, QString &error)
    {
        QBuffer buffer(&m_data);
        buffer.open(QIODevice::ReadOnly);
        QDataStream in(&buffer);
        in.setByteOrder(QDataStream::LittleEndian);

        char magic[4] = {};
        if (in.readRawData(magic, 4) != 4 || std::memcmp(magic, "GGUF", 4) != 0) {
            error = QStringLiteral("not a GGUF file");
            return false;
        }
        quint32 version = 0;
        if (!takeUint32(in, version)) {
            error = QStringLiteral("truncated header");
            return false;
        }
        if (version < 2) {
            error = QStringLiteral("unsupported GGUF version %1").arg(version);
            return false;
        }
        quint64 tensorCount = 0, kvCount = 0;
        if (!takeUint64(in, tensorCount) || !takeUint64(in, kvCount)) {
            error = QStringLiteral("truncated header counts");
            return false;
        }
        m_tensorCount = tensorCount;

        for (quint64 i = 0; i < kvCount; ++i) {
            QString key;
            if (!takeString(in, key)) {
                error = QStringLiteral("truncated metadata key");
                return false;
            }
            quint32 type = 0;
            if (!takeUint32(in, type)) {
                error = QStringLiteral("truncated metadata type");
                return false;
            }
            QVariant value;
            if (!takeValue(in, type, value)) {
                error = QStringLiteral("truncated metadata value");
                return false;
            }
            out.insert(key, value);
        }

        if (!readTensorNames(in, tensorNames, error))
            return false;
        return true;
    }

    bool readTensorNames(QDataStream &in, QStringList &names, QString &error)
    {
        for (quint64 i = 0; i < m_tensorCount; ++i) {
            QString name;
            if (!takeString(in, name)) {
                error = QStringLiteral("truncated tensor table");
                return false;
            }
            names.append(name);
            quint32 nDims = 0;
            if (!takeUint32(in, nDims)) {
                error = QStringLiteral("truncated tensor table");
                return false;
            }
            for (quint32 d = 0; d < nDims; ++d) {
                quint64 dim = 0;
                if (!takeUint64(in, dim)) {
                    error = QStringLiteral("truncated tensor table");
                    return false;
                }
            }
            quint32 type = 0;
            quint64 offset = 0;
            if (!takeUint32(in, type) || !takeUint64(in, offset)) {
                error = QStringLiteral("truncated tensor table");
                return false;
            }
        }
        return true;
    }

    bool takeUint8(QDataStream &in, quint8 &v) const
    {
        in >> v;
        if (in.status() != QDataStream::Ok) {
            m_truncated = true;
            return false;
        }
        return true;
    }

    bool takeUint32(QDataStream &in, quint32 &v) const
    {
        in >> v;
        if (in.status() != QDataStream::Ok) {
            m_truncated = true;
            return false;
        }
        return true;
    }

    bool takeUint64(QDataStream &in, quint64 &v) const
    {
        in >> v;
        if (in.status() != QDataStream::Ok) {
            m_truncated = true;
            return false;
        }
        return true;
    }

    bool takeString(QDataStream &in, QString &s) const
    {
        quint64 len = 0;
        if (!takeUint64(in, len))
            return false;
        if (len > static_cast<quint64>(in.device()->bytesAvailable())) {
            m_truncated = true;
            return false;
        }
        QByteArray raw(static_cast<int>(len), Qt::Uninitialized);
        if (in.readRawData(raw.data(), static_cast<int>(len)) != len) {
            m_truncated = true;
            return false;
        }
        s = QString::fromUtf8(raw);
        return true;
    }

    bool takeValue(QDataStream &in, quint32 type, QVariant &value) const
    {
        switch (type) {
        case Uint8: {
            quint8 v;
            if (!takeUint8(in, v))
                return false;
            value = v;
            return true;
        }
        case Int8: {
            qint8 v;
            in >> v;
            if (in.status() != QDataStream::Ok) {
                m_truncated = true;
                return false;
            }
            value = v;
            return true;
        }
        case Uint16: {
            quint16 v;
            in >> v;
            if (in.status() != QDataStream::Ok) {
                m_truncated = true;
                return false;
            }
            value = v;
            return true;
        }
        case Int16: {
            qint16 v;
            in >> v;
            if (in.status() != QDataStream::Ok) {
                m_truncated = true;
                return false;
            }
            value = v;
            return true;
        }
        case Uint32: {
            quint32 v;
            if (!takeUint32(in, v))
                return false;
            value = v;
            return true;
        }
        case Int32: {
            qint32 v;
            in >> v;
            if (in.status() != QDataStream::Ok) {
                m_truncated = true;
                return false;
            }
            value = v;
            return true;
        }
        case Float32: {
            quint32 raw;
            if (!takeUint32(in, raw))
                return false;
            float f{};
            std::memcpy(&f, &raw, sizeof f);
            value = f;
            return true;
        }
        case Bool: {
            quint8 v;
            if (!takeUint8(in, v))
                return false;
            value = (v != 0);
            return true;
        }
        case String: {
            QString v;
            if (!takeString(in, v))
                return false;
            value = v;
            return true;
        }
        case Uint64: {
            quint64 v;
            if (!takeUint64(in, v))
                return false;
            value = v;
            return true;
        }
        case Int64: {
            qint64 v;
            in >> v;
            if (in.status() != QDataStream::Ok) {
                m_truncated = true;
                return false;
            }
            value = v;
            return true;
        }
        case Float64: {
            quint64 raw;
            if (!takeUint64(in, raw))
                return false;
            double d{};
            std::memcpy(&d, &raw, sizeof d);
            value = d;
            return true;
        }
        case Array: {
            quint32 elemType = 0;
            if (!takeUint32(in, elemType))
                return false;
            quint64 count = 0;
            if (!takeUint64(in, count))
                return false;
            if (elemType == String) {
                for (quint64 i = 0; i < count; ++i) {
                    QString ignored;
                    if (!takeString(in, ignored))
                        return false;
                }
                value = QVariant();
            } else if (elemType == Array) {
                for (quint64 i = 0; i < count; ++i) {
                    QVariant ignored;
                    if (!takeValue(in, elemType, ignored))
                        return false;
                }
                value = QVariant();
            } else {
                const int size = scalarSize(elemType);
                if (size <= 0) {
                    m_truncated = true;
                    return false;
                }
                for (quint64 i = 0; i < count; ++i) {
                    QVariant ignored;
                    if (!takeValue(in, elemType, ignored))
                        return false;
                    Q_UNUSED(ignored);
                }
                value = QVariant();
            }
            return true;
        }
        default:
            m_truncated = true;
            return false;
        }
    }

    static int scalarSize(quint32 type)
    {
        switch (type) {
        case Uint8:
        case Int8:
        case Bool:
            return 1;
        case Uint16:
        case Int16:
            return 2;
        case Uint32:
        case Int32:
        case Float32:
            return 4;
        case Uint64:
        case Int64:
        case Float64:
            return 8;
        default:
            return 0;
        }
    }

    QByteArray m_data;
    QFile m_file;
    quint64 m_tensorCount = 0;
    mutable bool m_truncated = false;
};

qint64 readIntMeta(const QHash<QString, QVariant> &meta, const QStringList &arches, const QStringList &keys)
{
    for (const QString &arch : arches) {
        for (const QString &key : keys) {
            bool ok = false;
            const qint64 value = meta.value(arch + QLatin1Char('.') + key).toLongLong(&ok);
            if (ok && value > 0)
                return value;
        }
    }
    return 0;
}

// The layers that keep a KV cache. A hybrid architecture (Qwen3.5) keeps the
// recurrent state in SSM tensors and only the full-attention layers carry K
// and V — the tensor table says which layers those are. The speculative
// decoding head (the "nextn" block) is a draft layer of its own: llama.cpp
// runs it as a separate graph and keeps no cache for it.
int countKvLayers(const QStringList &tensorNames)
{
    QSet<int> layers;
    QSet<int> drafts;
    for (const QString &name : tensorNames) {
        if (!name.startsWith(QLatin1String("blk.")))
            continue;
        const int dot = name.indexOf(QLatin1Char('.'), 4);
        if (dot < 0)
            continue;
        bool ok = false;
        const int index = name.mid(4, dot - 4).toInt(&ok);
        if (!ok)
            continue;
        if (name.contains(QLatin1String("nextn."))) {
            drafts.insert(index);
            continue;
        }
        const QString tensor = name.mid(dot + 1);
        if (tensor.startsWith(QLatin1String("attn_k")) || tensor.startsWith(QLatin1String("attn_v")))
            layers.insert(index);
    }
    layers.subtract(drafts);
    return int(layers.size());
}

// llama.cpp reserves the graph's working set for the largest ubatch it may
// see: the query/key/value projection, the attention output and the
// feed-forward pair, each in f32. Calibrated against the size llama.cpp
// reports for the shipped models (the d3-mini Q4_K_M: 1312 MiB at an 8192
// ubatch).
qint64 computeBytesPerToken(qint64 nEmbd, qint64 nHead, qint64 nKvHead, qint64 headDim, qint64 nFf)
{
    if (nEmbd <= 0)
        return 0;
    qint64 bytes = nEmbd * 4 * 6;
    if (headDim > 0 && nHead > 0)
        bytes += headDim * (nHead + 2 * nKvHead) * 4;
    if (nFf > 0)
        bytes += nFf * 4 * 2;
    return bytes;
}

// The shape alone understates what llama.cpp reserves: a mixture-of-experts
// layer materializes the experts it routes a token to (the gate, the up
// projection and the down projection of each), and the plain decoder layers
// are only a part of the graph.
qint64 computeBytesPerToken(const ModelMemoryShape &shape)
{
    qint64 bytes = computeBytesPerToken(shape.nEmbd, shape.nHead, shape.nKvHead, shape.headDim, shape.nFf);
    if (shape.nMoeLayers > 0 && shape.nExpertUsed > 0 && shape.nExpertFf > 0)
        bytes += qint64(shape.nMoeLayers) * shape.nExpertUsed * shape.nExpertFf * 4 * 3;
    return bytes;
}

QString cacheType(const QString &type)
{
    const QString t = type.trimmed();
    if (t.contains(QLatin1String("f32"), Qt::CaseInsensitive))
        return QStringLiteral("f32");
    return QStringLiteral("f16");
}

}  // namespace

ModelMemoryEstimate estimateMemory(const ModelMemoryShape &shape, const ModelMemoryBudget &budget)
{
    ModelMemoryEstimate e;
    e.modelBytes = shape.weightsBytes;
    e.draftBytes = shape.draftBytes;
    e.mmprojBytes = shape.projectorBytes;
    e.ctxSize = budget.ctxSize > 0 ? budget.ctxSize : kDefaultCtxSize;
    e.bytesPerValue = budget.bytesPerValue;
    e.systemRamBytes = systemPhysicalRamBytes();

    e.nLayer = shape.nLayer;
    e.nKvLayer = shape.nKvLayers > 0 ? shape.nKvLayers : shape.nLayer;
    e.nKvHead = shape.nKvHead;
    e.headDim = shape.headDim;

    const qint64 kvPerToken = shape.kvBytesPerToken > 0 ? shape.kvBytesPerToken : 2LL * e.nKvLayer * shape.nKvHead * shape.headDim * e.bytesPerValue;
    e.kvCacheBytes = kvPerToken * e.ctxSize;

    const qint64 perToken = shape.workingSetBytesPerToken > 0 ? shape.workingSetBytesPerToken : computeBytesPerToken(shape) * kWorkingSetPercent / 100;
    const qint64 contextBytes = shape.workingSetContextBytes > 0 ? shape.workingSetContextBytes : kMaskBytesPerContextToken;
    if (budget.ubatchTokens > 0)
        e.computeBufferBytes = qint64(budget.ubatchTokens) * perToken + qint64(e.ctxSize) * budget.ubatchTokens * contextBytes;

    e.visionBufferBytes = shape.visionBufferBytes > 0 ? shape.visionBufferBytes : qint64(budget.visionTokens) * shape.visionBytesPerToken;

    e.totalBytes = e.modelBytes + e.draftBytes + e.mmprojBytes + e.kvCacheBytes + e.computeBufferBytes + e.visionBufferBytes;
    // The server's own allocations: the logits buffer, the samplers, the
    // graph metadata and the allocator's slack.
    e.overheadBytes = kServerOverheadBytes + e.totalBytes * kOverheadShare / 100;
    e.requiredVramBytes = e.totalBytes + e.overheadBytes;
    return e;
}

ModelMemoryShape shapeFromProfile(const ModelProfiles::Memory &memory)
{
    ModelMemoryShape shape;
    shape.nLayer = memory.nLayer;
    shape.nKvLayers = memory.nKvLayers;
    shape.nHead = memory.nHead;
    shape.nKvHead = memory.nKvHead;
    shape.headDim = memory.headDim;
    shape.nEmbd = memory.nEmbd;
    shape.nFf = memory.nFf;
    shape.nMoeLayers = memory.nMoeLayers;
    shape.nExpertUsed = memory.nExpertUsed;
    shape.nExpertFf = memory.nExpertFf;
    shape.visionBytesPerToken = memory.visionBytesPerToken;
    shape.visionBufferBytes = memory.visionBufferBytes;
    shape.workingSetBytesPerToken = memory.workingSetBytesPerToken;
    shape.workingSetContextBytes = memory.workingSetContextBytes;
    shape.kvBytesPerToken = memory.kvBytesPerToken;
    return shape;
}

ModelMemoryBudget budgetFromLaunch(const LaunchProfile &launch, int visionTokens)
{
    ModelMemoryBudget budget;
    budget.ctxSize = kDefaultCtxSize;
    budget.ubatchTokens = kDefaultUbatchTokens;
    if (const LaunchParameter *row = launch.find(QStringLiteral("ctx-size")); row && row->kind == LaunchValueKind::Number)
        budget.ctxSize = int(row->value.toDouble());
    if (const LaunchParameter *row = launch.find(QStringLiteral("ubatch-size")); row && row->kind == LaunchValueKind::Number)
        budget.ubatchTokens = int(row->value.toDouble());
    const LaunchParameter *cacheK = launch.find(QStringLiteral("cache-type-k"));
    const LaunchParameter *cacheV = launch.find(QStringLiteral("cache-type-v"));
    if ((cacheK && cacheK->kind == LaunchValueKind::Text && cacheType(cacheK->value.toString()) == QLatin1String("f32")) ||
        (cacheV && cacheV->kind == LaunchValueKind::Text && cacheType(cacheV->value.toString()) == QLatin1String("f32")))
        budget.bytesPerValue = 4;
    budget.visionTokens = visionTokens;
    return budget;
}

ModelMemoryShape shapeFromFile(const QString &modelPath, bool *ok, QString *error)
{
    QHash<QString, QVariant> meta;
    QStringList tensorNames;
    GgufReader reader(modelPath);
    QString readError;
    const bool valid = reader.run(meta, tensorNames, readError);
    if (ok)
        *ok = valid;
    if (error)
        *error = readError;

    ModelMemoryShape shape;
    qint64 nLayer = 0, nKvHead = 0, headCount = 0, nEmbd = 0, nFf = 0, headDim = 0;
    if (valid) {
        QStringList arches;
        const QString declared = meta.value(QStringLiteral("general.architecture")).toString();
        if (!declared.isEmpty())
            arches.append(declared);
        for (const QString &arch : {QStringLiteral("llama"), QStringLiteral("qwen2"), QStringLiteral("qwen35"), QStringLiteral("gptneox"), QStringLiteral("gemma4"), QStringLiteral("glm4")}) {
            if (!arches.contains(arch))
                arches.append(arch);
        }
        nLayer = readIntMeta(meta, arches, {QStringLiteral("block_count"), QStringLiteral("n_layer")});
        nKvHead = readIntMeta(meta, arches, {QStringLiteral("attention.head_count_kv"), QStringLiteral("n_head_kv")});
        headCount = readIntMeta(meta, arches, {QStringLiteral("attention.head_count"), QStringLiteral("n_head")});
        nEmbd = readIntMeta(meta, arches, {QStringLiteral("embedding_length"), QStringLiteral("n_embd")});
        nFf = readIntMeta(meta, arches, {QStringLiteral("feed_forward_length")});
        headDim = readIntMeta(meta, arches, {QStringLiteral("attention.key_length")});
        // A mixture of experts: the layers that route a token to experts, and
        // the experts they pick.
        const qint64 dense = readIntMeta(meta, arches, {QStringLiteral("leading_dense_block_count")});
        shape.nMoeLayers = int(std::max<qint64>(0, nLayer - std::max<qint64>(1, dense)));
        shape.nExpertUsed = int(readIntMeta(meta, arches, {QStringLiteral("expert_used_count")}));
        shape.nExpertFf = int(readIntMeta(meta, arches, {QStringLiteral("expert_feed_forward_length")}));
    }
    if (headDim <= 0 && headCount > 0 && nEmbd > 0)
        headDim = nEmbd / headCount;

    if (nLayer <= 0)
        nLayer = 28;
    if (nKvHead <= 0)
        nKvHead = 8;
    if (headDim <= 0)
        headDim = 128;

    shape.nLayer = int(nLayer);
    shape.nKvHead = int(nKvHead);
    shape.headDim = int(headDim);
    shape.nHead = int(headCount);
    shape.nEmbd = int(nEmbd);
    shape.nFf = int(nFf);
    shape.nKvLayers = countKvLayers(tensorNames);
    return shape;
}

ModelMemoryEstimate estimateModelMemory(const QString &modelPath, int ctxSize, const QString &cacheTypeK, const QString &cacheTypeV, const QString &mmprojPath, int ubatchTokens)
{
    bool valid = false;
    QString error;
    ModelMemoryShape shape = shapeFromFile(modelPath, &valid, &error);
    shape.weightsBytes = QFileInfo(modelPath).size();
    shape.projectorBytes = mmprojPath.isEmpty() ? 0 : QFileInfo(mmprojPath).size();

    ModelMemoryBudget budget;
    budget.ctxSize = ctxSize <= 0 ? kDefaultCtxSize : ctxSize;
    budget.ubatchTokens = ubatchTokens;
    const QString kt = cacheType(cacheTypeK);
    const QString vt = cacheType(cacheTypeV);
    budget.bytesPerValue = (kt == QLatin1String("f32") || vt == QLatin1String("f32")) ? 4 : 2;

    ModelMemoryEstimate e = estimateMemory(shape, budget);
    e.valid = valid;
    e.error = error;
    return e;
}

qint64 systemPhysicalRamBytes()
{
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX st;
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st))
        return static_cast<qint64>(st.ullTotalPhys);
    return 0;
#elif defined(Q_OS_MACOS)
    int64_t mem = 0;
    size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0)
        return static_cast<int64_t>(mem);
    return 0;
#elif defined(Q_OS_LINUX)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0)
        return static_cast<qint64>(pages) * pageSize;
    return 0;
#else
    return 0;
#endif
}

}  // namespace llocr
