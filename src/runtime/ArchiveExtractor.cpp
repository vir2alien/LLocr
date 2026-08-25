#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileDevice>

#include <array>
#include <cstdint>
#include <vector>

#include "runtime/ArchiveExtractor.h"

namespace llocr {

// ---------------------------------------------------------------------------
// CRC-32 (PKZIP / zlib: reflected poly 0xEDB88320, init 0xFFFFFFFF).
// ---------------------------------------------------------------------------
namespace crc32 {
std::array<uint32_t, 256> table;
bool built = false;

void ensure()
{
    if (built)
        return;
    const uint32_t poly = 0xEDB88320U;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1U) ? (poly ^ (c >> 1)) : (c >> 1);
        table[i] = c;
    }
    built = true;
}

uint32_t update(uint32_t crc, const char *data, qint64 len)
{
    for (qint64 i = 0; i < len; ++i)
        crc = table[(crc ^ (unsigned char)data[i]) & 0xFFU] ^ (crc >> 8);
    return crc;
}
}  // namespace crc32

QByteArray packBytes(const std::vector<unsigned char> &v)
{
    QByteArray out;
    out.reserve(int(v.size()));
    for (unsigned char b : v)
        out.append(char(b));
    return out;
}

// ---------------------------------------------------------------------------
// Bit reader over a compressed payload (LSB-first, RFC 1951). Incomplete reads
// yield zero bits so it never reads out of bounds; callers validate sizes.
// ---------------------------------------------------------------------------
class BitReader
{
public:
    BitReader(const QByteArray &src)
        : m_src(src) {}

    bool need(int n) const { return m_bitPos + n <= m_src.size() * 8; }

    int bits(int n)
    {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            const int byteIdx = m_bitPos >> 3;
            const int bitIdx = m_bitPos & 7;
            const int b = byteIdx < m_src.size() ? ((m_src[byteIdx] >> bitIdx) & 1) : 0;
            v |= (unsigned)b << i;
            ++m_bitPos;
        }
        return int(v);
    }

    void alignByte() { m_bitPos = (m_bitPos + 7) & ~7; }
    qint64 bytesConsumed() const { return (m_bitPos + 7) / 8; }

private:
    const QByteArray m_src;
    int m_bitPos = 0;
};

// ---------------------------------------------------------------------------
// Canonical Huffman trie (RFC 1951). `lens[sym]` is the code length of symbol
// `sym`. build() validates the code-length set and inserts one path per used
// symbol; decode() walks one bit at a time, never over-consuming.
// ---------------------------------------------------------------------------
class HuffmanTrie
{
    struct Node {
        int child[2] = {-1, -1};
        int symbol = -1;
    };
    std::vector<Node> m_nodes;

public:
    bool build(const std::vector<int> &lens)
    {
        m_nodes.clear();
        m_nodes.push_back(Node());

        std::array<int, 16> count = {0};
        int maxLen = 0;
        for (int l : lens) {
            if (l < 0 || l > 15)
                return false;
            if (l == 0)
                continue;
            if (l > maxLen)
                maxLen = l;
            ++count[l];
        }
        if (maxLen == 0)
            return false;

        std::array<int, 16> nextCode = {0};
        int code = 0;
        for (int len = 1; len <= 15; ++len) {
            code = (code + count[len - 1]) << 1;
            nextCode[len] = code;
            if (code + count[len] > (1 << len))
                return false;  // over-subscribed canonical set
        }

        for (int len = 1; len <= 15; ++len) {
            for (int sym = 0; sym < int(lens.size()); ++sym) {
                if (lens[sym] != len)
                    continue;
                int c = nextCode[len]++;
                int node = 0;
                for (int bit = len - 1; bit >= 0; --bit) {
                    const int b = (c >> bit) & 1;
                    if (m_nodes[node].child[b] < 0) {
                        m_nodes[node].child[b] = int(m_nodes.size());
                        m_nodes.push_back(Node());
                    }
                    node = m_nodes[node].child[b];
                }
                m_nodes[node].symbol = sym;
            }
        }
        return true;
    }

    // Returns the decoded symbol, or -1 (invalid) / -2 (truncated).
    int decode(BitReader &br) const
    {
        if (m_nodes.empty())
            return -1;
        int node = 0;
        while (m_nodes[node].symbol < 0) {
            if (!br.need(1))
                return -2;
            const int b = br.bits(1);
            const int nx = m_nodes[node].child[b];
            if (nx < 0)
                return -1;
            node = nx;
        }
        return m_nodes[node].symbol;
    }
};

