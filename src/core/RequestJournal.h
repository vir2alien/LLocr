#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

namespace llocr {

// The client-side trace of the requests the app sent. The managed runtime has
// a server process whose output is the log; an external server belongs to
// someone else and writes nothing the app can read, so the app keeps its own
// trace (ADR 228): one record per HTTP call with the metadata and a short
// excerpt of the reply.
//
// The trace never carries the API key — the key lives in the Authorization
// header, and the record holds the URL, the sizes and the status. It stays in
// memory: a recognized page is the user's document, and writing it to disk
// would put the document into a file nobody asked for.
class RequestJournal : public QObject
{
    Q_OBJECT

public:
    struct Record {
        QDateTime at;
        QString endpoint;  ///< the request's URL, without the key
        QString model;     ///< the alias the request named
        int requestBytes = 0;
        int status = 0;  ///< HTTP status, 0 when the request never completed
        int responseBytes = 0;
        qint64 durationMs = 0;
        QString error;    ///< the transport error or the server's own message
        QString excerpt;  ///< the reply's head, capped
    };

    static RequestJournal &instance();

    void record(const Record &record);

    QStringList lines() const;
    int count() const;
    void clear();

    // The reply's head for the trace: the answer text when the server sent
    // one, the server's error message when it sent an error, the raw body
    // otherwise. Capped, so a long page does not fill the view.
    static QString excerptOf(const QByteArray &body);
    static QStringList format(const Record &record);

signals:
    void changed();

private:
    RequestJournal() = default;

    mutable QMutex m_mutex;
    QList<Record> m_records;
    static constexpr int kMaxRecords = 500;
    static constexpr int kExcerptLimit = 1200;
};

}  // namespace llocr
