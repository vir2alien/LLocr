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
namespace {

struct GgufWriter {
    QByteArray b;

    void s(const QString &x)
    {
        QByteArray t = x.toUtf8();
        quint64 l = t.size();
        b.append(reinterpret_cast<const char *>(&l), 8);
        b.append(t);
    }
    void k(const QString &key, quint32 v)
    {
        s(key);
        quint32 t = 4;  // Uint32
        b.append(reinterpret_cast<const char *>(&t), 4);
        b.append(reinterpret_cast<const char *>(&v), 4);
    }
    void key(const QString &k, quint32 type)
    {
        s(k);
        b.append(reinterpret_cast<const char *>(&type), 4);
    }
    void i8(qint8 v) { b.append(reinterpret_cast<const char *>(&v), 1); }
    void u16(quint16 v) { b.append(reinterpret_cast<const char *>(&v), 2); }
    void u32(quint32 v) { b.append(reinterpret_cast<const char *>(&v), 4); }
    void u64(quint64 v) { b.append(reinterpret_cast<const char *>(&v), 8); }
    void f32(float v)
    {
        quint32 bits;
        std::memcpy(&bits, &v, sizeof bits);
        u32(bits);
    }
    // A tensor info: name, n_dims, dims, type, offset.
    void tensor(const QString &name)
    {
        s(name);
        u32(2);  // n_dims
        u64(1);
        u64(1);
        u32(0);  // type
        u64(0);  // offset
    }
};

}  // namespace

static QByteArray ggufHeader(int blockCount, int nHead, int nKvHead, int nEmbd)
{
    GgufWriter w;
    const QString a = QStringLiteral("llama");
    w.b.append("GGUF", 4);
    quint32 ver = 3;
    w.b.append(reinterpret_cast<const char *>(&ver), 4);
    quint64 tc = 0, kc = 4;
    w.b.append(reinterpret_cast<const char *>(&tc), 8);
    w.b.append(reinterpret_cast<const char *>(&kc), 8);
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
    GgufWriter w;
    w.b.append("GGUF", 4);
    quint32 ver = 3;
    w.b.append(reinterpret_cast<const char *>(&ver), 4);
    quint64 tc = 0;
    w.b.append(reinterpret_cast<const char *>(&tc), 8);
    quint64 kc = 7;
    w.b.append(reinterpret_cast<const char *>(&kc), 8);

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
    w.u32(8);  // elemType=String
    w.u64(2);  // count
    w.s(QStringLiteral("a"));
    w.s(QStringLiteral("b"));
    return w.b;
}

// A header with a tensor table: the estimator reads the layer numbers from it
// to tell the attention layers from the recurrent ones.
static QByteArray ggufHeaderWithTensors(int blockCount, int nHead, int nKvHead, int nEmbd, const QStringList &tensorNames)
{
    GgufWriter w;
    const QString a = QStringLiteral("llama");
    w.b.append("GGUF", 4);
    quint32 ver = 3;
    w.b.append(reinterpret_cast<const char *>(&ver), 4);
    quint64 tc = tensorNames.size(), kc = 4;
    w.b.append(reinterpret_cast<const char *>(&tc), 8);
    w.b.append(reinterpret_cast<const char *>(&kc), 8);
    w.k(a + QStringLiteral(".block_count"), blockCount);
    w.k(a + QStringLiteral(".attention.head_count"), nHead);
    w.k(a + QStringLiteral(".attention.head_count_kv"), nKvHead);
    w.k(a + QStringLiteral(".embedding_length"), nEmbd);
    for (const QString &name : tensorNames)
        w.tensor(name);
    return w.b;
}

// Writes a GGUF file (with optional trailing pad bytes) into `dir`; returns the
// path, or an empty string on failure. `dir` must outlive the estimate call.
static QString writeGguf(QTemporaryDir &dir, const QByteArray &header, qint64 padBytes = 0)
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

static QString writeFile(QTemporaryDir &dir, const QString &name, qint64 bytes)
{
    const QString path = dir.path() + QLatin1Char('/') + name;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QString();
    f.write(QByteArray(int(bytes), 'x'));
    f.close();
    return path;
}

