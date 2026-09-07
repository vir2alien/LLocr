#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>

#include <zlib.h>

#include "runtime/ArchiveExtractor.h"

namespace llocr {

namespace {

constexpr quint32 kLocalSig = 0x04034b50u;
constexpr quint32 kEocdSig = 0x06054b50u;
constexpr quint32 kCentralSig = 0x02014b50u;
constexpr qint64 kChunkSize = 256 * 1024;

struct ZipEntry {
    quint16 method = 0;
    quint32 crc = 0;
    qint64 compSize = 0;
    qint64 uncompSize = 0;
    quint32 extAttr = 0;
    qint64 localOffset = 0;
    QString name;
};

class Reader
{
public:
    explicit Reader(const QByteArray &bytes)
        : m_bytes(bytes) {}

    bool canRead(qint64 count) const
    {
        return count >= 0 && m_pos >= 0 && m_pos <= m_bytes.size()
               && count <= m_bytes.size() - m_pos;
    }

    qint64 pos() const { return m_pos; }
    void seek(qint64 position) { m_pos = position; }

    quint16 readU16()
    {
        if (!canRead(2))
            return 0;
        const auto *p = reinterpret_cast<const uchar *>(m_bytes.constData() + m_pos);
        m_pos += 2;
        return quint16(p[0]) | quint16(p[1] << 8);
    }

    quint32 readU32()
    {
        if (!canRead(4))
            return 0;
        const auto *p = reinterpret_cast<const uchar *>(m_bytes.constData() + m_pos);
        m_pos += 4;
        return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16)
               | (quint32(p[3]) << 24);
    }

    QByteArray take(qint64 count)
    {
        if (!canRead(count))
            return {};
        const QByteArray result = m_bytes.sliced(m_pos, count);
        m_pos += count;
        return result;
    }

private:
    const QByteArray &m_bytes;
    qint64 m_pos = 0;
};

QString normalizeName(const QString &raw, bool &ok, QString &error, bool &isDir)
{
    isDir = raw.endsWith(QLatin1Char('/'));
    for (const QChar ch : raw) {
        const ushort value = ch.unicode();
        if (value < 0x20 || value == 0x7f) {
            ok = false;
            error = QStringLiteral("control character in zip entry name");
            return {};
        }
    }
    if (raw.contains(QLatin1Char('\\'))) {
        ok = false;
        error = QStringLiteral("backslash in zip entry name");
        return {};
    }
    if (raw.startsWith(QLatin1Char('/')) || raw.startsWith(QLatin1String("//"))) {
        ok = false;
        error = QStringLiteral("absolute path in zip entry name");
        return {};
    }
    if (raw.size() >= 2 && raw.at(1) == QLatin1Char(':')) {
        ok = false;
        error = QStringLiteral("drive prefix in zip entry name");
        return {};
    }

    const QStringList parts = raw.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String(".") || part == QLatin1String("..")) {
            ok = false;
            error = QStringLiteral("traversal in zip entry name");
            return {};
        }
    }
    if (parts.isEmpty()) {
        ok = isDir;
        return {};
    }
    ok = true;
    return parts.join(QLatin1Char('/'));
}

bool inflateRaw(const QByteArray &input, qint64 expectedSize,
                const std::function<bool(const char *, qint64)> &sink,
                QString &error)
{
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = static_cast<uInt>(input.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        error = QStringLiteral("unable to initialize deflate decoder");
        return false;
    }

    std::array<char, kChunkSize> buffer{};
    qint64 produced = 0;
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef *>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        result = inflate(&stream, Z_NO_FLUSH);
        const qint64 count = qint64(buffer.size() - stream.avail_out);
        if (count > 0) {
            produced += count;
            if (expectedSize >= 0 && produced > expectedSize) {
                error = QStringLiteral("deflate output exceeds declared size");
                inflateEnd(&stream);
                return false;
            }
            if (!sink(buffer.data(), count)) {
                error = QStringLiteral("unable to write extracted file");
                inflateEnd(&stream);
                return false;
            }
        }
    }
    const bool ok = result == Z_STREAM_END && (expectedSize < 0 || produced == expectedSize);
    if (!ok)
        error = result == Z_STREAM_END ? QStringLiteral("deflate size verification failed")
                                       : QStringLiteral("invalid deflate stream");
    inflateEnd(&stream);
    return ok;
}

} // namespace

