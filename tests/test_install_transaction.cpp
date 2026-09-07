#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QTemporaryDir>
#include <QtCore>
#include <QtTest>

#include <functional>

#include <zlib.h>

#include "runtime/InstallTransaction.h"
#include "runtime/ReleaseAsset.h"
#include "runtime/RuntimePaths.h"

using namespace llocr;

// Path to the mock_llama_server test double, injected by tests/CMakeLists.txt
// as a compile definition (LLOCR_MOCK_SERVER).
#ifndef LLOCR_MOCK_SERVER
#define LLOCR_MOCK_SERVER "mock_llama_server"
#endif

// --- tiny raw-ZIP writer (same layout as the ArchiveExtractor test) ------
// mirrors the extractor's local-header parse exactly (name length read 4 bytes
// later than a textbook header) so data offsets land on the real payload.

struct ZipEntry {
    QString name;
    QByteArray content;          // uncompressed bytes (CRC source)
    quint16 method = 0;
    QByteArray compData;         // pre-compressed bytes; empty => stored content
    quint32 crc = 0;             // 0 => computed from content
    quint32 modeAttr = 0;        // upper 16 bits => (mode << 16)
};

static void pushU16(QByteArray &out, quint16 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
}

static void pushU32(QByteArray &out, quint32 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 24) & 0xFF));
}

static quint32 crc32(const QByteArray &data)
{
    return ::crc32(0L, reinterpret_cast<const Bytef *>(data.constData()),
                   static_cast<uInt>(data.size()));
}

static QByteArray buildZip(const QList<ZipEntry> &entries)
{
    QByteArray out;
    QByteArray central;
    quint32 running = 0;   // byte offset of the next local header

    for (const ZipEntry &e : entries) {
        const QByteArray nameBytes = e.name.toUtf8();
        const QByteArray payload = e.compData.isEmpty() ? e.content : e.compData;
        const quint32 compSize = quint32(payload.size());
        const quint32 uncompSize = quint32(e.content.size());
        const quint32 crc = e.crc != 0 ? e.crc : crc32(e.content);
        const quint32 thisOff = running;

        // Local file header (standard 30-byte fixed layout).
        pushU32(out, 0x04034b50u);
        pushU16(out, 20);
        pushU16(out, 0);
        pushU16(out, e.method);
        pushU16(out, 0);
        pushU16(out, 0);
        pushU32(out, crc);
        pushU32(out, compSize);
        pushU32(out, uncompSize);
        pushU16(out, quint16(nameBytes.size()));
        pushU16(out, 0);
        out += nameBytes;
        out += payload;
        running += quint32(30 + nameBytes.size() + payload.size());

        // Central directory entry.
        QByteArray c;
        pushU32(c, 0x02014b50u);
        pushU16(c, 0x0300u);         // made by Unix
        pushU16(c, 20);
        pushU16(c, 0);
        pushU16(c, e.method);
        pushU16(c, 0);
        pushU16(c, 0);
        pushU32(c, crc);
        pushU32(c, compSize);
        pushU32(c, uncompSize);
        pushU16(c, quint16(nameBytes.size()));
        pushU16(c, 0);
        pushU16(c, 0);
        pushU16(c, 0);
        pushU16(c, 0);
        pushU32(c, e.modeAttr);
        pushU32(c, thisOff);
        c += nameBytes;
        central += c;
    }

    // Central directory entries, then the end-of-central-directory record.
    out += central;

    // End-of-central-directory (standard 22-byte layout).
    pushU32(out, 0x06054b50u);
    pushU16(out, 0);
    pushU16(out, 0);
    pushU16(out, quint16(entries.size()));
    pushU16(out, quint16(entries.size()));
    pushU32(out, quint32(central.size()));
    pushU32(out, running);
    pushU16(out, 0);
    return out;
}

static QString writeArchive(const QString &dir, const QString &file,
                            const QByteArray &raw)
{
    const QString path = QDir(dir).filePath(file);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    f.write(raw);
    f.close();
    return path;
}

static QString archiveFor(const QString &dir, const QString &file,
                          const QList<ZipEntry> &entries)
{
    return writeArchive(dir, file, buildZip(entries));
}

// --- tiny ustar + gzip writer (mirrors the real llama.cpp macOS archives) ---