class TestModelMemoryEstimator : public QObject
{
    Q_OBJECT
private slots:
    void parsesHparamsAndComputesKvCache();
    void parsesMixedTypeMeta();
    void nonGgufFallsBackToDefaults();
    void f32CacheDoublesBytesPerValue();
    void onlyTheAttentionLayersGetAKvCache();
    void theMtpDraftBlockKeepsNoCache();
    void countsTheProjectorAndTheWorkingSet();
    void countsTheVisionEncoderAndTheOverhead();
    void aFlatVisionCostDoesNotFollowTheTokenCount();
    void aDraftOfItsOwnHoldsAContext();
    void aSlidingWindowCacheHasAFixedPart();
    void everyProfileCarriesItsArchitecture();
    void theRequirementFollowsTheMeasuredNumbers();
    void aPageFillsTheImageBudgetAndACropDoesNot();
    void theLaunchProfileSetsTheBudget();
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
    QVERIFY(e.kvCacheBytes > 0);  // fall-back estimate, no crash
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

void TestModelMemoryEstimator::onlyTheAttentionLayersGetAKvCache()
{
    QTemporaryDir dir;
    // A hybrid architecture: layers 0 and 2 keep the recurrent state in SSM
    // tensors, only layers 1 and 3 carry K and V — so only those two get a KV
    // cache (the qwen35 case: 8 of 32 layers).
    QStringList tensors;
    tensors << QStringLiteral("blk.0.ssm_out.weight") << QStringLiteral("blk.0.attn_qkv.weight");
    tensors << QStringLiteral("blk.1.attn_k.weight") << QStringLiteral("blk.1.attn_v.weight");
    tensors << QStringLiteral("blk.2.ssm_out.weight") << QStringLiteral("blk.2.attn_qkv.weight");
    tensors << QStringLiteral("blk.3.attn_k.weight") << QStringLiteral("blk.3.attn_v.weight");
    const QString path = writeGguf(dir, ggufHeaderWithTensors(4, 32, 8, 4096, tensors), 0);
    QVERIFY(!path.isEmpty());

    const ModelMemoryEstimate e = estimateModelMemory(path, 8192, QString(), QString());
    QVERIFY(e.valid);
    QCOMPARE(e.nLayer, 4);
    QCOMPARE(e.nKvLayer, 2);
    QCOMPARE(e.kvCacheBytes, 2LL * 2 * 8 * 128 * 8192 * 2);
}

void TestModelMemoryEstimator::countsTheProjectorAndTheWorkingSet()
{
    QTemporaryDir dir;
    const QString path = writeGguf(dir, ggufHeader(28, 32, 8, 4096), 0);
    QVERIFY(!path.isEmpty());
    const QString projector = writeFile(dir, QStringLiteral("mmproj.gguf"), 400 * 1024 * 1024);
    QVERIFY(!projector.isEmpty());

    const ModelMemoryEstimate small = estimateModelMemory(path, 8192, QString(), QString(), projector, 4096);
    QVERIFY(small.valid);
    QCOMPARE(small.mmprojBytes, 400LL * 1024 * 1024);
    QVERIFY(small.computeBufferBytes > 0);

    const ModelMemoryEstimate large = estimateModelMemory(path, 8192, QString(), QString(), projector, 8192);
    QCOMPARE(large.computeBufferBytes, 2 * small.computeBufferBytes);
    QCOMPARE(large.totalBytes, large.modelBytes + large.mmprojBytes + large.kvCacheBytes + large.computeBufferBytes);
}

// The speculative decoding head is a draft layer of its own: llama.cpp runs it
// as a separate graph and keeps no cache for it (the qwen35 MTP block).
void TestModelMemoryEstimator::theMtpDraftBlockKeepsNoCache()
{
    QTemporaryDir dir;
    QStringList tensors;
    tensors << QStringLiteral("blk.0.attn_k.weight") << QStringLiteral("blk.0.attn_v.weight");
    tensors << QStringLiteral("blk.1.nextn.attn_k.weight") << QStringLiteral("blk.1.nextn.attn_v.weight");
    const QString path = writeGguf(dir, ggufHeaderWithTensors(2, 32, 8, 4096, tensors), 0);
    QVERIFY(!path.isEmpty());

    const ModelMemoryEstimate e = estimateModelMemory(path, 8192, QString(), QString());
    QVERIFY(e.valid);
    QCOMPARE(e.nKvLayer, 1);
    QCOMPARE(e.kvCacheBytes, 2LL * 1 * 8 * 128 * 8192 * 2);
}

// The requirement of a card: the weights, the projector, the draft, the KV
// cache, the working set, the vision encoder and the server's overhead.
void TestModelMemoryEstimator::countsTheVisionEncoderAndTheOverhead()
{
    ModelMemoryShape shape;
    shape.weightsBytes = 1000LL * 1024 * 1024;
    shape.projectorBytes = 500LL * 1024 * 1024;
    shape.draftBytes = 50LL * 1024 * 1024;
    shape.nLayer = 32;
    shape.nKvLayers = 8;
    shape.nHead = 16;
    shape.nKvHead = 4;
    shape.headDim = 256;
    shape.nEmbd = 2560;
    shape.nFf = 9216;
    shape.visionBytesPerToken = 110600;

    ModelMemoryBudget budget;
    budget.ctxSize = 16384;
    budget.ubatchTokens = 512;
    budget.visionTokens = 1024;

    const ModelMemoryEstimate e = estimateMemory(shape, budget);
    QCOMPARE(e.kvCacheBytes, 2LL * 8 * 4 * 256 * 16384 * 2);
    QCOMPARE(e.visionBufferBytes, 1024LL * 110600);
    QCOMPARE(e.totalBytes, e.modelBytes + e.draftBytes + e.mmprojBytes + e.kvCacheBytes + e.computeBufferBytes + e.visionBufferBytes);
    QVERIFY(e.computeBufferBytes > 0);
    // the overhead is what turns the buffers into a requirement
    QVERIFY(e.overheadBytes > 0);
    QCOMPARE(e.requiredVramBytes, e.totalBytes + e.overheadBytes);
    QVERIFY(e.requiredVramBytes > e.totalBytes);

    // a bigger context costs more
    ModelMemoryBudget bigger = budget;
    bigger.ctxSize = 32768;
    const ModelMemoryEstimate more = estimateMemory(shape, bigger);
    QVERIFY(more.kvCacheBytes > e.kvCacheBytes);
    QVERIFY(more.requiredVramBytes > e.requiredVramBytes);
}

// A draft that is a model of its own runs in its own context: its weights,
// its KV cache and its compute buffer are all memory the card has to hold.
void TestModelMemoryEstimator::aDraftOfItsOwnHoldsAContext()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();
    const ModelProfiles::Profile *gemma = ModelProfiles::find(profiles, QStringLiteral("gemma-4-e4b"));
    QVERIFY(gemma);
    QVERIFY(gemma->memory.draftBytes > 0);
    QVERIFY(gemma->memory.draftKvBytesPerToken > 0);
    QVERIFY(gemma->memory.draftContextBytes > 0);

