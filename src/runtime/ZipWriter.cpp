#include "runtime/ZipWriter.h"

#include <QBuffer>
#include <QByteArray>
#include <QDateTime>
#include <QIODevice>

#include <zlib.h>

namespace llocr {

namespace {

constexpr quint32 kLocalSig = 0x04034b50u;
constexpr quint32 kCentralSig = 0x02014b50u;
constexpr quint32 kEocdSig = 0x06054b50u;
constexpr quint16 kUtf8Flag = 0x0800;
constexpr qint64 kChunkSize = 64 * 1024;
constexpr qint64 kMaxEntryBytes = 0xFFFFFFFFll;  // sizes are 32-bit fields

void putU16(QByteArray *out, quint16 value)
{
    out->append(char(value & 0xFF));
    out->append(char(value >> 8));
}

void putU32(QByteArray *out, quint32 value)
{
    out->append(char(value & 0xFF));
    out->append(char((value >> 8) & 0xFF));
    out->append(char((value >> 16) & 0xFF));
    out->append(char((value >> 24) & 0xFF));
}

quint16 dosTime(const QDateTime &now)
{
    return quint16(now.time().hour() << 11 | now.time().minute() << 5 | now.time().second() / 2);
}

quint16 dosDate(const QDateTime &now)
{
    return quint16((now.date().year() - 1980) << 9 | now.date().month() << 5 | now.date().day());
}

bool validEntryName(const QString &name)
{
    if (name.isEmpty() || name.startsWith(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return false;
    const QStringList parts = name.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String(".."))
            return false;
    }
    return true;
}

}  // namespace

ZipWriter::ZipWriter(const QString &path, QString *error)
{
    m_out.setFileName(path);
    if (!m_out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = QStringLiteral("cannot create %1").arg(path);
        m_failed = true;
        if (error)
            *error = m_error;
    }
}

ZipWriter::~ZipWriter()
{
    if (!m_finished && m_out.isOpen())
        abortFile();
}

void ZipWriter::abortFile()
{
    m_out.close();
    m_out.remove();
}

bool ZipWriter::addEntry(const QString &entryName, QIODevice &device)
{
    if (m_failed || m_finished) {
        if (m_error.isEmpty())
            m_error = QStringLiteral("archive is not writable");
        return false;
    }
    if (!validEntryName(entryName)) {
        m_error = QStringLiteral("invalid zip entry name: %1").arg(entryName);
        m_failed = true;
        return false;
    }
    if (!device.isOpen() || !(device.openMode() & QIODevice::ReadOnly)) {
        m_error = QStringLiteral("entry source is not readable: %1").arg(entryName);
        m_failed = true;
        return false;
    }
    return streamEntry(entryName, device);
}

bool ZipWriter::addEntry(const QString &entryName, const QByteArray &bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("entry source is not readable: %1").arg(entryName);
        return false;
    }
    return addEntry(entryName, buffer);
}

