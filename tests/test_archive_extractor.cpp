#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDevice>
#include <QTemporaryDir>
#include <QTest>

#include <cstdint>
#include <cstdlib>

#include <zlib.h>

#include "runtime/ArchiveExtractor.h"

using namespace llocr;

// --- little-endian byte writer --------------------------------------------
class ZWriter
{
public:
    ZWriter() = default;

    void u16(quint16 v)
    {
        m_b.append(char(v & 0xFF));
        m_b.append(char((v >> 8) & 0xFF));
    }
    void u32(quint32 v)
    {
        for (int i = 0; i < 4; ++i)
            m_b.append(char((v >> (8 * i)) & 0xFF));
    }
    void bytes(const QByteArray &b) { m_b.append(b); }
    QByteArray data() const { return m_b; }

private:
    QByteArray m_b;
};

uint32_t crc32Of(const QByteArray &data)
{
    return ::crc32(0L, reinterpret_cast<const Bytef *>(data.constData()),
                   static_cast<uInt>(data.size()));
}

// Raw DEFLATE from qCompress output. qCompress prepends a 4-byte big-endian
// uncompressed-length header and a 2-byte zlib header, and appends a 4-byte
// Adler-32 trailer; strip all of them to obtain the raw DEFLATE stream that
// ZIP stores (RFC 1951).
QByteArray rawDeflate(const QByteArray &b)
{
    const QByteArray z = qCompress(b);
    return z.mid(6, z.size() - 10);
}

struct ESpec {
    QString name;
    int method;
    QByteArray payload;   // uncompressed
    QByteArray comp;      // on-disk bytes
    quint32 externalAttr; // (mode << 16), e.g. 0755u<<16
    quint32 badCrc;       // nonzero => force an incorrect CRC for the entry
    bool isDir = false;
    // declared-uncomp size override for bomb/ratio tests (0 = none)
    qint64 declaredUncomp = 0;
};

// Builds an archive with the given entries. When `declaredUncompOverride` is
// nonzero it replaces the uncompSize field in both local + central headers
// (used to simulate size-limit / compression-ratio bombs).
QByteArray buildZip(const QList<ESpec> &specs, qint64 declaredUncompOverride = 0)
{
    ZWriter locals, central, out;
    quint32 localOffset = 0;
    for (const ESpec &e : specs) {
        qint64 uncomp = declaredUncompOverride;
        if (uncomp == 0)
            uncomp = qint64(e.payload.size());
        quint32 crc = e.badCrc ? e.badCrc : crc32Of(e.payload);
        // local header
        ZWriter l;
        l.u32(0x04034b50);
        l.u16(20);
        l.u16(0);
        l.u16(quint16(e.payload == e.comp ? 0 : 8));  // method
        l.u16(0);
        l.u16(0);
        l.u32(crc);
        l.u32(quint32(e.comp.size()));
        l.u32(quint32(uncomp));
        l.u16(quint16(e.name.toUtf8().size()));
        l.u16(0);
        l.bytes(e.name.toUtf8());
        l.bytes(e.comp);
        locals.bytes(l.data());

        // central dir
        ZWriter c;
        c.u32(0x02014b50);
        c.u16(0x0300);
        c.u16(20);
        c.u16(0);
        c.u16(quint16(e.payload == e.comp ? 0 : 8));
        c.u16(0);
        c.u16(0);
        c.u32(crc);
        c.u32(quint32(e.comp.size()));
        c.u32(quint32(uncomp));
        c.u16(quint16(e.name.toUtf8().size()));
        c.u16(0);
        c.u16(0);
        c.u16(0);
        c.u16(0);
        c.u32(e.externalAttr);
        c.u32(localOffset);
        c.bytes(e.name.toUtf8());
        central.bytes(c.data());

        localOffset += quint32(l.data().size());
    }

    out.bytes(locals.data());
    const quint32 centralOffset = quint32(locals.data().size());
    out.bytes(central.data());
    out.u32(0x06054b50);
    out.u16(0);
    out.u16(0);
    out.u16(quint16(specs.size()));
    out.u16(quint16(specs.size()));
    out.u32(quint32(central.data().size()));
    out.u32(centralOffset);
    out.u16(0);
    return out.data();
}