    const ModelMemoryShape shape = shapeFromProfile(gemma->memory);
    QCOMPARE(shape.draftBytes, gemma->memory.draftBytes);

    ModelMemoryBudget budget;
    budget.ctxSize = 16384;
    budget.ubatchTokens = 512;
    const ModelMemoryEstimate e = estimateMemory(shape, budget);
    // the draft context's KV cache follows the context, the rest does not
    QCOMPARE(e.draftContextBytes, gemma->memory.draftContextBytes + gemma->memory.draftKvBytesPerToken * 16384);
    QCOMPARE(e.totalBytes, e.modelBytes + e.draftBytes + e.draftContextBytes + e.mmprojBytes + e.kvCacheBytes + e.computeBufferBytes + e.visionBufferBytes);
    QVERIFY(e.requiredVramBytes > e.totalBytes);

    // a bigger context makes the draft's cache bigger
    ModelMemoryBudget bigger = budget;
    bigger.ctxSize = 32768;
    QVERIFY(estimateMemory(shape, bigger).draftContextBytes > e.draftContextBytes);
}

// A sliding-window hybrid keeps a cache that does not follow the context:
// the sliding layers get a fixed number of cells.
void TestModelMemoryEstimator::aSlidingWindowCacheHasAFixedPart()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();
    const ModelProfiles::Profile *gemma = ModelProfiles::find(profiles, QStringLiteral("gemma-4-12b"));
    QVERIFY(gemma);
    QVERIFY(gemma->memory.kvBytesFixed > 0);

    const ModelMemoryShape shape = shapeFromProfile(gemma->memory);
    ModelMemoryBudget budget;
    budget.ctxSize = 16384;
    const ModelMemoryEstimate e = estimateMemory(shape, budget);
    // the full-attention layers follow the context, the sliding cells do not
    QCOMPARE(e.kvCacheBytes, 2LL * gemma->memory.nKvLayers * gemma->memory.nKvHead * gemma->memory.headDim * 2 * 16384 + gemma->memory.kvBytesFixed);

    ModelMemoryBudget smaller = budget;
    smaller.ctxSize = 8192;
    const ModelMemoryEstimate less = estimateMemory(shape, smaller);
    QCOMPARE(less.kvCacheBytes, e.kvCacheBytes - 128LL * 1024 * 1024);
}

