#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "runtime/ReleaseCatalog.h"

using namespace llocr;

namespace {

// A digest is just 64 repeated hex characters; the exact value is irrelevant,
// only the match between the body table and the asset file name matters.
QString digestOf(char hex)
{
    return QString(64, QChar(hex));
}

QJsonObject makeAsset(const QString &name, const QString &url, qint64 size)
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("browser_download_url"), url);
    o.insert(QStringLiteral("size"), static_cast<double>(size));
    return o;
}

// Builds the GitHub Releases "items" array used across the parsing tests. The
// fixture is assembled with QJsonObject/QJsonArray rather than a raw literal so
// it is valid JSON by construction (and stays friendly to moc).
//
// The names mirror the real llama.cpp releases (verified against the GitHub
// Releases API): Windows ships `.zip` with a backend token, macOS ships a
// generic (no backend token) `.tar.gz`, and Ubuntu ships `.tar.gz` either
// generic or with a backend token.
QJsonArray buildReleases()
{
    const QString winCuda = QStringLiteral("llama-b10594-bin-win-cuda-cu12-x64.zip");
    const QString winCpu = QStringLiteral("llama-b10594-bin-win-cpu-x64.zip");
    const QString macTar = QStringLiteral("llama-b10594-bin-macos-arm64.tar.gz");
    const QString linuxVulkan = QStringLiteral("llama-b10594-bin-ubuntu-vulkan-x64.tar.gz");
    const QString linuxGeneric = QStringLiteral("llama-b10594-bin-ubuntu-x64.tar.gz");
    const QString mystery = QStringLiteral("llama-b10594-mystery-file.txt");

    QStringList bodyLines;
    bodyLines << QStringLiteral("## sha256")
              << QStringLiteral("sha256: %1  %2").arg(digestOf('a'), winCuda)
              << QStringLiteral("sha256: %1  %2").arg(digestOf('b'), winCpu)
              << QStringLiteral("sha256: %1  %2").arg(digestOf('c'), macTar)
              << QStringLiteral("sha256: %1  %2").arg(digestOf('d'), linuxVulkan)
              << QStringLiteral("sha256: %1  %2").arg(digestOf('e'), linuxGeneric);

    QJsonObject r1;
    r1.insert(QStringLiteral("tag_name"), QStringLiteral("b10594"));
    r1.insert(QStringLiteral("name"), QStringLiteral("llama.cpp b10594"));
    r1.insert(QStringLiteral("published_at"), QStringLiteral("2025-06-01T00:00:00Z"));
    r1.insert(QStringLiteral("body"), bodyLines.join(QLatin1Char('\n')));
    QJsonArray a1;
    a1.append(makeAsset(winCuda, QStringLiteral("https://example.com/win-cuda.zip"), 1000));
    a1.append(makeAsset(winCpu, QStringLiteral("https://example.com/win-cpu.zip"), 900));
    a1.append(makeAsset(macTar, QStringLiteral("https://example.com/macos.tar.gz"), 800));
    a1.append(makeAsset(linuxVulkan, QStringLiteral("https://example.com/linux-vulkan.tar.gz"), 850));
    a1.append(makeAsset(linuxGeneric, QStringLiteral("https://example.com/linux-generic.tar.gz"), 700));
    a1.append(makeAsset(mystery, QStringLiteral("https://example.com/mystery.txt"), 42));
    r1.insert(QStringLiteral("assets"), a1);

    QJsonObject r2;
    r2.insert(QStringLiteral("tag_name"), QStringLiteral("b10589"));
    r2.insert(QStringLiteral("name"), QStringLiteral("llama.cpp b10589"));
    r2.insert(QStringLiteral("published_at"), QStringLiteral("2025-05-20T00:00:00Z"));
    r2.insert(QStringLiteral("body"), QStringLiteral("No sha256 table in this release."));
    QJsonArray a2;
    a2.append(makeAsset(QStringLiteral("llama-b10589-bin-macos-x64.tar.gz"),
                        QStringLiteral("https://example.com/macos-old.tar.gz"), 777));
    r2.insert(QStringLiteral("assets"), a2);

    QJsonArray releases;
    releases.append(r1);
    releases.append(r2);
    return releases;
}

