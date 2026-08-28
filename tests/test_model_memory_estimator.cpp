#include <QByteArray>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QtTest>

#include <cstring>

#include "runtime/ModelMemoryEstimator.h"

using namespace llocr;

// Minimal GGUF v3 header writer for a few integer hyperparameters.
// Layout: "GGUF", version(u32), tensor_count(u64), metadata_kv_count(u64),
// then kv-pairs: key(str), value_type(u32), value (8-byte scalar).
static QByteArray ggufHeader(int blockCount, int nHead, int nKvHead, int nEmbd)
{
    struct W {
        QByteArray b;
        void s(const QString &x) {
            QByteArray t = x.toUtf8();
            quint64 l = t.size();
            b.append(reinterpret_cast<const char*>(&l), 8);
            b.append(t);
        }
        void k(const QString &key, quint32 v) {
            s(key);
            quint32 t = 4; // Uint32
            b.append(reinterpret_cast<const char*>(&t), 4);
            b.append(reinterpret_cast<const char*>(&v), 4);
        }
    } w;
    const QString a = QStringLiteral("llama");
    w.b.append("GGUF", 4);
    quint32 ver = 3;
    w.b.append(reinterpret_cast<const char*>(&ver), 4);
    quint64 tc = 0, kc = 4;
    w.b.append(reinterpret_cast<const char*>(&tc), 8);
    w.b.append(reinterpret_cast<const char*>(&kc), 8);
    w.k(a + QStringLiteral(".block_count"), blockCount);
    w.k(a + QStringLiteral(".attention.head_count"), nHead);
    w.k(a + QStringLiteral(".attention.head_count_kv"), nKvHead);
    w.k(a + QStringLiteral(".embedding_length"), nEmbd);
    return w.b;
}

// Mixed-type GGUF header (review 1.4): interleaves the Uint32 hyperparameters
// the estimator reads with the non-scalar/float keys real files carry, proving
// the QDataStream reader skips them without losing the integer hyperparams.
// Order: string, float32, uint32 × 2, int8, uint64, array-of-strings.
static QByteArray ggufMixedHeader(int blockCount, int nKhv)
{
    struct W {
        QByteArray b;
        void s(const QString &x) {
            QByteArray t = x.toUtf8();
            quint64 l = t.size();
            b.append(reinterpret_cast<const char*>(&l), 8);
            b.append(t);
        }
        void key(const QString &k, quint32 type) { s(k); b.append(reinterpret_cast<const char*>(&type), 4); }
        void i8(quint8 v) { b.append(reinterpret_cast<const char*>(&v), 1); }
        void u16(quint16 v) { b.append(reinterpret_cast<const char*>(&v), 2); }
        void u32(quint32 v) { b.append(reinterpret_cast<const char*>(&v), 4); }
        void u64(quint64 v) { b.append(reinterpret_cast<const char*>(&v), 8); }
        void f32(float v) { quint32 bits; std::memcpy(&bits, &v, sizeof bits); u32(bits); }
    } w;
    w.b.append("GGUF", 4);
    quint32 ver = 3;
    w.b.append(reinterpret_cast<const char*>(&ver), 4);
    quint64 tc = 0;
    w.b.append(reinterpret_cast<const char*>(&tc), 8);
    quint64 kc = 7;
    w.b.append(reinterpret_cast<const char*>(&kc), 8);

    const QString a = QStringLiteral("llama");
    // String value (type 8): "general.architecture"
    w.key(QStringLiteral("general.architecture"), 8);
    w.s(QStringLiteral("llama"));
    // Float32 value (type 6)
    w.key(a + QStringLiteral(".attention.layer_norm_rms_epsilon"), 6);
    w.f32(1.0e-5f);
    // Uint32 hyperparams (type 4)
    w.key(a + QStringLiteral(".block_count"), 4);
    w.u32(static_cast<quint32>(blockCount));
    w.key(a + QStringLiteral(".attention.head_count_kv"), 4);
    w.u32(static_cast<quint32>(nKhv));
    // Int8 value (type 1)
    w.key(QStringLiteral("int8.key"), 1);
    w.i8(-5);
    // Uint64 value (type 10)
    w.key(QStringLiteral("uint64.key"), 10);
    w.u64(1234567890123ULL);
    // Array of strings (type 9)
    w.key(QStringLiteral("general.file_type"), 9);
    w.u32(8); // elemType=String
    w.u64(2); // count
    w.s(QStringLiteral("a"));
    w.s(QStringLiteral("b"));
    return w.b;
}