// DEFLATE fixed/huffman tables per RFC 1951.
const int kLengthBase[29] = {3,   4,   5,   6,   7,   8,   9,   10,  11, 13,
                             15,  17,  19,  23,  27,  31,  35,  43,  51, 59,
                             67,  83,  99,  115, 131, 163, 195, 227, 258};
const int kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                              2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistanceBase[30] = {1,    2,    3,    4,    5,    7,    9,    13,
                                17,   25,   33,   49,   65,   97,   129,  193,
                                257,  385,  513,  769,  1025, 1537, 2049, 3073,
                                4097, 6145, 8193, 12289, 16385, 24577};
const int kDistanceExtra[30] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,
                                6, 7, 7,  8,  8,  9,  9,  10, 10, 11, 11, 12, 12, 13, 13};
const int kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                        11, 4,  12, 3, 13, 2, 14, 1, 15};

bool setErr(QString &err, const QString &msg)
{
    err = msg;
    return false;
}

// ---------------------------------------------------------------------------
// DEFLATE (RFC 1951) stream decoder.
// ---------------------------------------------------------------------------
class DeflateDecoder
{
public:
    DeflateDecoder(qint64 capBytes)
        : m_cap(capBytes) {}

    // Returns empty error string on success. `out` receives decompressed data.
    QString inflate(const QByteArray &in, std::vector<unsigned char> &out)
    {
        BitReader br(in);
        bool final = false;
        while (!final) {
            if (!br.need(3))
                return QStringLiteral("truncated deflate block header");
            final = br.bits(1);
            const int btype = br.bits(2);
            QString err;
            if (btype == 0)
                err = storedBlock(br, out);
            else if (btype == 1) {
                HuffmanTrie lit, dst;
                buildFixedTables(lit, dst);
                err = inflateLoop(br, out, lit, dst);
            } else if (btype == 2)
                err = dynamicBlock(br, out);
            else
                return QStringLiteral("reserved deflate block type");
            if (!err.isEmpty())
                return err;
        }
        return QString();
    }

private:
    qint64 m_cap;

    void buildFixedTables(HuffmanTrie &lit, HuffmanTrie &dst)
    {
        std::vector<int> litLens(288, 0);
        for (int i = 0; i < 144; ++i) litLens[i] = 8;
        for (int i = 144; i < 256; ++i) litLens[i] = 9;
        for (int i = 256; i < 280; ++i) litLens[i] = 7;
        for (int i = 280; i < 288; ++i) litLens[i] = 8;
        std::vector<int> dstLens(32, 5);
        lit.build(litLens);
        dst.build(dstLens);
    }

    QString storedBlock(BitReader &br, std::vector<unsigned char> &out)
    {
        br.alignByte();
        if (!br.need(32))
            return QStringLiteral("stored block header truncated");
        const int len = br.bits(16);
        const int nlen = br.bits(16);
        if ((len ^ 0xFFFF) != nlen)
            return QStringLiteral("stored block length mismatch");
        if (!br.need(len * 8))
            return QStringLiteral("stored block data truncated");
        for (int i = 0; i < len; ++i)
            out.push_back(static_cast<unsigned char>(br.bits(8)));
        return QString();
    }

    bool buildDynamicTables(BitReader &br, HuffmanTrie &lit, HuffmanTrie &dst,
                            QString &err)
    {
        if (!br.need(14))
            return setErr(err, QStringLiteral("dynamic huffman header truncated"));
        const int hlit = br.bits(5) + 257;
        const int hdist = br.bits(5) + 1;
        const int hclen = br.bits(4) + 4;

        std::vector<int> clLens(19, 0);
        for (int i = 0; i < hclen; ++i) {
            if (!br.need(3))
                return setErr(err, "code length codes truncated");
            clLens[kOrder[i]] = br.bits(3);
        }
        HuffmanTrie cl;
        if (!cl.build(clLens))
            return setErr(err, "invalid code-length huffman table");

        std::vector<int> lens(hlit + hdist, 0);
        int idx = 0;
        const int total = hlit + hdist;
        while (idx < total) {
            const int sym = cl.decode(br);
            if (sym < 0)
                return setErr(err, "invalid code-length code");
            if (sym < 16) {
                lens[idx++] = sym;
            } else if (sym == 16) {
                if (idx == 0)
                    return setErr(err, "repeat with no previous length");
                if (!br.need(2))
                    return setErr(err, "repeat count truncated");
                const int rep = 3 + br.bits(2);
                const int prev = lens[idx - 1];
                for (int i = 0; i < rep && idx < total; ++i)
                    lens[idx++] = prev;
            } else {
                if (!br.need(sym == 17 ? 3 : 7))
                    return setErr(err, "zero repeat truncated");
                int rep = (sym == 17 ? 3 : 11) + br.bits(sym == 17 ? 3 : 7);
                for (int i = 0; i < rep && idx < total; ++i)
                    lens[idx++] = 0;
            }
        }
        if (idx != total)
            return setErr(err, "code lengths overflow");
        if (lens[256] == 0)
            return setErr(err, "missing end-of-block code");

        std::vector<int> litLens(lens.begin(), lens.begin() + hlit);
        std::vector<int> dstLens(lens.begin() + hlit, lens.end());
        if (!lit.build(litLens))
            return setErr(err, "invalid literal huffman table");
        if (!dst.build(dstLens) && hdist > 0)
            return setErr(err, "invalid distance huffman table");
        return true;
    }

