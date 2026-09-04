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

} // namespace llocr