ExtractResult ArchiveExtractor::extractArchive(const QString &archivePath,
                                                const QString &destDir)
{
    if (archivePath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
        return extractZip(archivePath, destDir);
    if (archivePath.endsWith(QStringLiteral(".tar.gz"), Qt::CaseInsensitive)
        || archivePath.endsWith(QStringLiteral(".tgz"), Qt::CaseInsensitive))
        return extractTarGz(archivePath, destDir);
    ExtractResult r;
    r.error = QObject::tr("Unsupported archive type: %1").arg(archivePath);
    return r;
}

ExtractResult ArchiveExtractor::extractZip(const QString &zipPath, const QString &destDir)
{
    ExtractResult result;
    QFile archive(zipPath);
    if (!archive.open(QIODevice::ReadOnly)) {
        result.error = QObject::tr("Unable to open archive: %1").arg(zipPath);
        return result;
    }
    const QByteArray data = archive.readAll();
    const qint64 size = data.size();
    if (size < 22) {
        result.error = QStringLiteral("not a ZIP archive");
        return result;
    }

    qint64 eocd = -1;
    const qint64 searchFrom = std::max<qint64>(0, size - 65557);
    for (qint64 i = size - 22; i >= searchFrom; --i) {
        if (static_cast<quint8>(data.at(i)) != 0x50)
            continue;
        Reader candidate(data);
        candidate.seek(i);
        if (candidate.canRead(4) && candidate.readU32() == kEocdSig) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        result.error = QStringLiteral("not a ZIP archive (trailer missing)");
        return result;
    }

    Reader eocdReader(data);
    eocdReader.seek(eocd + 8);
    if (!eocdReader.canRead(14)) {
        result.error = QStringLiteral("truncated ZIP trailer");
        return result;
    }
    const quint16 diskEntries = eocdReader.readU16();
    const quint16 totalEntries = eocdReader.readU16();
    const quint32 centralSize = eocdReader.readU32();
    const quint32 centralOffset = eocdReader.readU32();
    if (diskEntries != totalEntries
        || qint64(centralOffset) + qint64(centralSize) > size) {
        result.error = QStringLiteral("unsupported or corrupt central directory");
        return result;
    }

    QList<ZipEntry> entries;
    Reader central(data);
    central.seek(centralOffset);
    const qint64 centralEnd = qint64(centralOffset) + centralSize;
    while (central.pos() < centralEnd) {
        if (!central.canRead(46) || central.readU32() != kCentralSig) {
            result.error = QStringLiteral("corrupt central directory");
            return result;
        }
        central.seek(central.pos() + 4); // version made/by
        const quint16 flags = central.readU16();
        const quint16 method = central.readU16();
        central.seek(central.pos() + 4); // time/date
        const quint32 crc = central.readU32();
        const quint32 compressedSize = central.readU32();
        const quint32 uncompressedSize = central.readU32();
        const quint16 nameLength = central.readU16();
        const quint16 extraLength = central.readU16();
        const quint16 commentLength = central.readU16();
        central.seek(central.pos() + 4); // disk number, internal attributes
        const quint32 extAttr = central.readU32();
        const quint32 localOffset = central.readU32();
        if (!central.canRead(qint64(nameLength) + extraLength + commentLength)) {
            result.error = QStringLiteral("truncated central directory entry");
            return result;
        }
        const QString rawName = QString::fromUtf8(central.take(nameLength));
        central.seek(central.pos() + extraLength + commentLength);

        bool valid = false;
        bool isDir = false;
        QString nameError;
        const QString name = normalizeName(rawName, valid, nameError, isDir);
        if (!valid && !isDir) {
            result.error = nameError;
            return result;
        }
        if (isDir || name.isEmpty())
            continue;
        if (flags & 0x0001) {
            result.error = QStringLiteral("encrypted ZIP entries are not supported");
            return result;
        }
        entries.append(ZipEntry{method, crc, compressedSize, uncompressedSize,
                                extAttr, localOffset, name});
    }

    if (entries.size() > kMaxFiles) {
        result.error = QStringLiteral("archive contains too many files");
        return result;
    }
    qint64 totalUncompressed = 0;
    for (const ZipEntry &entry : std::as_const(entries)) {
        if (entry.uncompSize > kMaxTotalBytes - totalUncompressed) {
            result.error = QStringLiteral("archive total uncompressed size exceeds limit");
            return result;
        }
        totalUncompressed += entry.uncompSize;
    }

    QSet<QString> written;
    for (const ZipEntry &entry : std::as_const(entries)) {
        const quint32 type = (entry.extAttr >> 16) & 0xF000u;
        if (type == 0xA000u) {
            result.error = QStringLiteral("zip entry is a symbolic link (rejected)");
            return result;
        }
        const QString duplicateKey = entry.name.toLower();
        if (written.contains(duplicateKey)) {
            result.error = QStringLiteral("duplicate path in archive: %1").arg(entry.name);
            return result;
        }
        written.insert(duplicateKey);

        Reader local(data);
        local.seek(entry.localOffset);
        if (!local.canRead(30) || local.readU32() != kLocalSig) {
            result.error = QStringLiteral("corrupt local header");
            return result;
        }
        local.seek(local.pos() + 22);
        const quint16 nameLength = local.readU16();
        const quint16 extraLength = local.readU16();
        const qint64 dataOffset = local.pos() + nameLength + extraLength;
        if (dataOffset < 0 || dataOffset > size
            || entry.compSize > size - dataOffset) {
            result.error = QStringLiteral("zip entry data truncated");
            return result;
        }
        if (entry.uncompSize > kMaxTotalBytes
            || (entry.compSize > 0 && entry.uncompSize > entry.compSize * kMaxCompressionRatio)) {
            result.error = QStringLiteral("zip entry exceeds size or compression ratio limit");
            return result;
        }

        const QString destination = QDir(destDir).filePath(entry.name);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
            result.error = QObject::tr("unable to create directory for %1").arg(destination);
            return result;
        }
        QFile output(destination);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.error = QObject::tr("unable to write %1").arg(destination);
            return result;
        }

        quint32 crc = crc32(0L, Z_NULL, 0);
        qint64 writtenBytes = 0;
        const auto sink = [&](const char *bytes, qint64 count) {
            crc = crc32(crc, reinterpret_cast<const Bytef *>(bytes), static_cast<uInt>(count));
            writtenBytes += count;
            return output.write(bytes, count) == count;
        };
        QString extractionError;
        if (entry.method == 0) {
            for (qint64 offset = 0; offset < entry.compSize; offset += kChunkSize) {
                const qint64 count = std::min(kChunkSize, entry.compSize - offset);
                if (!sink(data.constData() + dataOffset + offset, count)) {
                    extractionError = QStringLiteral("unable to write extracted file");
                    break;
                }
            }
        } else if (entry.method == 8) {
            const QByteArray compressed = data.sliced(dataOffset, entry.compSize);
            inflateRaw(compressed, entry.uncompSize, sink, extractionError);
        } else {
            extractionError = QStringLiteral("unsupported zip compression method");
        }
        if (!extractionError.isEmpty() || !output.flush()) {
            result.error = extractionError.isEmpty()
                               ? QObject::tr("unable to write %1").arg(destination)
                               : extractionError;
            return result;
        }
        output.close();
        if (writtenBytes != entry.uncompSize || crc != entry.crc) {
            result.error = writtenBytes != entry.uncompSize
                               ? QStringLiteral("zip entry size verification failed")
                               : QStringLiteral("zip entry CRC mismatch");
            return result;
        }

#ifdef Q_OS_UNIX
        if ((entry.extAttr >> 16) & 0100u) {
            QFile permissions(destination);
            permissions.setPermissions(permissions.permissions() | QFileDevice::ExeUser
                                        | QFileDevice::ExeGroup | QFileDevice::ExeOther);
        }
#endif
        ++result.fileCount;
        result.totalBytes += entry.uncompSize;
    }
    result.ok = true;
    return result;
}

