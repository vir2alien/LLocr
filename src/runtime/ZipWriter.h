#pragma once

#include <QFile>
#include <QList>
#include <QString>

class QIODevice;

namespace llocr {

// Minimal ZIP writer (compression method 0 / store). The payloads a project
// container carries (PDF, PNG, JPEG, DjVu) are already compressed, so storing
// keeps the writer streamable and small; ArchiveExtractor reads the result.
class ZipWriter
{
public:
    ZipWriter(const QString &path, QString *error = nullptr);
    ~ZipWriter();

    ZipWriter(const ZipWriter &) = delete;
    ZipWriter &operator=(const ZipWriter &) = delete;

    // Streams the whole device into one entry. The device must already be open.
    bool addEntry(const QString &entryName, QIODevice &device);

    bool addEntry(const QString &entryName, const QByteArray &bytes);

    // Writes the central directory and closes the archive. The destructor of an
    // unfinished writer deletes the partial file.
    bool finish(QString *error = nullptr);

    QString error() const { return m_error; }

private:
    struct Entry {
        QString name;
        quint32 crc = 0;
        qint64 size = 0;
        qint64 offset = 0;
    };

    bool streamEntry(const QString &entryName, QIODevice &device);
    void abortFile();

    QFile m_out;
    QList<Entry> m_entries;
    QString m_error;
    bool m_finished = false;
    bool m_failed = false;
};

}  // namespace llocr
