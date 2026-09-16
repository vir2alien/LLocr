#pragma once

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace llocr {

QString sanitizeFileName(const QString &name);

class DownloadTask : public QObject
{
    Q_OBJECT

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString fileName READ fileName CONSTANT)
    Q_PROPERTY(QString targetDir READ targetDir CONSTANT)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY progressChanged)
    Q_PROPERTY(qint64 receivedBytes READ receivedBytes NOTIFY progressChanged)
    Q_PROPERTY(int speedBytesPerSec READ speedBytesPerSec NOTIFY progressChanged)
    Q_PROPERTY(int etaSec READ etaSec NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)

public:
    enum class State {
        Queued,
        Running,
        Paused,
        Verifying,
        Completed,
        Canceled,
        Failed,
    };
    Q_ENUM(State)

    struct Request {
        QUrl url;
        QString targetDir;     // directory the file lands in
        QString fileName;      // desired name (sanitized internally)
        QString sha256;        // lowercase hex digest; empty = skip verification
        QString authorization; // optional "Bearer …" for the origin host
    };

    DownloadTask(const Request &request, QNetworkAccessManager *nam, QObject *parent = nullptr);
    ~DownloadTask() override;

    State state() const { return m_state; }
    QString fileName() const { return m_fileName; }
    QString targetDir() const { return m_targetDir; }
    QString partPath() const { return m_partPath; }
    qint64 totalBytes() const { return m_totalBytes; }
    qint64 receivedBytes() const { return m_receivedBytes; }
    int speedBytesPerSec() const { return m_speedBps; }
    int etaSec() const { return m_etaSec; }
    QString error() const { return m_error; }

    void start();
    void cancel(bool deletePartial);
    void abortDownload();

    void setAllowLoopbackHttp(bool allow) { m_allowLoopbackHttp = allow; }

    using FreeBytesQuery = std::function<qint64(const QString &dirPath)>;
    void setFreeBytesQuery(FreeBytesQuery query);

signals:
    void stateChanged();
    void progressChanged();
    void downloadFinished(bool ok);

private:
    QString partPathFor(const QString &final) const { return final + QStringLiteral(".part"); }
    QString metaPathFor(const QString &part) const { return part + QStringLiteral(".meta"); }

    void issueRequest();
    void onMetadata(QNetworkReply *reply);
    void onData(QNetworkReply *reply);
    void onFinished(QNetworkReply *reply);
    void handleRedirect(const QUrl &target);

    void openForFresh();
    void openForAppend();
    void restartFresh(QNetworkReply *reply);
    void closeFile();
    void updateProgress();
    void verifySha256();
    void persistMeta();
    void readMeta();
    void fail(const QString &message);
    void setState(State next);

    bool isAllowedUrl(const QUrl &url) const;

    bool hasResumeValidator() const;
    QString ifRangeValue() const;
    void hashExistingPart();

private:
    Request m_request;
    QNetworkAccessManager *m_nam = nullptr;

    QString m_targetDir;
    QString m_fileName;
    QString m_finalPath;
    QString m_partPath;
    QString m_metaPath;

    State m_state = State::Queued;
    QString m_error;

    qint64 m_totalBytes = -1;    // -1 = unknown
    qint64 m_receivedBytes = 0;
    int m_speedBps = 0;
    int m_etaSec = 0;

    QFile m_file;
    bool m_fileOpen = false;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};

    QNetworkReply *m_reply = nullptr;
    QUrl m_effectiveUrl;
    QString m_authorization;
    int m_redirectCount = 0;
    bool m_allowLoopbackHttp = false;

    qint64 m_resumeBytes = 0;
    QString m_resumeEtag;
    QString m_resumeLastModified;
    qint64 m_resumeExpectedTotal = -1;
    bool m_resumeRequested = false;
    QString m_ifRangeValue;

    bool m_cancelRequested = false;
    bool m_cancelDeletePartial = false;
    bool m_pauseRequested = false;
    bool m_abortRequested = false;

    QElapsedTimer m_speedClock;
    qint64 m_speedSampleBytes = 0;
    bool m_speedClockValid = false;

    FreeBytesQuery m_freeBytesQuery;
};

}  // namespace llocr