#pragma once

#include <QList>
#include <QObject>
#include <QPointer>

#include "runtime/DownloadTask.h"

namespace llocr {

class DownloadManager;

class DownloadGroup : public QObject
{
    Q_OBJECT

public:
    explicit DownloadGroup(DownloadManager *manager, QObject *parent = nullptr);

    void begin();
    void enqueue(const DownloadTask::Request &request);

    bool failed() const { return m_failed; }
    QString firstError() const { return m_firstError; }
    double progress() const;

signals:
    void progressChanged();
    void allFinished(bool ok);

private:
    void onOneFinished(DownloadTask *task, bool ok);

private:
    DownloadManager *m_manager = nullptr;
    // The manager drops a finished task once ten of them pile up, so the
    // group must not hold a raw pointer: QPointer goes null when the task is
    // destroyed (ADR 210).
    QList<QPointer<DownloadTask>> m_tasks;
    // The bytes of the tasks that are done: the manager may delete them, and
    // the progress of the group must not fall back when it does.
    qint64 m_finishedBytes = 0;
    int m_count = 0;
    int m_done = 0;
    bool m_failed = false;
    QString m_firstError;
};

}  // namespace llocr
