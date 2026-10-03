#pragma once

#include <QList>
#include <QObject>

#include "runtime/DownloadTask.h"

class QNetworkAccessManager;

namespace llocr {

class DownloadManager : public QObject
{
    Q_OBJECT

public:
    explicit DownloadManager(QObject *parent = nullptr);
    ~DownloadManager() override;

    int enqueue(const DownloadTask::Request &request);

    void cancelAll(bool deletePartial);

    DownloadTask *taskAt(int row) const;
    int taskCount() const { return m_tasks.size(); }

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