QString writeZip(const QTemporaryDir &dir, const QString &name, const QByteArray &data)
{
    const QString path = QDir(dir.path()).filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(data);
        f.close();
    }
    return path;
}

ESpec makeEntry(const QString &name, const QByteArray &payload,
                bool deflate = false, quint32 attr = 0, quint32 badCrc = 0)
{
    ESpec e;
    e.name = name;
    e.payload = payload;
    e.comp = deflate ? rawDeflate(payload) : payload;
    e.externalAttr = attr;
    e.badCrc = badCrc;
    return e;
}

// --- tiny ustar writer for tar.gz tests ------------------------------------
// Enough of the POSIX ustar format for the extractor tests: name, mode, size,
// typeflag, magic, ustar prefix, plus symlink entries. The produced archive is
// close to what llama.cpp publishes (BSD tar on the macOS runners).

struct TEntry {
    QString name;
    QByteArray content;    // payload for regular files, link target for symlinks
    QString linkTarget;    // non-empty => symlink (typeflag '2')
    bool isDir = false;
    quint32 mode = 0644;
};

QByteArray buildTar(const QList<TEntry> &entries)
{
    QByteArray out;
    for (const TEntry &e : entries) {
        QByteArray h(512, char(0));
        char *raw = h.data();
        const QByteArray name = e.name.toUtf8();
        ::memcpy(raw, name.constData(), ::size_t(name.size() < 100 ? name.size() : 100));
        const QString mode = QStringLiteral("%1").arg(e.mode, 7, 8, QLatin1Char('0'));
        ::memcpy(raw + 100, mode.toLatin1().constData(), 7);
        const QByteArray spaces(6, ' ');     // checksum placeholder
        ::memcpy(raw + 148, spaces.constData(), 6);
        if (e.isDir) {
            h[156] = char('5');
        } else if (!e.linkTarget.isEmpty()) {
            h[156] = char('2');
            const QByteArray target = e.linkTarget.toUtf8().left(100);
            ::memcpy(raw + 157, target.constData(), ::size_t(target.size()));
        } else {
            h[156] = char('0');
            const QString size = QStringLiteral("%1").arg(e.content.size(), 11, 8,
                                                          QLatin1Char('0'));
            ::memcpy(raw + 124, size.toLatin1().constData(), 11);
        }
        // magic "ustar\0" (raw bytes; no template overload ambiguity)
        ::memcpy(raw + 257, "ustar\0", 6);
        out += h;
        if (!e.isDir && e.linkTarget.isEmpty()) {
            out += e.content;
            const qint64 pad = (512 - (e.content.size() % 512)) % 512;
            out += QByteArray(int(pad), char(0));
        }
    }
    out += QByteArray(1024, char(0));  // end-of-archive marker
    return out;
}

QByteArray gzipBytes(const QByteArray &data)
{
    QByteArray out;
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
    stream.avail_in = static_cast<uInt>(data.size());
    deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                 Z_DEFAULT_STRATEGY);
    out.resize(data.size() + (data.size() >> 4) + 128);
    stream.next_out = reinterpret_cast<Bytef *>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    deflate(&stream, Z_FINISH);
    out.truncate(int(stream.total_out));
    deflateEnd(&stream);
    return out;
}

TEntry tarEntry(const QString &name, const QByteArray &content,
                quint32 mode = 0644)
{
    TEntry e;
    e.name = name;
    e.content = content;
    e.mode = mode;
    return e;
}