    QString dynamicBlock(BitReader &br, std::vector<unsigned char> &out)
    {
        QString err;
        HuffmanTrie lit, dst;
        if (!buildDynamicTables(br, lit, dst, err))
            return err;
        return inflateLoop(br, out, lit, dst);
    }

    QString inflateLoop(BitReader &br, std::vector<unsigned char> &out,
                        const HuffmanTrie &lit, const HuffmanTrie &dst)
    {
        for (;;) {
            const int sym = lit.decode(br);
            if (sym < 0)
                return QStringLiteral("invalid literal code");
            if (sym == 256)
                return QString();
            if (sym < 256) {
                out.push_back(static_cast<unsigned char>(sym));
                if (m_cap > 0 && qint64(out.size()) > m_cap)
                    return QStringLiteral("deflate output exceeds declared size");
                continue;
            }
            if (sym > 285)
                return QStringLiteral("invalid length code");
            const int li = sym - 257;
            int length = kLengthBase[li];
            if (kLengthExtra[li]) {
                if (!br.need(kLengthExtra[li]))
                    return QStringLiteral("truncated length extra bits");
                length += br.bits(kLengthExtra[li]);
            }
            const int d = dst.decode(br);
            if (d < 0 || d >= 30)
                return QStringLiteral("invalid distance code");
            int distance = kDistanceBase[d];
            if (kDistanceExtra[d]) {
                if (!br.need(kDistanceExtra[d]))
                    return QStringLiteral("truncated distance extra bits");
                distance += br.bits(kDistanceExtra[d]);
            }
            if (qint64(out.size()) < distance)
                return QStringLiteral("distance before start of output");
            for (int i = 0; i < length; ++i)
                out.push_back(out[qint64(out.size()) - distance]);
            if (m_cap > 0 && qint64(out.size()) > m_cap)
                return QStringLiteral("deflate output exceeds declared size");
        }
    }
};

// ---------------------------------------------------------------------------
// Byte reader over the whole archive, for header parsing.
// ---------------------------------------------------------------------------
class Reader
{
public:
    Reader(const QByteArray &b)
        : m_b(b) {}

    bool avail(int n) const { return m_pos + n <= m_b.size(); }
    int pos() const { return m_pos; }
    void seek(int p) { m_pos = p; }
    void skip(int n) { m_pos += n; }

    quint16 readU16()
    {
        quint16 v = static_cast<quint16>(static_cast<quint8>(m_b[m_pos]))
                    | static_cast<quint16>(static_cast<quint16>(static_cast<quint8>(m_b[m_pos + 1])) << 8);
        m_pos += 2;
        return v;
    }
    quint32 readU32()
    {
        quint32 v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<quint32>(static_cast<quint8>(m_b[m_pos + i])) << (8 * i);
        m_pos += 4;
        return v;
    }
    QByteArray take(int n)
    {
        const QByteArray v = m_b.mid(m_pos, n);
        m_pos += n;
        return v;
    }

private:
    const QByteArray m_b;
    int m_pos = 0;
};

const quint32 kLocalSig = 0x04034b50u;
const quint32 kEocdSig = 0x06054b50u;
const quint32 kCentralSig = 0x02014b50u;

struct ZipEntry {
    quint16 method;
    quint32 crc;
    qint64 compSize;
    qint64 uncompSize;
    quint32 extAttr;
    qint64 localOffset;
    QString name;
};