namespace {

QByteArray tarGzPayload(const QList<ZipEntry> &entries)
{
    QByteArray tar;
    QByteArray h(512, char(0));
    for (const ZipEntry &e : entries) {
        h.fill(char(0));
        char *raw = h.data();
        const QByteArray name = e.name.toUtf8();
        ::memcpy(raw, name.constData(), ::size_t(name.size() < 100 ? name.size() : 100));
        const QString mode = QStringLiteral("%1").arg(e.modeAttr >> 16, 7, 8, QLatin1Char('0'));
        ::memcpy(raw + 100, mode.toLatin1().constData(), 7);
        if (!e.content.isEmpty()) {
            const QString size = QStringLiteral("%1").arg(e.content.size(), 11, 8,
                                                          QLatin1Char('0'));
            ::memcpy(raw + 124, size.toLatin1().constData(), 11);
        }
        h[156] = char(e.content.isEmpty() ? '5' : '0');
        ::memcpy(raw + 257, "ustar\0", 6);
        tar += h;
        if (!e.content.isEmpty()) {
            tar += e.content;
            const qint64 pad = (512 - (e.content.size() % 512)) % 512;
            tar += QByteArray(int(pad), char(0));
        }
    }
    tar += QByteArray(1024, char(0));

    QByteArray out;
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(tar.constData()));
    stream.avail_in = static_cast<uInt>(tar.size());
    deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                 Z_DEFAULT_STRATEGY);
    out.resize(tar.size() + (tar.size() >> 4) + 128);
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    deflate(&stream, Z_FINISH);
    out.truncate(int(stream.total_out));
    deflateEnd(&stream);
    return out;
}

}  // namespace

static QString archiveTarGzFor(const QString &dir, const QString &file,
                               const QList<ZipEntry> &entries)
{
    return writeArchive(dir, file, tarGzPayload(entries));
}

// True when the staging dir has no sub-directories left (a failed or completed
// install must leave nothing behind there).
static bool stagingEmpty(const RuntimePaths &paths)
{
    return QDir(paths.stagingDir())
        .entryList(QDir::Dirs | QDir::NoDotAndDotDot)
        .isEmpty();
}

// True when no installed build directory exists under runtime/ (used to assert
// that a failed install left nothing behind).
static bool noInstalledBuild(const RuntimePaths &paths)
{
    for (const QString &name : QDir(paths.runtimeDir())
             .entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (name != QStringLiteral("staging"))
            return false;
    }
    return true;
}

class TestInstallTransaction : public QObject {
    Q_OBJECT

private slots:
    void installsSuccessfully();
    void installsFromTarGz();
    void sizeMismatchFails();
    void shaMismatchFails();
    void badArchiveFails();
    void duplicateEntryFails();
    void probeFailureFails();
    void cleanupUnused();
    void cleanupStaging();
};

void TestInstallTransaction::installsSuccessfully()
{
    // The mock llama-server binary is the payload; it must answer --version so
    // the probe inside InstallTransaction::start succeeds.
    const QString mock = QString::fromUtf8(LLOCR_MOCK_SERVER);
    QVERIFY2(QFile::exists(mock), qPrintable(mock));
    QFile mf(mock);
    QVERIFY2(mf.open(QIODevice::ReadOnly), "mock binary unreadable");
    const QByteArray serverBin = mf.readAll();
    mf.close();

    QList<ZipEntry> entries;
    ZipEntry server;
    server.name = QStringLiteral("llama-server");
    server.content = serverBin;
    server.modeAttr = 0o755u << 16;   // executable per the extractor
    server.crc = crc32(serverBin);
    entries << server;

    ZipEntry readme;
    readme.name = QStringLiteral("readme.txt");
    readme.content = QByteArray("InstallTransaction demo\n");
    entries << readme;

    QTemporaryDir dir;
    const QString zipPath = archiveFor(dir.path(), QStringLiteral("release.zip"), entries);
    QVERIFY2(!zipPath.isEmpty(), "failed to write archive");

    QTemporaryDir root;
    const RuntimePaths paths(QDir(root.path()).filePath(QStringLiteral("app")),
                             QDir(root.path()).filePath(QStringLiteral("models")));

    ReleaseAsset asset;
    asset.fileName = QStringLiteral("llama-b10594-bin-macos-arm64.zip");
    asset.os = QStringLiteral("macos");
    asset.backend = QStringLiteral("metal");
    asset.arch = QStringLiteral("arm64");
    asset.build = QStringLiteral("b10594");
    asset.size = QFileInfo(zipPath).size();
    asset.sha256 = QString();

    bool committed = false;
    QString committedTag;
    std::function<void(const InstallOutput &)> commit =
        [&](const InstallOutput &o) {
            committed = true;
            committedTag = o.tag;
        };

    const InstallOutput out = InstallTransaction::start(zipPath, asset, paths, commit);

    QVERIFY2(out.ok, qPrintable(out.error));
    QCOMPARE(out.build, QStringLiteral("b10594"));

    const QString expectedTag = QStringLiteral("llama.cpp-b10594-metal-macos-arm64");
    QCOMPARE(out.tag, expectedTag);
    QVERIFY(QFile::exists(out.serverPath));
    QVERIFY(QDir(paths.installDir(out.tag)).exists());
    QVERIFY(committed);
    QCOMPARE(committedTag, expectedTag);
    // The staging/<uuid> tree was atomically renamed into place and is gone.
    QVERIFY(stagingEmpty(paths));
}