class TestArchiveExtractor : public QObject
{
    Q_OBJECT

private slots:
    void extractsStoredAndDeflate()
    {
        QTemporaryDir dir;
        // ~100 KB made of a repeated 256-byte pseudo-random block: it compresses
        // so the entry really exercises the DEFLATE decoder. The realized ratio is
        // well below the 200:1 anti-bomb compression-ratio limit.
        QByteArray block;
        block.reserve(256);
        quint32 state = 0x12345678u;
        for (int i = 0; i < 256; ++i) {
            state = state * 1664525u + 1013904223u;
            block.append(char((state >> 24) & 0xFF));
        }
        QByteArray big;
        big.reserve(100000);
        while (big.size() < 100000)
            big += block;
        big.truncate(100000);
        QList<ESpec> specs;
        specs.append(makeEntry(QStringLiteral("hello.txt"), QByteArray("Hello, world!")));
        specs.append(makeEntry(QStringLiteral("compressed.bin"), big, true));
        const QString zip = writeZip(dir, QStringLiteral("a.zip"), buildZip(specs));
        const QString dest = QDir(dir.path()).filePath(QStringLiteral("out"));
        const ExtractResult r = ArchiveExtractor::extractZip(zip, dest);
        QVERIFY(r.ok);
        QCOMPARE(r.error, QString());
        QCOMPARE(r.fileCount, 2);

        QFile hello(QDir(dest).filePath("hello.txt"));
        QVERIFY(hello.open(QIODevice::ReadOnly));
        QCOMPARE(hello.readAll(), QByteArrayLiteral("Hello, world!"));

        QFile compressed(QDir(dest).filePath("compressed.bin"));
        QVERIFY(compressed.open(QIODevice::ReadOnly));
        QCOMPARE(compressed.readAll(), big);
    }

    void rejectsPathTraversal()
    {
        QTemporaryDir dir;
        QList<ESpec> specs;
        specs.append(makeEntry(QStringLiteral("../evil.txt"), QByteArrayLiteral("x")));
        const QString zip = writeZip(dir, QStringLiteral("t.zip"), buildZip(specs));
        QVERIFY(!ArchiveExtractor::extractZip(zip, QDir(dir.path()).filePath("out")).ok);
    }

    void rejectsAbsoluteAndDriveAndBackslash()
    {
        QTemporaryDir dir;
        QList<ESpec> specs;
        specs.append(makeEntry(QStringLiteral("/etc/passwd"), QByteArrayLiteral("x")));
        QVERIFY(!ArchiveExtractor::extractZip(writeZip(dir, "a", buildZip(specs)),
                                              QDir(dir.path()).filePath("o1")).ok);
        specs.clear();
        specs.append(makeEntry(QStringLiteral("C:/win/x"), QByteArrayLiteral("x")));
        QVERIFY(!ArchiveExtractor::extractZip(writeZip(dir, "b", buildZip(specs)),
                                              QDir(dir.path()).filePath("o2")).ok);
        specs.clear();
        specs.append(makeEntry(QStringLiteral("dir\\file"), QByteArrayLiteral("x")));
        QVERIFY(!ArchiveExtractor::extractZip(writeZip(dir, "c", buildZip(specs)),
                                              QDir(dir.path()).filePath("o3")).ok);
    }

    void rejectsControlChar()
    {
        QTemporaryDir dir;
        QList<ESpec> specs;
        specs.append(makeEntry(QStringLiteral("a\x01b"), QByteArrayLiteral("x")));
        const QString zip = writeZip(dir, QStringLiteral("d.zip"), buildZip(specs));
        QVERIFY(!ArchiveExtractor::extractZip(zip, QDir(dir.path()).filePath("out")).ok);
    }

    void rejectsSymbolicLink()
    {
        QTemporaryDir dir;
        QList<ESpec> specs;
        // mode type 0xA000 = symlink
        specs.append(makeEntry(QStringLiteral("link"), QByteArrayLiteral("target"),
                               false, 0xA0000000u));
        const QString zip = writeZip(dir, "e.zip", buildZip(specs));
        const ExtractResult r = ArchiveExtractor::extractZip(zip, QDir(dir.path()).filePath("out"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("link")));
    }

    void rejectsDuplicatePaths()
    {
        QTemporaryDir dir;
        QList<ESpec> specs;
        specs.append(makeEntry(QStringLiteral("same.txt"), QByteArrayLiteral("a")));
        specs.append(makeEntry(QStringLiteral("same.txt"), QByteArrayLiteral("b")));
        const QString zip = writeZip(dir, "f.zip", buildZip(specs));
        QVERIFY(!ArchiveExtractor::extractZip(zip, QDir(dir.path()).filePath("out")).ok);
    }