// Every shipped model ships the architecture the estimate needs: a
// quantization that is not downloaded yet has no header to read.
// A projector whose cost does not follow the token count carries the whole
// working set: the gemma-4-E4B runs without flash attention, so its encoder
// holds an attention matrix over every patch of the page.
void TestModelMemoryEstimator::aFlatVisionCostDoesNotFollowTheTokenCount()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();
    const ModelProfiles::Profile *e4b = ModelProfiles::find(profiles, QStringLiteral("gemma-4-e4b"));
    QVERIFY(e4b);
    QVERIFY(e4b->memory.visionBufferBytes > 0);

    const ModelMemoryShape shape = shapeFromProfile(e4b->memory);
    ModelMemoryBudget budget;
    budget.visionTokens = 1120;
    const ModelMemoryEstimate e = estimateMemory(shape, budget);
    QCOMPARE(e.visionBufferBytes, e4b->memory.visionBufferBytes);

    ModelMemoryBudget bigger = budget;
    bigger.visionTokens = 4480;
    QCOMPARE(estimateMemory(shape, bigger).visionBufferBytes, e.visionBufferBytes);
}

void TestModelMemoryEstimator::everyProfileCarriesItsArchitecture()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();
    QVERIFY(!profiles.isEmpty());
    for (const ModelProfiles::Profile &profile : profiles) {
        QVERIFY2(profile.memory.valid, qPrintable(profile.id));
        QVERIFY(profile.memory.nEmbd > 0);
        QVERIFY(profile.memory.nLayer > 0);
        // the vision cost is a rate per image token, or the whole working set
        // of a projector whose cost does not follow the token count
        QVERIFY(profile.memory.visionBytesPerToken > 0 || profile.memory.visionBufferBytes > 0);
        QVERIFY(profile.files.mmproj.sizeBytes > 0);
    }
    // the minicpm-jev keeps a cache in 6 of its 24 layers
    const ModelProfiles::Profile *jev = ModelProfiles::find(profiles, QStringLiteral("minicpm-jev"));
    QVERIFY(jev);
    QCOMPARE(jev->memory.nKvLayers, 6);
    const ModelMemoryShape shape = shapeFromProfile(jev->memory);
    QCOMPARE(shape.nKvLayers, 6);
    QCOMPARE(shape.nEmbd, 2048);
}

// The requirement of the two models the numbers were measured on: the
// minicpm-jev, whose two-image decision request runs on the launch profile
// the decision role ships, and the GLM-OCR, whose page recognition reserves
// more in the vision encoder than in the cache.
void TestModelMemoryEstimator::theRequirementFollowsTheMeasuredNumbers()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();

    const ModelProfiles::Profile *jev = ModelProfiles::find(profiles, QStringLiteral("minicpm-jev"));
    QVERIFY(jev);
    ModelMemoryShape jevShape = shapeFromProfile(jev->memory);
    jevShape.weightsBytes = 2012021120;  // the curated Q8_0
    jevShape.projectorBytes = jev->files.mmproj.sizeBytes;
    jevShape.draftBytes = jev->memory.draftBytes;
    ModelMemoryBudget jevBudget;
    jevBudget.ctxSize = 16384;  // the launch profile the decision role ships
    jevBudget.ubatchTokens = 512;
    jevBudget.visionTokens = ModelProfiles::visionTokensFor(*jev, QStringLiteral("decision"));
    QCOMPARE(jevBudget.visionTokens, 512);  // the decision task sends a crop: the floor, not the page budget
    const ModelMemoryEstimate jevEstimate = estimateMemory(jevShape, jevBudget);
    // six of the 24 layers keep a cache at the 16384 context
    QCOMPARE(jevEstimate.kvCacheBytes, 2LL * jev->memory.nKvLayers * jev->memory.nKvHead * jev->memory.headDim * 2 * 16384);
    QVERIFY(jevEstimate.visionBufferBytes > 0);
    // the two-image request: the requirement holds the weights, the projector
    // and the cache with room for the buffers
    QVERIFY(jevEstimate.requiredVramBytes > jevShape.weightsBytes + jevShape.projectorBytes + jevEstimate.kvCacheBytes);
    QVERIFY(jevEstimate.requiredVramBytes < 6LL * 1024 * 1024 * 1024);

    const ModelProfiles::Profile *glm = ModelProfiles::find(profiles, QStringLiteral("glm-ocr"));
    QVERIFY(glm);
    ModelMemoryShape glmShape = shapeFromProfile(glm->memory);
    glmShape.weightsBytes = 950433408;  // the curated Q8_0
    glmShape.projectorBytes = glm->files.mmproj.sizeBytes;
    ModelMemoryBudget glmBudget;
    glmBudget.ctxSize = 16384;
    glmBudget.ubatchTokens = 512;
    glmBudget.visionTokens = ModelProfiles::visionTokensFor(*glm, QStringLiteral("layout"));
    QCOMPARE(glmBudget.visionTokens, 6144);
    const ModelMemoryEstimate glmEstimate = estimateMemory(glmShape, glmBudget);
    // the page fills the image budget: the encoder reserves more than the cache
    QVERIFY(glmEstimate.visionBufferBytes > glmEstimate.kvCacheBytes);
    QVERIFY(glmEstimate.requiredVramBytes > 4LL * 1024 * 1024 * 1024);
}