// Validates and normalises a zip entry name. Returns a clean relative path with
// '/' separators, or sets `ok=false` (with `error`) when it would escape the
// extraction directory.
QString normalizeName(const QString &raw, bool &ok, QString &error, bool &isDir)
{
    isDir = raw.endsWith(QLatin1Char('/'));
    for (QChar ch : raw) {
        const ushort u = ch.unicode();
        if (u < 0x20 || u == 0x7f) {
            ok = false;
            error = QStringLiteral("control character in zip entry name");
            return QString();
        }
    }
    if (raw.contains(QLatin1Char('\\'))) {
        ok = false;
        error = QStringLiteral("backslash in zip entry name");
        return QString();
    }
    if (raw.startsWith(QLatin1Char('/')) || raw.startsWith(QLatin1String("//"))) {
        ok = false;
        error = QStringLiteral("absolute path in zip entry name");
        return QString();
    }
    if (raw.length() >= 2 && raw.at(1) == QLatin1Char(':')) {
        ok = false;
        error = QStringLiteral("drive prefix in zip entry name");
        return QString();
    }
    const QStringList parts = raw.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        if (p == QStringLiteral("..") || p == QStringLiteral(".")) {
            ok = false;
            error = QStringLiteral("traversal in zip entry name");
            return QString();
        }
    }
    if (parts.isEmpty()) {
        ok = !isDir;  // a bare trailing '/' is a no-op directory marker
        return QString();
    }
    ok = true;
    return parts.join(QLatin1Char('/'));
}