// ---------------------------------------------------------------------------
// tar.gz support (llama.cpp macOS/Linux release archives, ADR 34 amended)
// ---------------------------------------------------------------------------

namespace {

// Parses an octal ASCII field. Tar numeric fields are zero-padded with NULs
// or spaces (some writers leave them entirely blank for 0); strip both before
// parsing. Returns -1 only for non-octal garbage.
qint64 parseOctalField(const QByteArray &field)
{
    QString s = QString::fromLatin1(field.constData(), field.size());
    s.remove(QChar(0));   // NUL padding
    s = s.trimmed();      // space padding
    if (s.isEmpty())
        return 0;
    bool ok = false;
    const qint64 v = s.toLongLong(&ok, 8);
    return ok ? v : -1;
}

// Reads a NUL- or space-terminated ASCII string from a fixed-size field.
QString fieldString(const QByteArray &field)
{
    int len = 0;
    while (len < field.size() && field.at(len) != 0 && field.at(len) != ' ')
        ++len;
    return QString::fromLatin1(field.constData(), len);
}

// Parses a PAX extended-header record stream: lines of "<len> key=value\n".
// Returns the `path=` / `linkpath=` values if present.
struct PaxValues {
    QString path;
    QString linkpath;
};

PaxValues parsePaxRecords(const QByteArray &data)
{
    PaxValues v;
    qint64 pos = 0;
    while (pos < data.size()) {
        const int nl = data.indexOf('\n', pos);
        if (nl < 0)
            break;
        const QString line = QString::fromLatin1(data.constData() + pos, nl - pos);
        pos = nl + 1;
        // "<len> key=value"
        const int sp = line.indexOf(QLatin1Char(' '));
        if (sp <= 0)
            continue;
        const QString kv = line.mid(sp + 1);
        const int eq = kv.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = kv.left(eq);
        const QString value = kv.mid(eq + 1);
        if (key == QStringLiteral("path"))
            v.path = value;
        else if (key == QStringLiteral("linkpath"))
            v.linkpath = value;
    }
    return v;
}

}  // namespace