bool ZipWriter::streamEntry(const QString &entryName, QIODevice &device)
{
    const QByteArray name = entryName.toUtf8();
    if (name.size() > 0xFFFF) {
        m_error = QStringLiteral("zip entry name too long: %1").arg(entryName);
        return false;
    }

    Entry entry;
    entry.name = entryName;
    entry.offset = m_out.pos();

    const QDateTime now = QDateTime::currentDateTime();
    QByteArray header;
    header.reserve(30);
    putU32(&header, kLocalSig);
    putU16(&header, 20);  // version needed
    putU16(&header, kUtf8Flag);
    putU16(&header, 0);  // method: store
    putU16(&header, dosTime(now));
    putU16(&header, dosDate(now));
    putU32(&header, 0);  // crc32, patched below
    putU32(&header, 0);  // compressed size, patched below
    putU32(&header, 0);  // uncompressed size, patched below
    putU16(&header, quint16(name.size()));
    putU16(&header, 0);  // extra length
    if (m_out.write(header) != header.size() || m_out.write(name) != name.size()) {
        m_error = QStringLiteral("write failed at %1").arg(entryName);
        m_failed = true;
        return false;
    }

    quint32 crc = crc32(0L, Z_NULL, 0);
    qint64 total = 0;
    while (true) {
        const QByteArray chunk = device.read(kChunkSize);
        if (chunk.isEmpty())
            break;
        crc = crc32(crc, reinterpret_cast<const Bytef *>(chunk.constData()), static_cast<uInt>(chunk.size()));
        if (m_out.write(chunk) != chunk.size()) {
            m_error = QStringLiteral("write failed at %1").arg(entryName);
            m_failed = true;
            return false;
        }
        total += chunk.size();
        if (total > kMaxEntryBytes) {
            m_error = QStringLiteral("entry larger than 4 GiB: %1").arg(entryName);
            m_failed = true;
            return false;
        }
    }

    if (!device.atEnd()) {
        m_error = QStringLiteral("read failed at %1: %2").arg(entryName, device.errorString());
        m_failed = true;
        return false;
    }

    entry.crc = crc;
    entry.size = total;
    m_entries.append(entry);

    QByteArray patch;
    putU32(&patch, crc);
    putU32(&patch, quint32(total));
    putU32(&patch, quint32(total));
    if (m_out.seek(entry.offset + 14) && m_out.write(patch) == patch.size() && m_out.seek(m_out.size())) {
        return true;
    }
    m_error = QStringLiteral("write failed at %1").arg(entryName);
    m_failed = true;
    return false;
}

bool ZipWriter::finish(QString *error)
{
    if (m_finished)
        return true;
    if (m_failed || !m_out.isOpen()) {
        if (m_error.isEmpty())
            m_error = QStringLiteral("archive is not writable");
        abortFile();
        if (error)
            *error = m_error;
        return false;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 centralOffset = m_out.pos();
    for (const Entry &entry : std::as_const(m_entries)) {
        const QByteArray name = entry.name.toUtf8();
        QByteArray header;
        header.reserve(46);
        putU32(&header, kCentralSig);
        putU16(&header, (3 << 8) | 20);  // made by: Unix, 2.0
        putU16(&header, 20);             // version needed
        putU16(&header, kUtf8Flag);
        putU16(&header, 0);  // method: store
        putU16(&header, dosTime(now));
        putU16(&header, dosDate(now));
        putU32(&header, entry.crc);
        putU32(&header, quint32(entry.size));
        putU32(&header, quint32(entry.size));
        putU16(&header, quint16(name.size()));
        putU16(&header, 0);               // extra length
        putU16(&header, 0);               // comment length
        putU16(&header, 0);               // disk number
        putU16(&header, 0);               // internal attributes
        putU32(&header, 0100644u << 16);  // external attributes: regular file, rw-r--r--
        putU32(&header, quint32(entry.offset));
        if (m_out.write(header) != header.size() || m_out.write(name) != name.size()) {
            m_error = QStringLiteral("write failed in central directory");
            m_failed = true;
            abortFile();
            if (error)
                *error = m_error;
            return false;
        }
    }
    const qint64 centralSize = m_out.pos() - centralOffset;

    QByteArray eocd;
    eocd.reserve(22);
    putU32(&eocd, kEocdSig);
    putU16(&eocd, 0);  // disk number
    putU16(&eocd, 0);  // disk with central directory
    putU16(&eocd, quint16(m_entries.size()));
    putU16(&eocd, quint16(m_entries.size()));
    putU32(&eocd, quint32(centralSize));
    putU32(&eocd, quint32(centralOffset));
    putU16(&eocd, 0);  // comment length
    if (m_out.write(eocd) != eocd.size() || !m_out.flush()) {
        m_error = QStringLiteral("write failed in archive trailer");
        m_failed = true;
        abortFile();
        if (error)
            *error = m_error;
        return false;
    }

    m_out.close();
    m_finished = true;
    return true;
}

}  // namespace llocr
