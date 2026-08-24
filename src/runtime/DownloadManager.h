#pragma once

#include <QAbstractListModel>
#include <QList>

#include "runtime/DownloadTask.h"

class QNetworkAccessManager;

namespace llocr {

// A bounded queue of resumable downloads (§ Stage C task 3). At most two run
// concurrently; the rest wait in `Queued`. Exposes the list as a
// QAbstractListModel (name, targetDir, size, received, speed, eta, state,
// error) and aggregates progress for a single status line.
class DownloadManager : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        TargetDirRole,
        TotalBytesRole,
        ReceivedBytesRole,
        SpeedRole,
        EtaRole,
        StateRole,
        ErrorRole,
    };
    Q_ENUM(Role)

    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager() override;

    /// Adds a download to the queue and returns its row. It starts immediately
    /// if a slot is free, otherwise waits for one to finish.
    int enqueue(const DownloadTask::Request &request);

    void cancel(int row, bool deletePartial);
    void pause(int row);
    void cancelAll(bool deletePartial);
    void pauseAll();

    DownloadTask *taskAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Aggregated progress across all tasks.
    qint64 totalBytes() const { return m_totalBytes; }
    qint64 receivedBytes() const { return m_receivedBytes; }
    int speedBytesPerSec() const { return m_speedBps; }
    int etaSec() const { return m_etaSec; }

    /// Loopback http is rejected by default (§7.3); tests opt in.
    void setAllowLoopbackHttp(bool allow);
    /// Inject the free-space probe (tests); defaults to QStorageInfo.
    void setFreeBytesQuery(DownloadTask::FreeBytesQuery query);

signals:
    void progressChanged();

private:
    void startNextQueued();
    void recalcAggregate();
    void onTaskFinished(DownloadTask *task, bool ok);
    int countRunning() const;

    QNetworkAccessManager *m_nam = nullptr;
    QList<DownloadTask *> m_tasks;
    int m_maxParallel = 2;
    bool m_allowLoopbackHttp = false;
    DownloadTask::FreeBytesQuery m_freeBytesQuery;

    qint64 m_totalBytes = 0;
    qint64 m_receivedBytes = 0;
    int m_speedBps = 0;
    int m_etaSec = 0;
};

}  // namespace llocr