QByteArray releasesJson()
{
    return QJsonDocument(buildReleases()).toJson(QJsonDocument::Compact);
}

}  // namespace

class TestReleaseCatalog : public QObject
{
    Q_OBJECT

private slots:
    void parsesKnownLayout();
    void extractsShaFromBody();
    void picksPlatformAsset();
    void pickUnknownReturnsEmpty();
    void cudartAssetIsFlagged();
    void cacheMissingIsNotFresh();
    void cacheIsFreshWithinTtl();
    void buildFromTag();
    void detectPlatformMatchesHost();
};

void TestReleaseCatalog::parsesKnownLayout()
{
    QString err;
    const QList<ReleaseInfo> releases = ReleaseCatalog::parseReleasesJson(buildReleases(), err);
    QVERIFY(err.isEmpty());
    QCOMPARE(releases.size(), 2);
    QCOMPARE(releases.at(0).tagName, QStringLiteral("b10594"));
    QCOMPARE(releases.at(0).build, qint64(10594));
    QCOMPARE(releases.at(0).assets.size(), 6);
}

void TestReleaseCatalog::extractsShaFromBody()
{
    QString err;
    const QList<ReleaseInfo> releases = ReleaseCatalog::parseReleasesJson(buildReleases(), err);
    const ReleaseAsset *winCuda = nullptr;
    for (const ReleaseAsset &a : releases.at(0).assets) {
        if (a.fileName == QStringLiteral("llama-b10594-bin-win-cuda-cu12-x64.zip"))
            winCuda = &a;
    }
    QVERIFY(winCuda);
    QCOMPARE(winCuda->sha256, digestOf('a'));
    QVERIFY(releases.at(1).assets.at(0).sha256.isEmpty());
}

void TestReleaseCatalog::picksPlatformAsset()
{
    QString err;
    const QList<ReleaseInfo> releases = ReleaseCatalog::parseReleasesJson(buildReleases(), err);
    const ReleaseInfo &r = releases.at(0);
    QCOMPARE(r.pickAsset(QStringLiteral("win"), QStringLiteral("x64"),
                         QStringLiteral("cuda-cu12")).fileName,
             QStringLiteral("llama-b10594-bin-win-cuda-cu12-x64.zip"));
    // macOS ships a single universal build (no backend token): both the
    // recommended "metal" and the fallback "cpu" must select it.
    QCOMPARE(r.pickAsset(QStringLiteral("macos"), QStringLiteral("arm64"),
                         QStringLiteral("metal")).fileName,
             QStringLiteral("llama-b10594-bin-macos-arm64.tar.gz"));
    QCOMPARE(r.pickAsset(QStringLiteral("macos"), QStringLiteral("arm64"),
                         QStringLiteral("cpu")).fileName,
             QStringLiteral("llama-b10594-bin-macos-arm64.tar.gz"));
    QCOMPARE(r.pickAsset(QStringLiteral("linux"), QStringLiteral("x64"),
                         QStringLiteral("vulkan")).fileName,
             QStringLiteral("llama-b10594-bin-ubuntu-vulkan-x64.tar.gz"));
    // Generic Ubuntu build serves the plain "cpu" request.
    QCOMPARE(r.pickAsset(QStringLiteral("linux"), QStringLiteral("x64"),
                         QStringLiteral("cpu")).fileName,
             QStringLiteral("llama-b10594-bin-ubuntu-x64.tar.gz"));
}

void TestReleaseCatalog::pickUnknownReturnsEmpty()
{
    QString err;
    const QList<ReleaseInfo> releases = ReleaseCatalog::parseReleasesJson(buildReleases(), err);
    QVERIFY(releases.at(0).pickAsset(QStringLiteral("unknown"), QStringLiteral("x64"),
                                     QStringLiteral("cpu")).fileName.isEmpty());
    bool foundMystery = false;
    for (const ReleaseAsset &a : releases.at(0).assets)
        if (a.fileName == QStringLiteral("llama-b10594-mystery-file.txt"))
            foundMystery = true;
    QVERIFY(foundMystery);
}