void TestInstallTransaction::installsFromTarGz()
{
    // macOS ships a `.tar.gz` with the binary under a top-level build folder;
    // the transaction must locate llama-server inside it and commit.
    const QString mock = QString::fromUtf8(LLOCR_MOCK_SERVER);
    QVERIFY2(QFile::exists(mock), qPrintable(mock));
    QFile mf(mock);
    QVERIFY2(mf.open(QIODevice::ReadOnly), "mock binary unreadable");
    const QByteArray serverBin = mf.readAll();
    mf.close();

    QList<ZipEntry> entries;
    ZipEntry server;
    server.name = QStringLiteral("llama-b10825/llama-server");
    server.content = serverBin;
    server.modeAttr = 0o755u << 16;
    server.crc = crc32(serverBin);
    entries << server;

    ZipEntry readme;
    readme.name = QStringLiteral("llama-b10825/readme.txt");
    readme.content = QByteArray("llama.cpp b10825\n");
    entries << readme;

    QTemporaryDir dir;
    const QString archivePath =
        archiveTarGzFor(dir.path(), QStringLiteral("llama-b10825-bin-macos-arm64.tar.gz"), entries);
    QVERIFY2(!archivePath.isEmpty(), "failed to write archive");

    QTemporaryDir root;
    const RuntimePaths paths(QDir(root.path()).filePath(QStringLiteral("app")),
                             QDir(root.path()).filePath(QStringLiteral("models")));

    ReleaseAsset asset;
    asset.fileName = QStringLiteral("llama-b10825-bin-macos-arm64.tar.gz");
    asset.os = QStringLiteral("macos");
    asset.arch = QStringLiteral("arm64");
    asset.build = QStringLiteral("b10825");
    asset.size = QFileInfo(archivePath).size();
    asset.sha256 = QString();

    bool committed = false;
    QString committedTag;
    std::function<void(const InstallOutput &)> commit =
        [&](const InstallOutput &o) {
            committed = true;
            committedTag = o.tag;
        };

    const InstallOutput out = InstallTransaction::start(archivePath, asset, paths, commit);

    QVERIFY2(out.ok, qPrintable(out.error));
    // The probe reports the mock's own build (b10594), which wins over the
    // asset-derived build number.
    QCOMPARE(out.build, QStringLiteral("b10594"));
    QVERIFY(QFile::exists(out.serverPath));
    QVERIFY(QDir(paths.installDir(out.tag)).exists());
    QCOMPARE(out.tag, QStringLiteral("llama.cpp-b10594-cpu-macos-arm64"));
    QVERIFY(committed);
    QVERIFY(stagingEmpty(paths));
}