    void rejectsSizeLimitBomb()
    {
        QTemporaryDir dir;
        QList<ESpec> specs;
        // A single 32-bit size field cannot exceed 4 GiB - 1, so use five
        // entries each declaring 1 GiB: the 5 GiB cumulative size trips the
        // 4 GiB anti-bomb limit before any per-entry checks.
        for (int i = 0; i < 5; ++i)
            specs.append(makeEntry(QStringLiteral("bomb%1.bin").arg(i),
                                   QByteArrayLiteral("A"), true));
        const QString zip = writeZip(dir, "g.zip", buildZip(specs, 1LL << 30));
        const ExtractResult r = ArchiveExtractor::extractZip(zip, QDir(dir.path()).filePath("out"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("size")));
    }

    void rejectsImpossibleCompressionRatio()
    {
        QTemporaryDir dir;
        QList<ESpec> specs;
        specs.append(makeEntry(QStringLiteral("ratio.bin"), QByteArray(10, 'A'), true));
        const qint64 compSz = qint64(rawDeflate(QByteArray(10, 'A')).size());
        const QString zip = writeZip(dir, "h.zip", buildZip(specs, compSz * 300));
        const ExtractResult r = ArchiveExtractor::extractZip(zip, QDir(dir.path()).filePath("out"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("ratio")));
    }

    void setsExecutableBitOnUnix()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir dir;
        QList<ESpec> specs;
        specs.append(makeEntry(QStringLiteral("run.sh"), QByteArrayLiteral("#!/bin/sh\n"),
                               false, 0755u << 16));
        const QString zip = writeZip(dir, "i.zip", buildZip(specs));
        const ExtractResult r = ArchiveExtractor::extractZip(zip, QDir(dir.path()).filePath("out"));
        QVERIFY(r.ok);
        const QFileInfo fi(QDir(dir.path()).filePath("out/run.sh"));
        QVERIFY(fi.permissions() & QFileDevice::ExeUser);
        QVERIFY(fi.permissions() & QFileDevice::ExeOther);
#endif
    }

    void rejectsCrcMismatch()
    {
        QTemporaryDir dir;
        QList<ESpec> specs;
        specs.append(makeEntry(QStringLiteral("file.bin"), QByteArrayLiteral("payload"),
                               false, 0, 0x12345678u));
        const QString zip = writeZip(dir, "j.zip", buildZip(specs));
        QVERIFY(!ArchiveExtractor::extractZip(zip, QDir(dir.path()).filePath("out")).ok);
    }

    // --- tar.gz ------------------------------------------------------------

    void extractsTarGzWithSymlinks()
    {
        // Mirrors the real llama.cpp macOS archive: a top-level directory, an
        // executable llama-server, a plain file, and versioned .dylib symlinks.
        QTemporaryDir dir;
        QList<TEntry> entries;
        TEntry d; d.name = QStringLiteral("llama-b10825"); d.isDir = true;
        entries << d;
        entries << tarEntry(QStringLiteral("llama-b10825/llama-server"),
                            QByteArray("#!/bin/sh\necho llama-server\n"), 0755);
        entries << tarEntry(QStringLiteral("llama-b10825/readme.txt"),
                            QByteArray("llama.cpp\n"));
        TEntry link; link.name = QStringLiteral("llama-b10825/libggml.dylib");
        link.linkTarget = QStringLiteral("libggml.0.23.0.dylib");
        entries << link;

        const QString archive = writeZip(dir, QStringLiteral("a.tar.gz"),
                                         gzipBytes(buildTar(entries)));
        const QString dest = QDir(dir.path()).filePath(QStringLiteral("out"));
        const ExtractResult r = ArchiveExtractor::extractTarGz(archive, dest);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.fileCount, 2);

        QFileInfo server(QDir(dest).filePath("llama-b10825/llama-server"));
        QVERIFY(server.exists());
#ifdef Q_OS_UNIX
        QVERIFY(server.permissions() & QFileDevice::ExeUser);
