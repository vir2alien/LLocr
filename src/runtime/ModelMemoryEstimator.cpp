#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QHash>
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

// ---------------------------------------------------------------------------
// Minimal GGUF header metadata reader.
//
// Layout (version ≥ 2, strings are u64-length + bytes):
//   magic   : 4 bytes "GGUF"
//   version : u32
//   tensor_count : u64
//   metadata_kv_count : u64
//   then `metadata_kv_count` kv-pairs:
//     key        : u64 len + bytes
//     value_type : u32
//     value      : scalar or array (see GGUFType)
// We only need a small subset of integer hyperparameters, so anything else
// (strings, arrays, floats) is skipped by reading past it.
// ---------------------------------------------------------------------------

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

class GgufReader {
public:
    // GGUF metadata lives at the start of the file; a bounded prefix is enough
    // for the few-KB header. Reading the whole multi-GB model would freeze the
    // GUI thread (estimateModelMemory is Q_INVOKABLE) and can OOM.
    static constexpr qint64 kMaxHeaderBytes = 8 * 1024 * 1024;  // 8 MiB

    GgufReader(const QString &path)
    {
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::ReadOnly)) {
            m_error = m_file.errorString();
            return;
        }
        m_data = m_file.read(kMaxHeaderBytes);
    }

    // § review 1.4: the whole header is read through QDataStream over the
    // bounded prefix. LittleEndian matches the GGUF format; status() after each
    // read replaces the manual position/bounds tracking. Float32/Float64 are
    // read as raw little-endian bit patterns and memcpy'd into float/double:
    // QDataStream's single setFloatingPointPrecision cannot serve a header that
    // interleaves Float32 and Float64 values, and memcpy avoids strict-aliasing
    // UB (review 4.6).
    bool run(QHash<QString, QVariant> &out, QString &error)
    {
        if (!m_error.isEmpty()) {
            error = m_error;
            return false;
        }
        QDataStream in(m_data);
        in.setByteOrder(QDataStream::LittleEndian);

        char magic[4] = {};
        if (in.readRawData(magic, 4) != 4
            || std::memcmp(magic, "GGUF", 4) != 0) {
            error = QStringLiteral("not a GGUF file");
            return false;
        }
        if (!takeUint32(in, m_version))
            return setError(error, QStringLiteral("truncated header"));
        if (m_version < 2) {
            error = QStringLiteral("unsupported GGUF version %1").arg(m_version);
            return false;
        }
        quint64 tensorCount = 0, kvCount = 0;
        if (!takeU64(in, tensorCount) || !takeU64(in, kvCount))
            return setError(error, QStringLiteral("truncated header counts"));
        Q_UNUSED(tensorCount);

        for (quint64 i = 0; i < kvCount; ++i) {
            QString key;
            if (!takeString(in, key))
                return setError(error, QStringLiteral("truncated metadata key"));
            quint32 type = 0;
            if (!takeUint32(in, type))
                return setError(error, QStringLiteral("truncated metadata type"));
            QVariant value;
            if (!takeValue(in, type, value))
                return setError(error, QStringLiteral("truncated metadata value"));
            out.insert(key, value);
        }
        return true;
    }

private:
    bool setError(QString &error, const QString &msg) const
    {
        error = msg;
        return false;
    }

    bool takeUint8(QDataStream &in, quint8 &v) const
    {
        in >> v;
        return in.status() == QDataStream::Ok;
    }
    bool takeUint32(QDataStream &in, quint32 &v) const
    {
        in >> v;
        return in.status() == QDataStream::Ok;
    }
    bool takeU64(QDataStream &in, quint64 &v) const
    {
        in >> v;
        return in.status() == QDataStream::Ok;
    }
    bool takeString(QDataStream &in, QString &s) const
    {
        quint64 len = 0;
        if (!takeU64(in, len))
            return false;
        // len is attacker-controlled: never allocate past the bounded prefix.
        if (len > static_cast<quint64>(in.device()->bytesAvailable()))
            return false;
        QByteArray raw(static_cast<int>(len), Qt::Uninitialized);
        if (in.readRawData(raw.data(), static_cast<qint64>(len)) != static_cast<qint64>(len))
            return false;
        s = QString::fromUtf8(raw);
        return true;
    }

    // Reads a single metadata value of `type`, advancing past its payload.
    bool takeValue(QDataStream &in, quint32 type, QVariant &value) const
    {
        switch (type) {
        case Uint8: { quint8 v; if (!takeUint8(in, v)) return false; value = v; return true; }
        case Int8: { qint8 v; in >> v; if (in.status() != QDataStream::Ok) return false; value = v; return true; }
        case Uint16: { quint16 v; in >> v; if (in.status() != QDataStream::Ok) return false; value = v; return true; }
        case Int16: { qint16 v; in >> v; if (in.status() != QDataStream::Ok) return false; value = v; return true; }
        case Uint32: { quint32 v; if (!takeUint32(in, v)) return false; value = v; return true; }
        case Int32: { quint32 v; if (!takeUint32(in, v)) return false; value = static_cast<qint32>(v); return true; }
        case Float32: {
            quint32 raw; if (!takeUint32(in, raw)) return false;
            float f{}; std::memcpy(&f, &raw, sizeof f); value = static_cast<double>(f); return true;
        }
        case Bool: { quint8 v; if (!takeUint8(in, v)) return false; value = (v != 0); return true; }
        case String: { QString s; if (!takeString(in, s)) return false; value = s; return true; }
        case Uint64: { quint64 v; if (!takeU64(in, v)) return false; value = v; return true; }
        case Int64: { quint64 v; if (!takeU64(in, v)) return false; value = static_cast<qint64>(v); return true; }
        case Float64: {
            quint64 raw; if (!takeU64(in, raw)) return false;
            double d{}; std::memcpy(&d, &raw, sizeof d); value = d; return true;
        }
        case Array: {
            quint32 elemType = 0; quint64 count = 0;
            if (!takeUint32(in, elemType) || !takeU64(in, count)) return false;
            for (quint64 i = 0; i < count; ++i) {
                QVariant ignored;
                if (!takeValue(in, elemType, ignored)) return false;
            }
            value = QVariant(); // arrays skipped
            return true;
        }
        default:
            return false;
        }
    }

    mutable QByteArray m_data;
    mutable QString m_error;
    mutable quint32 m_version = 0;
    QFile m_file;
};