// Writes a GGUF file (with optional trailing pad bytes) into `dir`; returns the
// path, or an empty string on failure. `dir` must outlive the estimate call.
static QString writeGguf(QTemporaryDir &dir, const QByteArray &header,
                         qint64 padBytes = 0)
{
    const QString path = dir.path() + QStringLiteral("/model.gguf");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(header);
    if (padBytes > 0)
        f.write(QByteArray(int(padBytes), 'x'));
    f.close();
    return path;
}

class TestModelMemoryEstimator : public QObject {
    Q_OBJECT
private slots:
    void parsesHparamsAndComputesKvCache();
    void parsesMixedTypeMeta();
    void nonGgufFallsBackToDefaults();
    void f32CacheDoublesBytesPerValue();
    void systemRamIsPositive();
};

void TestModelMemoryEstimator::parsesHparamsAndComputesKvCache()
{
    QTemporaryDir dir;
    // 28 layers, 32 heads → head_dim = 4096/32 = 128, 8 KV heads.
    const QString path = writeGguf(dir, ggufHeader(28, 32, 8, 4096), 1024 * 1024);
    QVERIFY(!path.isEmpty());
    const ModelMemoryEstimate e = estimateModelMemory(path, 8192, QString(), QString());
    QVERIFY(e.valid);
    QCOMPARE(e.nLayer, 28);
    QCOMPARE(e.nKvHead, 8);
    QCOMPARE(e.headDim, 128);
    QVERIFY(e.modelBytes > 0);
    QCOMPARE(e.kvCacheBytes, 2LL * 28 * 8 * 128 * 8192 * 2);
    QCOMPARE(e.totalBytes, e.modelBytes + e.kvCacheBytes);
}

void TestModelMemoryEstimator::parsesMixedTypeMeta()
{
    QTemporaryDir dir;
    // A realistic header: string/float32/array-of-strings metadata interleaved
    // with the Uint32 hyperparams. The QDataStream reader must skip the former
    // without losing the latter (review 1.4).
    const QString path = writeGguf(dir, ggufMixedHeader(30, 8), 0);
    QVERIFY(!path.isEmpty());
    const ModelMemoryEstimate e = estimateModelMemory(path, 4096, QString(), QString());
    QVERIFY(e.valid);
    QCOMPARE(e.nLayer, 30);
    QCOMPARE(e.nKvHead, 8);
    // head_count is absent → head_dim falls back to the documented default 128.
    QCOMPARE(e.headDim, 128);
}

void TestModelMemoryEstimator::nonGgufFallsBackToDefaults()
{
    QTemporaryDir dir;
    const QString path = writeGguf(dir, QByteArray("not a gguf file"), 0);
    QVERIFY(!path.isEmpty());
    const ModelMemoryEstimate e = estimateModelMemory(path, 8192, QString(), QString());
    QVERIFY(!e.valid);
    QVERIFY(e.kvCacheBytes > 0); // fall-back estimate, no crash
}

void TestModelMemoryEstimator::f32CacheDoublesBytesPerValue()
{
    QTemporaryDir dir;
    const QString path = writeGguf(dir, ggufHeader(28, 32, 8, 128 * 32), 0);
    QVERIFY(!path.isEmpty());
    const QString f32 = QStringLiteral("f32");
    const ModelMemoryEstimate e = estimateModelMemory(path, 8192, f32, QString());
    QCOMPARE(e.bytesPerValue, 4);
}

void TestModelMemoryEstimator::systemRamIsPositive()
{
    QVERIFY(systemPhysicalRamBytes() > 0);
}

QTEST_MAIN(TestModelMemoryEstimator)
#include "test_model_memory_estimator.moc"