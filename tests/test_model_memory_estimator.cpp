#include <QByteArray>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QtTest>

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