ExtractResult ArchiveExtractor::extractZip(const QString &zipPath, const QString &destDir)
{
    ExtractResult res;
    crc32::ensure();
    QFile f(zipPath);
    if (!f.open(QIODevice::ReadOnly)) {
        res.error = QObject::tr("Unable to open archive: %1").arg(zipPath);
        return res;
    }
    const QByteArray data = f.readAll();
    f.close();

    const int n = data.size();
    if (n < 22) {
        res.error = QStringLiteral("not a ZIP archive");
        return res;
    }
    // Find the end-of-central-directory by scanning back over the trailer.
    int eocd = -1;
    const int searchFrom = qMax(0, n - 65557);
    for (int i = n - 22; i >= searchFrom; --i) {
        if (i < 0)
            break;
        bool found = static_cast<quint8>(data[i]) == 0x50;
        if (!found)
            continue;
        const quint32 sig = static_cast<quint32>(static_cast<quint8>(data[i]))
                            | (static_cast<quint32>(static_cast<quint8>(data[i + 1])) << 8)
                            | (static_cast<quint32>(static_cast<quint8>(data[i + 2])) << 16)
                            | (static_cast<quint32>(static_cast<quint8>(data[i + 3])) << 24);
        if (sig == kEocdSig) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        res.error = QStringLiteral("not a ZIP archive (trailer missing)");
        return res;
    }

    Reader r(data);
    // EOCD layout: sig(4) disk(2) disk(2) entries(2) total(2) size(4) offset(4).
    r.seek(eocd + 12);
    const quint32 centralSize = r.readU32();
    const quint32 centralOffset = r.readU32();

    QList<ZipEntry> entries;
    Reader c(data);
    c.seek(int(centralOffset));
    const quint64 centralEnd = quint64(centralOffset) + centralSize;
    while (quint64(c.pos()) + 46 <= centralEnd) {
        if (c.readU32() != kCentralSig) {
            res.error = QStringLiteral("corrupt central directory");
            return res;
        }
        c.skip(4);
        const quint16 flags = c.readU16();
        (void)flags;
        const quint16 method = c.readU16();
        c.skip(4);
        const quint32 crc = c.readU32();
        const quint32 compSize = c.readU32();
        const quint32 uncompSize = c.readU32();
        const quint16 nameLen = c.readU16();
        const quint16 extraLen = c.readU16();
        const quint16 commentLen = c.readU16();
        c.skip(2);
        c.skip(2);
        const quint32 extAttr = c.readU32();
        const quint32 localOffset = c.readU32();
        const QByteArray nameBytes = c.take(nameLen);
        c.skip(extraLen + commentLen);

        bool nok = false;
        QString nerr;
        bool isDir = false;
        const QString norm = normalizeName(QString::fromUtf8(nameBytes), nok, nerr, isDir);
        if ((!nok && !nerr.isEmpty()) || (!nok && !isDir)) {
            res.error = nerr;
            return res;
        }
        if (isDir)
            continue;
        if (norm.isEmpty())
            continue;

        ZipEntry e;
        e.method = method;
        e.crc = crc;
        e.compSize = compSize;
        e.uncompSize = uncompSize;
        e.extAttr = extAttr;
        e.localOffset = localOffset;
        e.name = norm;
        entries.append(e);
    }

    // Enforce counts and cumulative limits up front.
    if (entries.size() > kMaxFiles) {
        res.error = QStringLiteral("archive contains too many files");
        return res;
    }
    qint64 total = 0;
    for (const ZipEntry &e : entries) {
        total += e.uncompSize;
        if (total > kMaxTotalBytes) {
            res.error = QStringLiteral("archive total uncompressed size exceeds limit");
            return res;
        }
    }

    QSet<QString> written;
    res.ok = true;
    for (const ZipEntry &e : entries) {
        // Reject link entries (symlink type 0xA000 in high mode bits) entirely.
        const quint32 type = (e.extAttr >> 16) & 0xF000u;
        if (type == 0xA000u) {
            res.error = QStringLiteral("zip entry is a symbolic link (rejected)");
            res.ok = false;
            return res;
        }
        if (written.contains(e.name)) {
            res.error = QStringLiteral("duplicate path in archive: %1").arg(e.name);
            res.ok = false;
            return res;
        }
        written.insert(e.name);

        // Locate the data via the local file header.
        Reader lr(data);
        lr.seek(int(e.localOffset));
        if (lr.readU32() != kLocalSig) {
            res.error = QStringLiteral("corrupt local header");
            res.ok = false;
            return res;
        }
        lr.skip(22);
        const quint16 fnLen = lr.readU16();
        const quint16 exLen = lr.readU16();
        lr.skip(fnLen + exLen);
        const int dataOff = lr.pos();
        if (dataOff + e.compSize > n) {
            res.error = QStringLiteral("zip entry data truncated");
            res.ok = false;
            return res;
        }

        if (e.uncompSize > ArchiveExtractor::kMaxTotalBytes) {
            res.error = QStringLiteral("zip entry exceeds size limit");
            res.ok = false;
            return res;
        }

        QByteArray payload;
        if (e.method == 0) {
            if (e.compSize != e.uncompSize) {
                res.error = QStringLiteral("stored entry size mismatch");
                res.ok = false;
                return res;
            }
            payload = data.mid(dataOff, int(e.compSize));
        } else if (e.method == 8) {
            // Compression ratio anti-bomb check per entry.
            if (e.compSize > 0 && e.uncompSize > e.compSize * kMaxCompressionRatio) {
                res.error = QStringLiteral("zip entry exceeds compression ratio limit");
                res.ok = false;
                return res;
            }
            std::vector<unsigned char> out;
            out.reserve(size_t(qMin<qint64>(e.uncompSize, ArchiveExtractor::kMaxTotalBytes)));
            DeflateDecoder dec(e.uncompSize);
            const QString err = dec.inflate(data.mid(dataOff, int(e.compSize)), out);
            if (!err.isEmpty()) {
                res.error = err;
                res.ok = false;
                return res;
            }
            payload = packBytes(out);
        } else {
            res.error = QStringLiteral("unsupported zip compression method");
            res.ok = false;
            return res;
        }

        // Verify declared size and CRC.
        if (qint64(payload.size()) != e.uncompSize) {
            res.error = QStringLiteral("zip entry size verification failed");
            res.ok = false;
            return res;
        }
        const uint32_t crcComputed =
            crc32::update(0xFFFFFFFFu, payload.constData(), payload.size());
        if (crcComputed != e.crc) {
            res.error = QStringLiteral("zip entry CRC mismatch");
            res.ok = false;
            return res;
        }

        // Write, creating parent directories.
        const QString destPath = QDir(destDir).filePath(e.name);
        QDir().mkpath(QFileInfo(destPath).absolutePath());
        QFile out(destPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            res.error = QObject::tr("unable to write %1").arg(destPath);
            res.ok = false;
            return res;
        }
        out.write(payload);
        out.flush();
        out.close();

#ifdef Q_OS_UNIX
        // Set executable bits only for entries whose mode advertises them.
        const quint32 mode = e.extAttr >> 16;
        if (mode & 0100u) {
            QFile pf(destPath);
            pf.setPermissions(pf.permissions() | QFileDevice::ExeUser | QFileDevice::ExeGroup
                              | QFileDevice::ExeOther);
        }
#else
        (void)0;
#endif
        ++res.fileCount;
        res.totalBytes += e.uncompSize;
    }
    return res;
}

}  // namespace llocr