ExtractResult ArchiveExtractor::extractTarGz(const QString &tarGzPath,
                                             const QString &destDir)
{
    ExtractResult result;
    QFile archive(tarGzPath);
    if (!archive.open(QIODevice::ReadOnly)) {
        result.error = QObject::tr("Unable to open archive: %1").arg(tarGzPath);
        return result;
    }
    const QByteArray compressed = archive.readAll();
    if (compressed.size() < 18) {
        result.error = QStringLiteral("not a gzip archive");
        return result;
    }

    // Decompress the whole stream into memory (the ZIP path reads the whole
    // archive into memory too; llama.cpp tarballs are a few hundred MB at most).
    // inflateInit2(15 + 32) auto-detects the zlib/gzip wrapper.
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    if (inflateInit2(&stream, 15 + 32) != Z_OK) {
        result.error = QStringLiteral("unable to initialize gzip decoder");
        return result;
    }

    QByteArray tar;
    std::array<char, kChunkSize> chunk{};
    int zres = Z_OK;
    bool bomb = false;
    while (zres == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef *>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        zres = inflate(&stream, Z_NO_FLUSH);
        const qint64 count = qint64(chunk.size() - stream.avail_out);
        if (count > 0) {
            if (tar.size() > kMaxTotalBytes - count) {
                bomb = true;
                break;
            }
            tar.append(chunk.data(), count);
        }
        if (zres == Z_STREAM_END)
            break;
        if (zres != Z_OK && zres != Z_BUF_ERROR) {
            inflateEnd(&stream);
            result.error = QStringLiteral("invalid gzip stream");
            return result;
        }
        if (stream.avail_in == 0 && zres == Z_BUF_ERROR) {
            // not enough input to make progress
            inflateEnd(&stream);
            result.error = QStringLiteral("truncated gzip stream");
            return result;
        }
    }
    inflateEnd(&stream);
    if (bomb) {
        result.error = QObject::tr("archive total uncompressed size exceeds limit");
        return result;
    }
    if (zres != Z_STREAM_END) {
        result.error = QStringLiteral("invalid or truncated gzip stream");
        return result;
    }

    // --- ustar walk --------------------------------------------------------
    QSet<QString> written;
    qint64 pos = 0;
    QString pendingLongName;      // from a GNU 'L' entry
    PaxValues pendingPax;         // from a PAX 'x' entry
    const qint64 size = tar.size();

    while (pos + 512 <= size) {
        bool allZero = true;
        for (qint64 i = pos; i < pos + 512; ++i) {
            if (tar.at(i) != 0) {
                allZero = false;
                break;
            }
        }
        if (allZero)
            break;  // end-of-archive marker

        const QByteArray h = tar.sliced(pos, 512);
        pos += 512;

        QString name = fieldString(h.sliced(0, 100));
        const QString prefix = fieldString(h.sliced(345, 155));
        const QString linkName = fieldString(h.sliced(157, 100));
        const quint8 typeflag = quint8(h.at(156));
        const qint64 fileSize = parseOctalField(h.sliced(124, 12));
        const qint64 mode = parseOctalField(h.sliced(100, 8));
        if (fileSize < 0) {
            result.error = QStringLiteral("corrupt tar header (bad size)");
            return result;
        }

        // Payload belongs to this header; the next header starts aligned to 512.
        const qint64 payloadStart = pos;
        const qint64 payloadEnd = payloadStart + fileSize;
        if (payloadEnd > size) {
            result.error = QStringLiteral("truncated tar payload");
            return result;
        }
        pos = (payloadEnd + 511) & ~qint64(511);
        if (pos > size) {
            result.error = QStringLiteral("truncated tar archive");
            return result;
        }
        const QByteArray payload = tar.sliced(payloadStart, qint64(fileSize));

        // GNU long name / PAX extended header: consume and remember; the next
        // regular entry uses the recorded name.
        if (typeflag == 'L') {
            pendingLongName = QString::fromUtf8(payload.constData(), payload.size());
            pendingLongName = pendingLongName.trimmed();
            if (pendingLongName.endsWith(QLatin1Char('\0')))
                pendingLongName.chop(1);
            continue;
        }
        if (typeflag == 'x' || typeflag == 'g') {
            if (typeflag == 'x')
                pendingPax = parsePaxRecords(payload);
            continue;
        }

        if (!pendingPax.path.isEmpty())
            name = pendingPax.path;
        else if (!pendingLongName.isEmpty())
            name = pendingLongName;

        QString fullName = name;
        if (!prefix.isEmpty())
            fullName = prefix + QLatin1Char('/') + name;
        if (fullName.startsWith(QLatin1String("./")))
            fullName = fullName.mid(2);

        bool valid = false;
        bool isDir = false;
        QString nameError;
        const QString normalized = normalizeName(fullName, valid, nameError, isDir);
        if (!valid && !isDir) {
            result.error = nameError;
            return result;
        }

        // Reset the remembered long name / pax values after use.
        pendingLongName.clear();
        pendingPax = PaxValues{};

        if (typeflag == '5') {  // directory: created on demand below
            continue;
        }
        if (typeflag == '2') {  // symbolic link
            QString target = linkName;
            if (target.isEmpty())
                target = pendingPax.linkpath;
            // Only simple, relative, in-tree links are created. The llama.cpp
            // macOS tarballs link e.g. `libggml.dylib -> libggml.0.dylib`.
            if (target.startsWith(QLatin1Char('/')) || target.contains(QLatin1String(".."))) {
                result.warning = QObject::tr("Skipped unsafe symlink %1").arg(normalized);
                continue;
            }
            if (normalized.isEmpty())
                continue;
            const QString duplicateKey = normalized.toLower();
            if (written.contains(duplicateKey)) {
                result.error = QStringLiteral("duplicate path in archive: %1").arg(normalized);
                return result;
            }
            written.insert(duplicateKey);
            const QString dest = QDir(destDir).filePath(normalized);
            if (!QDir().mkpath(QFileInfo(dest).absolutePath())) {
                result.error = QObject::tr("unable to create directory for %1").arg(dest);
                return result;
            }
            if (QFile::exists(dest))
                QFile::remove(dest);
            if (QFile::link(target, dest))
                continue;
            // Some platforms need a cleanup of the half-created link.
            result.warning = QObject::tr("Unable to create symlink %1 (skipped)").arg(normalized);
            continue;
        }
        if (typeflag == '1') {  // hardlink
            result.warning = QObject::tr("Skipped hardlink %1").arg(normalized);
            continue;
        }
        if (typeflag != '0' && typeflag != 0 && typeflag != '7') {
            // char/block devices, fifos: not needed for llama.cpp; skip.
            continue;
        }

        // Regular file.
        if (normalized.isEmpty()) {
            result.error = QStringLiteral("empty tar entry name");
            return result;
        }
        if (result.fileCount >= kMaxFiles) {
            result.error = QObject::tr("archive contains too many files");
            return result;
        }
        if (result.totalBytes > kMaxTotalBytes - fileSize) {
            result.error = QObject::tr("archive total uncompressed size exceeds limit");
            return result;
        }
        const QString duplicateKey = normalized.toLower();
        if (written.contains(duplicateKey)) {
            result.error = QStringLiteral("duplicate path in archive: %1").arg(normalized);
            return result;
        }
        written.insert(duplicateKey);

        const QString dest = QDir(destDir).filePath(normalized);
        if (!QDir().mkpath(QFileInfo(dest).absolutePath())) {
            result.error = QObject::tr("unable to create directory for %1").arg(dest);
            return result;
        }
        QFile output(dest);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.error = QObject::tr("unable to write %1").arg(dest);
            return result;
        }
        if (fileSize > 0 && output.write(payload) != fileSize) {
            output.close();
            result.error = QObject::tr("unable to write %1").arg(dest);
            return result;
        }
        if (!output.flush()) {
            output.close();
            result.error = QObject::tr("unable to write %1").arg(dest);
            return result;
        }
        output.close();

#ifdef Q_OS_UNIX
        if ((mode & 0100u)) {
            QFile permissions(dest);
            permissions.setPermissions(permissions.permissions()
                                       | QFileDevice::ExeUser | QFileDevice::ExeGroup
                                       | QFileDevice::ExeOther);
        }
#endif
        ++result.fileCount;
        result.totalBytes += fileSize;
    }

    result.ok = true;
    return result;
}

}  // namespace llocr
