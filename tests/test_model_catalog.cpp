#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTest>

#include "runtime/ModelCatalog.h"

using namespace llocr;

namespace {

QJsonObject makeFile(const QString &path, const QString &type, double size,
                     const QString &lfsOid = QString())
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), type);
    o.insert(QStringLiteral("oid"), path);
    o.insert(QStringLiteral("path"), path);
    o.insert(QStringLiteral("size"), size);
    if (!lfsOid.isEmpty()) {
        QJsonObject lfs;
        lfs.insert(QStringLiteral("oid"), lfsOid);
        lfs.insert(QStringLiteral("size"), size);
        o.insert(QStringLiteral("lfs"), lfs);
    }
    return o;
}

// A tree fixture resembling a real multi-file + mmproj GGUF repo.
QJsonArray buildTree()
{
    QJsonArray arr;
    arr.append(makeFile(QStringLiteral(".gitattributes"), QStringLiteral("file"), 100));
    arr.append(makeFile(QStringLiteral("model-Q4_K_M-00001-of-00003.gguf"),
                        QStringLiteral("lfs"), 1000, QString(64, QLatin1Char('a'))));
    arr.append(makeFile(QStringLiteral("model-Q4_K_M-00002-of-00003.gguf"),
                        QStringLiteral("lfs"), 1000, QString(64, QLatin1Char('b'))));
    arr.append(makeFile(QStringLiteral("model-Q4_K_M-00003-of-00003.gguf"),
                        QStringLiteral("lfs"), 1000, QString(64, QLatin1Char('c'))));
    arr.append(makeFile(QStringLiteral("mmproj-F16.gguf"), QStringLiteral("lfs"),
                        500, QString(64, QLatin1Char('d'))));
    // Directory entry should be skipped by callers but parsed.
    arr.append(makeFile(QStringLiteral("sub"), QStringLiteral("directory"), 0));
    return arr;
}

}  // namespace

class TestModelCatalog : public QObject
{
    Q_OBJECT

private slots:
    void parsesTree();
    void detectsRoles();
    void splitsMultiPart();
    void extractsQuantization();
    void encodesPaths();
    void resolvesUrl();
    void parsesPaginationLink();
    void parsesSearch();
    void filtersGguf();
};

void TestModelCatalog::parsesTree()
{
    QString err;
    const QList<HfFile> files = ModelCatalog::parseTreeJson(buildTree(), err);
    QVERIFY(err.isEmpty());
    QCOMPARE(files.size(), 6);  // includes the directory entry
    bool foundFirst = false;
    bool foundMmproj = false;
    for (const HfFile &f : files) {
        if (f.name == QStringLiteral("model-Q4_K_M-00001-of-00003.gguf")) {
            foundFirst = true;
            QVERIFY(f.isLfs);
            QCOMPARE(f.lfsOid, QString(64, QLatin1Char('a')));
            QCOMPARE(f.size, qint64(1000));
        }
        if (f.name == QStringLiteral("mmproj-F16.gguf"))
            foundMmproj = true;
        if (f.name == QStringLiteral("sub"))
            QVERIFY(f.isDir);
        if (f.name == QStringLiteral(".gitattributes"))
            QVERIFY(!f.isLfs);
    }
    QVERIFY(foundFirst);
    QVERIFY(foundMmproj);
}

void TestModelCatalog::detectsRoles()
{
    QCOMPARE(ModelCatalog::fileKind(QStringLiteral("model-Q4_K_M.gguf")),
             ModelFileKind::Model);
    QCOMPARE(ModelCatalog::fileKind(QStringLiteral("mmproj-F16.gguf")),
             ModelFileKind::Vision);
    QCOMPARE(ModelCatalog::fileKind(QStringLiteral("readme.md")),
             ModelFileKind::NotModel);
    QCOMPARE(ModelCatalog::fileKind(QStringLiteral("mmproj.bin")),
             ModelFileKind::NotModel);
}

void TestModelCatalog::splitsMultiPart()
{
    QString base;
    int idx = 0, cnt = 0;
    QVERIFY(ModelCatalog::splitMultiPart(
        QStringLiteral("model-Q4_K_M-00001-of-00005.gguf"), &base, &idx, &cnt));
    QVERIFY(base.contains(QStringLiteral("model")));
    QCOMPARE(idx, 1);
    QCOMPARE(cnt, 5);
    QVERIFY(ModelCatalog::isMultiPart(QStringLiteral("model-00001-of-00005.gguf")));
    QVERIFY(!ModelCatalog::isMultiPart(QStringLiteral("model-Q4_K_M.gguf")));
}

