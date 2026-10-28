#include "runtime/DownloadGroup.h"

#include "runtime/DownloadManager.h"

#include <algorithm>

namespace llocr {

DownloadGroup::DownloadGroup(DownloadManager *manager, QObject *parent) : QObject(parent), m_manager(manager)
{
    connect(manager, &DownloadManager::progressChanged, this, &DownloadGroup::progressChanged);
}

void DownloadGroup::begin()
{
    m_count = 0;
    m_done = 0;
    m_failed = false;
    m_firstError.clear();
    m_finishedBytes = 0;
    m_tasks.clear();
}

void DownloadGroup::enqueue(const DownloadTask::Request &request)
{
    DownloadTask *task = m_manager->taskAt(m_manager->enqueue(request));
    ++m_count;
    m_tasks.append(task);
    connect(task, &DownloadTask::downloadFinished, this, [this, task](bool ok) { onOneFinished(task, ok); });
    const auto st = task->state();
    if (st == DownloadTask::State::Completed || st == DownloadTask::State::Failed || st == DownloadTask::State::Canceled)
        onOneFinished(task, st == DownloadTask::State::Completed);
}

double DownloadGroup::progress() const
{
    qint64 total = m_finishedBytes;
    qint64 received = m_finishedBytes;
    for (const QPointer<DownloadTask> &task : m_tasks) {
        if (!task)
            continue;
        const qint64 taskTotal = task->totalBytes();
        if (taskTotal <= 0)
            continue;  // size still unknown (not started yet)
        total += taskTotal;
        received += (std::min)(task->receivedBytes(), taskTotal);
    }
    if (total <= 0)
        return 0.0;
    return double(received) / double(total);
}

void DownloadGroup::onOneFinished(DownloadTask *task, bool ok)
{
    // A finished task leaves the live list: the manager may delete it, and
    // the group keeps only the size it accounted for.
    if (task)
        m_finishedBytes += qMax<qint64>(0, task->totalBytes());
    m_tasks.removeOne(task);
    ++m_done;
    if (!ok) {
        m_failed = true;
        if (task && m_firstError.isEmpty())
            m_firstError = task->error();
    }
    emit progressChanged();
    if (m_done >= m_count)
        emit allFinished(!m_failed);
}

}  // namespace llocr