#endif
        QFile readme(QDir(dest).filePath("llama-b10825/readme.txt"));
        QVERIFY(readme.open(QIODevice::ReadOnly));
        QCOMPARE(readme.readAll(), QByteArrayLiteral("llama.cpp\n"));
#ifdef Q_OS_UNIX
        // The link is relative to its own directory; the target file is not in
        // this synthetic archive, so check the link itself, not its target.
        QFileInfo linkInfo(QDir(dest).filePath("llama-b10825/libggml.dylib"));
        QVERIFY(linkInfo.isSymLink());
        QCOMPARE(QFileInfo(linkInfo.symLinkTarget()).fileName(),
                 QStringLiteral("libggml.0.23.0.dylib"));
#endif
    }

    void dispatchRoutesTarGzByName()
    {
        QTemporaryDir dir;
        QList<TEntry> entries;
        entries << tarEntry(QStringLiteral("f.txt"), QByteArrayLiteral("x"));
        const QString archive = writeZip(dir, QStringLiteral("rel.tar.gz"),
                                         gzipBytes(buildTar(entries)));
        const QString dest = QDir(dir.path()).filePath(QStringLiteral("out"));
        const ExtractResult r = ArchiveExtractor::extractArchive(archive, dest);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(QFile::exists(QDir(dest).filePath("f.txt")));
    }

    void dispatchRejectsUnknownExtension()
    {
        QTemporaryDir dir;
        const QString archive = writeZip(dir, QStringLiteral("rel.7z"), QByteArrayLiteral("x"));
        const ExtractResult r = ArchiveExtractor::extractArchive(archive, dir.path());
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("Unsupported")));
    }

    void tarRejectsTraversal()
    {
        QTemporaryDir dir;
        QList<TEntry> entries;
        entries << tarEntry(QStringLiteral("../evil"), QByteArrayLiteral("x"));
        const QString archive = writeZip(dir, QStringLiteral("t.tar.gz"),
                                         gzipBytes(buildTar(entries)));
        QVERIFY(!ArchiveExtractor::extractTarGz(archive, QDir(dir.path()).filePath("out")).ok);
    }

    void tarSkipsUnsafeSymlink()
    {
        QTemporaryDir dir;
        QList<TEntry> entries;
        TEntry link; link.name = QStringLiteral("a/b");
        link.linkTarget = QStringLiteral("../../etc/passwd");
        entries << link;
        const QString archive = writeZip(dir, QStringLiteral("s.tar.gz"),
                                         gzipBytes(buildTar(entries)));
        const ExtractResult r = ArchiveExtractor::extractTarGz(archive, QDir(dir.path()).filePath("out"));
        QVERIFY(r.ok);
        QVERIFY(!r.warning.isEmpty());
    }

    void tarRejectsNonGzip()
    {
        QTemporaryDir dir;
        const QString archive = writeZip(dir, QStringLiteral("x.tar.gz"),
                                         QByteArrayLiteral("this is not gzip"));
        const ExtractResult r = ArchiveExtractor::extractTarGz(archive, dir.path());
        QVERIFY(!r.ok);
    }

    // Optional integration check against a real llama.cpp release archive:
    // point LLOCR_REAL_TAR_GZ at a downloaded `.tar.gz` to verify extraction
    // produces a usable llama-server. Skipped when the variable is unset.
    void extractsRealLlamaArchive()
    {
#ifdef Q_OS_UNIX
        const char *env = ::getenv("LLOCR_REAL_TAR_GZ");
        if (!env || !*env)
            QSKIP("LLOCR_REAL_TAR_GZ not set");
        const QString archive = QString::fromUtf8(env);
        QTemporaryDir dir;
        const ExtractResult r = ArchiveExtractor::extractTarGz(archive, dir.path());
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(QFile::exists(QDir(dir.path()).filePath("llama-b10825/llama-server")));
#else
        QSKIP("real-archive check runs on Unix only");
#endif
    }
};

QTEST_MAIN(TestArchiveExtractor)
#include "test_archive_extractor.moc"