void TestModelCatalog::extractsQuantization()
{
    QCOMPARE(ModelCatalog::quantizationFromName(QStringLiteral("model-Q4_K_M.gguf")),
             QStringLiteral("Q4_K_M"));
    QCOMPARE(ModelCatalog::quantizationFromName(QStringLiteral("unsloth.bin.F16.gguf")),
             QStringLiteral("F16"));
    QCOMPARE(ModelCatalog::quantizationFromName(QStringLiteral("model-x.Q8_0.gguf")),
             QStringLiteral("Q8_0"));
    // Non-gguf names never extract a quant token.
    QVERIFY(ModelCatalog::quantizationFromName(QStringLiteral("readme.txt")).isEmpty());
}

void TestModelCatalog::encodesPaths()
{
    // Non-ASCII and reserved chars survive URL encoding; '/' is preserved.
    const QString encoded = ModelCatalog::encodePath(QStringLiteral("org/модель Q4.gguf"));
    QVERIFY(encoded.contains(QStringLiteral("%D0%BC")));  // ём hex
    QVERIFY(encoded.contains(QLatin1Char('/')));
    QVERIFY(!encoded.contains(QLatin1Char(' ')));
}

void TestModelCatalog::resolvesUrl()
{
    const QUrl url = ModelCatalog::resolveUrl(
        QStringLiteral("org/repo"), QStringLiteral("abc123"),
        QStringLiteral("dir/file space-Q4_K_M.gguf"));
    QCOMPARE(url.scheme(), QStringLiteral("https"));
    QCOMPARE(url.host(), QStringLiteral("huggingface.co"));
    QVERIFY(url.path().contains(QStringLiteral("/org/repo/resolve/abc123/")));
    // The space must be percent-encoded at the wire level.
    QVERIFY(!url.toEncoded().contains(' '));
    QVERIFY(url.toEncoded().contains("%20"));
}

void TestModelCatalog::parsesPaginationLink()
{
    const QByteArray header =
        "<https://huggingface.co/api/models/org/r/tree/main?recursive=true&page=2>; "
        "rel=\"next\"";
    const QUrl next = ModelCatalog::nextPageUrl(header);
    QVERIFY(next.isValid());
    QVERIFY(next.toString().contains(QStringLiteral("page=2")));
    // No 'next' rel → empty.
    QVERIFY(ModelCatalog::nextPageUrl(
                QByteArray("<https://x/>; rel=\"prev\""))
                .isEmpty());
}

void TestModelCatalog::parsesSearch()
{
    QJsonArray arr;
    QJsonObject o;
    o.insert(QStringLiteral("id"), QStringLiteral("org/repo"));
    o.insert(QStringLiteral("title"), QStringLiteral("Repo"));
    o.insert(QStringLiteral("downloads"), 123456);
    o.insert(QStringLiteral("likes"), 7);
    o.insert(QStringLiteral("license"), QStringLiteral("apache-2.0"));
    o.insert(QStringLiteral("gated"), true);
    QJsonArray tags{QStringLiteral("gguf"), QStringLiteral("vision")};
    o.insert(QStringLiteral("tags"), tags);
    arr.append(o);

    const QList<HfModelSummary> results = ModelCatalog::parseSearchJson(arr);
    QCOMPARE(results.size(), 1);
    QCOMPARE(results.at(0).id, QStringLiteral("org/repo"));
    QCOMPARE(results.at(0).downloads, qint64(123456));
    QVERIFY(results.at(0).gated);
    QVERIFY(results.at(0).tags.contains(QStringLiteral("vision")));
}

void TestModelCatalog::filtersGguf()
{
    const QStringList in = {
        QStringLiteral("a.gguf"), QStringLiteral("b.bin"),
        QStringLiteral("c.GGUF"), QStringLiteral("d.txt")};
    const QStringList out = ModelCatalog::allGguf(in);
    QCOMPARE(out.size(), 2);
}

QTEST_MAIN(TestModelCatalog)
#include "test_model_catalog.moc"