void TestInstallTransaction::sizeMismatchFails()
{
    QTemporaryDir dir;
    QList<ZipEntry> entries;
    ZipEntry e;
    e.name = QStringLiteral("llama-server");
    e.content = QByteArray("tiny");
    e.modeAttr = 0o755u << 16;
    entries << e;
    const QString zipPath = archiveFor(dir.path(), QStringLiteral("rel.zip"), entries);

    QTemporaryDir root;
    const RuntimePaths paths(QDir(root.path()).filePath(QStringLiteral("app")),
                             QDir(root.path()).filePath(QStringLiteral("models")));

    ReleaseAsset asset;
    asset.fileName = QStringLiteral("llama-release.zip");
    asset.os = QStringLiteral("macos");
    asset.backend = QStringLiteral("cpu");
    asset.arch = QStringLiteral("arm64");
    asset.size = QFileInfo(zipPath).size() + 1;   // wrong size
    asset.sha256 = QString();

    bool committed = false;
    const auto commit = [&](const InstallOutput &) { committed = true; };

    const InstallOutput out = InstallTransaction::start(zipPath, asset, paths, commit);

    QVERIFY(!out.ok);
    QVERIFY(!committed);
    QVERIFY(noInstalledBuild(paths));
    QVERIFY(stagingEmpty(paths));
}

void TestInstallTransaction::shaMismatchFails()
{
    QTemporaryDir dir;
    QList<ZipEntry> entries;
    ZipEntry e;
    e.name = QStringLiteral("llama-server");
    e.content = QByteArray("tiny");
    e.modeAttr = 0o755u << 16;
    entries << e;
    const QString zipPath = archiveFor(dir.path(), QStringLiteral("rel.zip"), entries);

    QTemporaryDir root;
    const RuntimePaths paths(QDir(root.path()).filePath(QStringLiteral("app")),
                             QDir(root.path()).filePath(QStringLiteral("models")));

    ReleaseAsset asset;
    asset.fileName = QStringLiteral("llama-release.zip");
    asset.os = QStringLiteral("macos");
    asset.backend = QStringLiteral("cpu");
    asset.arch = QStringLiteral("arm64");
    asset.size = QFileInfo(zipPath).size();
    asset.sha256 = QString(64, QLatin1Char('f'));   // wrong digest

    bool committed = false;
    const auto commit = [&](const InstallOutput &) { committed = true; };

    const InstallOutput out = InstallTransaction::start(zipPath, asset, paths, commit);

    QVERIFY(!out.ok);
    QVERIFY(!committed);
    QVERIFY(noInstalledBuild(paths));
    QVERIFY(stagingEmpty(paths));
}

void TestInstallTransaction::badArchiveFails()
{
    QTemporaryDir dir;
    const QByteArray junk("this is not a zip archive at all");
    const QString zipPath = writeArchive(dir.path(), QStringLiteral("junk.zip"), junk);

    QTemporaryDir root;
    const RuntimePaths paths(QDir(root.path()).filePath(QStringLiteral("app")),
                             QDir(root.path()).filePath(QStringLiteral("models")));

    ReleaseAsset asset;
    asset.fileName = QStringLiteral("llama-release.zip");
    asset.os = QStringLiteral("macos");
    asset.backend = QStringLiteral("cpu");
    asset.arch = QStringLiteral("arm64");
    asset.size = QFileInfo(zipPath).size();
    asset.sha256 = QString();

    bool committed = false;
    const auto commit = [&](const InstallOutput &) { committed = true; };

    const InstallOutput out = InstallTransaction::start(zipPath, asset, paths, commit);

    QVERIFY(!out.ok);
    QVERIFY(!committed);
    QVERIFY(noInstalledBuild(paths));
    QVERIFY(stagingEmpty(paths));
}

void TestInstallTransaction::duplicateEntryFails()
{
    // Extraction-stage failure inside the transaction: a zip whose entries
    // collide (the hardened ArchiveExtractor rejects duplicate paths). The
    // transaction must fail before rename/commit and leave nothing behind.
    QTemporaryDir dir;
    QList<ZipEntry> entries;
    ZipEntry a;
    a.name = QStringLiteral("llama-server");
    a.content = QByteArray("first");
    a.modeAttr = 0o755u << 16;
    entries << a;
    ZipEntry b;
    b.name = QStringLiteral("llama-server");
    b.content = QByteArray("second");
    b.modeAttr = 0o755u << 16;
    entries << b;
    const QString zipPath = archiveFor(dir.path(), QStringLiteral("dup.zip"), entries);
    QVERIFY(!zipPath.isEmpty());

    QTemporaryDir root;
    const RuntimePaths paths(QDir(root.path()).filePath(QStringLiteral("app")),
                             QDir(root.path()).filePath(QStringLiteral("models")));

    ReleaseAsset asset;
    asset.fileName = QStringLiteral("llama-release.zip");
    asset.os = QStringLiteral("macos");
    asset.backend = QStringLiteral("cpu");
    asset.arch = QStringLiteral("arm64");
    asset.size = QFileInfo(zipPath).size();
    asset.sha256 = QString();

    bool committed = false;
    const auto commit = [&](const InstallOutput &) { committed = true; };

    const InstallOutput out = InstallTransaction::start(zipPath, asset, paths, commit);

    QVERIFY(!out.ok);
    QVERIFY(!committed);
    QVERIFY(noInstalledBuild(paths));
    QVERIFY(stagingEmpty(paths));
}