// Picks the first value among candidate keys that exists in `meta`.
bool readIntMeta(const QHash<QString, QVariant> &meta,
                 const QString &candidate1, const QString &candidate2,
                 qint64 &value)
{
    for (const QString &key : {candidate1, candidate2}) {
        if (meta.contains(key)) {
            value = meta.value(key).toLongLong();
            return true;
        }
    }
    return false;
}

QString cacheType(const QString &type)
{
    const QString t = type.trimmed();
    if (t.contains(QLatin1String("f32"), Qt::CaseInsensitive))
        return QStringLiteral("f32");
    return QStringLiteral("f16");
}

} // namespace

ModelMemoryEstimate estimateModelMemory(const QString &modelPath, int ctxSize,
                                        const QString &cacheTypeK,
                                        const QString &cacheTypeV)
{
    ModelMemoryEstimate e;
    e.modelBytes = QFileInfo(modelPath).size();
    e.ctxSize = ctxSize <= 0 ? 8192 : ctxSize;
    e.systemRamBytes = systemPhysicalRamBytes();

    // bytes per value: f32 → 4, otherwise f16 → 2. If either cache is f32 the
    // effective footprint is larger; we use the larger of the two.
    const QString kt = cacheType(cacheTypeK);
    const QString vt = cacheType(cacheTypeV);
    e.bytesPerValue = (kt == "f32" || vt == "f32") ? 4 : 2;

    // Parse hyperparameters. Best-effort: unknown architecture keys → fall back
    // to a rough per-token default so a warning can still be shown.
    QHash<QString, QVariant> meta;
    GgufReader reader(modelPath);
    e.valid = reader.run(meta, e.error);

    qint64 nLayer = 0, nKvHead = 0, headCount = 0, nEmbd = 0;
    qint64 headDim = 0;
    if (e.valid) {
        for (const QString &arch : {QStringLiteral("llama"), QStringLiteral("qwen2"),
                                    QStringLiteral("gptneox")}) {
            if (!nLayer)
                readIntMeta(meta, arch + ".block_count", arch + ".n_layer", nLayer);
            if (!nKvHead)
                readIntMeta(meta, arch + ".attention.head_count_kv",
                            arch + ".n_head_kv", nKvHead);
            if (!headCount)
                readIntMeta(meta, arch + ".attention.head_count",
                            arch + ".n_head", headCount);
            if (!nEmbd)
                readIntMeta(meta, arch + ".embedding_length",
                            arch + ".n_embd", nEmbd);
        }
    }
    // head_dim == n_embd / head_count for standard (non-MQA/GQA-by-groups) attn.
    if (headCount > 0 && nEmbd > 0)
        headDim = nEmbd / headCount;

    // Fall-back assumptions when metadata is missing (documented, approximate):
    // typical vision LLMs approximate 28 layers, 8 KV heads, 128 head dim.
    if (nLayer <= 0) nLayer = 28;
    if (nKvHead <= 0) nKvHead = 8;
    if (headDim <= 0) headDim = 128;

    e.nLayer = nLayer;
    e.nKvHead = nKvHead;
    e.headDim = headDim;
    e.kvCacheBytes = 2LL * nLayer * nKvHead * headDim * e.ctxSize * e.bytesPerValue;
    e.totalBytes = e.modelBytes + e.kvCacheBytes;
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
        return static_cast<qint64>(mem);
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

} // namespace llocr