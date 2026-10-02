#pragma once

#include <QAbstractListModel>
#include <QList>

#include "runtime/DownloadTask.h"

class QNetworkAccessManager;

namespace llocr {

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

    int enqueue(const DownloadTask::Request &request);

    void cancel(int row, bool deletePartial);
    void cancelAll(bool deletePartial);

    DownloadTask *taskAt(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setAllowLoopbackHttp(bool allow);
    void setFreeBytesQuery(DownloadTask::FreeBytesQuery query);

signals:
    void progressChanged();

private:
    void startNextQueued();
    void emitProgress();
    void onTaskFinished(DownloadTask *task, bool ok);
    void evictFinishedTasks();
    int countRunning() const;

private:
    QNetworkAccessManager *m_nam = nullptr;
    QList<DownloadTask *> m_tasks;
    int m_maxParallel = 2;
    bool m_allowLoopbackHttp = false;
    DownloadTask::FreeBytesQuery m_freeBytesQuery;
};

}  // namespace llocr