void TestInstallTransaction::probeFailureFails()
{
    // Extraction succeeds, but the located binary is not a runnable
    // llama-server (it does not answer --version), so the probe fails and the
    // transaction must roll back with no leftovers.
    QTemporaryDir dir;
    QList<ZipEntry> entries;
    ZipEntry server;
    server.name = QStringLiteral("llama-server");
    server.content = QByteArray("this is not an executable llama-server\n");
    server.modeAttr = 0o755u << 16;
    entries << server;
    const QString zipPath = archiveFor(dir.path(), QStringLiteral("probe.zip"), entries);
    QVERIFY(!zipPath.isEmpty());

    QTemporaryDir root;
    const RuntimePaths paths(QDir(root.path()).filePath(QStringLiteral("app")),
                             QDir(root.path()).filePath(QStringLiteral("models")));

    ReleaseAsset asset;
    asset.fileName = QStringLiteral("llama-release.zip");
    asset.os = QStringLiteral("macos");
    asset.backend = QStringLiteral("cpu");
    asset.arch = QStringLiteral("arm64");
    asset.size = QFileInfo(zipPath).size();
    asset.sha256 = QString();

    bool committed = false;
    const auto commit = [&](const InstallOutput &) { committed = true; };

    const InstallOutput out = InstallTransaction::start(zipPath, asset, paths, commit);

    QVERIFY(!out.ok);
    QVERIFY(!committed);
    QVERIFY(noInstalledBuild(paths));
    QVERIFY(stagingEmpty(paths));
}

void TestInstallTransaction::cleanupUnused()
{
    QTemporaryDir root;
    const RuntimePaths paths(QDir(root.path()).filePath(QStringLiteral("app")),
                             QDir(root.path()).filePath(QStringLiteral("models")));

    QVERIFY(QDir().mkpath(paths.installDir(QStringLiteral("Tag1"))));
    QVERIFY(QDir().mkpath(paths.installDir(QStringLiteral("Tag2"))));
    QVERIFY(QDir().mkpath(QDir(paths.runtimeDir()).filePath("staging/uuid")));

    const QString summary = InstallTransaction::cleanupUnusedBuilds(
        paths, QStringLiteral("Tag2"));

    QVERIFY(!QFile::exists(paths.installDir(QStringLiteral("Tag1"))));
    QVERIFY(QFile::exists(paths.installDir(QStringLiteral("Tag2"))));
    // The staging tree sits inside runtime/ and is swept as well.
    QVERIFY(!QFile::exists(QDir(paths.runtimeDir()).filePath("staging")));
    QVERIFY(summary.contains(QStringLiteral("Removed")));
}

void TestInstallTransaction::cleanupStaging()
{
    QTemporaryDir root;
    const RuntimePaths paths(QDir(root.path()).filePath(QStringLiteral("app")),
                             QDir(root.path()).filePath(QStringLiteral("models")));

    const QString uuidDir =
        QDir(paths.stagingDir()).filePath(QStringLiteral("deadbeef-uuid"));
    QVERIFY(QDir().mkpath(uuidDir));
    QFile stale(QDir(uuidDir).filePath(QStringLiteral("stale.bin")));
    QVERIFY(stale.open(QIODevice::WriteOnly | QIODevice::Truncate));
    stale.write("leftover payload");
    stale.close();

    InstallTransaction::cleanupStaging(paths);

    QVERIFY(!QFile::exists(uuidDir));
    QVERIFY(stagingEmpty(paths));
}

QTEST_MAIN(TestInstallTransaction)
#include "test_install_transaction.moc"