void TestReleaseCatalog::cudartAssetIsFlagged()
{
    // Two real-world shapes: legacy `...-cu124.zip` and the current
    // `...-12.4-x64.zip` / `...-13.3-arm64.zip` naming.
    QJsonArray assets;
    assets.append(makeAsset(QStringLiteral("cudart-llama-bin-win-cuda-cu124.zip"),
                            QStringLiteral("https://example.com/cu.zip"), 1100));
    assets.append(makeAsset(QStringLiteral("cudart-llama-bin-win-cuda-12.4-x64.zip"),
                            QStringLiteral("https://example.com/cu124.zip"), 1200));
    assets.append(makeAsset(QStringLiteral("cudart-llama-bin-win-cuda-13.3-arm64.zip"),
                            QStringLiteral("https://example.com/cu133.zip"), 1300));
    QJsonObject release;
    release.insert(QStringLiteral("tag_name"), QStringLiteral("b10594"));
    release.insert(QStringLiteral("assets"), assets);
    QJsonArray items;
    items.append(release);

    QString err;
    const QList<ReleaseInfo> releases = ReleaseCatalog::parseReleasesJson(items, err);
    QCOMPARE(releases.at(0).assets.size(), 3);
    for (const ReleaseAsset &a : releases.at(0).assets)
        QVERIFY(a.cudart);
}

void TestReleaseCatalog::detectPlatformMatchesHost()
{
    // Regression: QSysInfo::kernelType() is "darwin" on macOS, which contains
    // the substring "win" — a plain contains("win") check fell through to the
    // Windows branch and made the runtime installer download a Windows zip on
    // a Mac (then "no llama-server binary found in the release archive").
    const PlatformInfo info = ReleaseCatalog::detectPlatform();
#ifdef Q_OS_MACOS
    QCOMPARE(info.os, PlatformOs::macOS);
    QCOMPARE(info.osTag, QStringLiteral("macos"));
    QVERIFY(!info.arch.isEmpty());
#elif defined(Q_OS_WIN)
    QCOMPARE(info.os, PlatformOs::Windows);
    QCOMPARE(info.osTag, QStringLiteral("win"));
#else
    QCOMPARE(info.os, PlatformOs::Linux);
    QCOMPARE(info.osTag, QStringLiteral("linux"));
#endif
}

void TestReleaseCatalog::cacheMissingIsNotFresh()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDateTime when;
    bool fresh = false;
    QString err;
    qint64 cachedBuild = -1;
    const QList<ReleaseInfo> list =
        ReleaseCatalog::loadCache(dir.path(), when, cachedBuild, fresh, err);
    QVERIFY(list.isEmpty());
    QVERIFY(!fresh);
    QVERIFY(!err.isEmpty());
}

void TestReleaseCatalog::cacheIsFreshWithinTtl()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(QDir(dir.path()).filePath(QStringLiteral("releases.json")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(releasesJson());
    f.close();

    QDateTime when;
    bool fresh = false;
    QString err;
    qint64 cachedBuild = -1;
    const QList<ReleaseInfo> list =
        ReleaseCatalog::loadCache(dir.path(), when, cachedBuild, fresh, err);
    QVERIFY(fresh);
    QCOMPARE(list.size(), 2);
    QCOMPARE(cachedBuild, qint64(10594));
    QVERIFY(!when.isNull());
}

void TestReleaseCatalog::buildFromTag()
{
    QCOMPARE(extractBuildNumberFromTag(QStringLiteral("b10594")), 10594);
    QCOMPARE(extractBuildNumberFromTag(QStringLiteral("10594")), 10594);
    QCOMPARE(extractBuildNumberFromTag(QStringLiteral("v10594")), 10594);
    QCOMPARE(extractBuildNumberFromTag(QStringLiteral("latest")), -1);
}

QTEST_MAIN(TestReleaseCatalog)
#include "test_release_catalog.moc"