// A page fills the model's image budget; a block crop stays at the floor the
// model declares.
void TestModelMemoryEstimator::aPageFillsTheImageBudgetAndACropDoesNot()
{
    const QList<ModelProfiles::Profile> profiles = ModelProfiles::instance();
    const ModelProfiles::Profile *glm = ModelProfiles::find(profiles, QStringLiteral("glm-ocr"));
    QVERIFY(glm);
    // a page fills the model's image budget — glm-ocr ships no layout role, so
    // the budget is the one the profile declares
    QCOMPARE(ModelProfiles::visionTokensFor(*glm, QStringLiteral("layout")), 6144);
    // the block role: a crop is scaled up to the floor the model declares
    QCOMPARE(ModelProfiles::visionTokensFor(*glm, QStringLiteral("blockRecognition")), 456);

    // a model that declares no budget: a page falls back to the model's own
    const ModelProfiles::Profile *mineru = ModelProfiles::find(profiles, QStringLiteral("mineru"));
    QVERIFY(mineru);
    QCOMPARE(ModelProfiles::visionTokensFor(*mineru, QStringLiteral("layout")), mineru->memory.visionTokens);
    QVERIFY(ModelProfiles::visionTokensFor(*mineru, QStringLiteral("blockRecognition")) < mineru->memory.visionTokens);
}

// The launch profile is what the server runs with: the context, the ubatch
// and the cache types.
void TestModelMemoryEstimator::theLaunchProfileSetsTheBudget()
{
    LaunchProfile launch;
    LaunchParameter ctx;
    ctx.name = QStringLiteral("ctx-size");
    ctx.kind = LaunchValueKind::Number;
    ctx.value = 16384.0;
    LaunchParameter ubatch;
    ubatch.name = QStringLiteral("ubatch-size");
    ubatch.kind = LaunchValueKind::Number;
    ubatch.value = 6144.0;
    LaunchParameter cache;
    cache.name = QStringLiteral("cache-type-k");
    cache.kind = LaunchValueKind::Text;
    cache.value = QStringLiteral("f32");
    launch.parameters = {ctx, ubatch, cache};

    const ModelMemoryBudget budget = budgetFromLaunch(launch, 1024);
    QCOMPARE(budget.ctxSize, 16384);
    QCOMPARE(budget.ubatchTokens, 6144);
    QCOMPARE(budget.bytesPerValue, 4);
    QCOMPARE(budget.visionTokens, 1024);

    // a profile that says nothing gets llama.cpp's own defaults
    const ModelMemoryBudget plain = budgetFromLaunch(LaunchProfile(), 0);
    QCOMPARE(plain.ctxSize, 8192);
    QCOMPARE(plain.ubatchTokens, 512);
    QCOMPARE(plain.bytesPerValue, 2);
}

void TestModelMemoryEstimator::systemRamIsPositive()
{
    QVERIFY(systemPhysicalRamBytes() > 0);
}

QTEST_MAIN(TestModelMemoryEstimator)
#include "test_model_memory_estimator.moc"
