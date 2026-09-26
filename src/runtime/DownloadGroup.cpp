#include "runtime/DownloadGroup.h"

#include "runtime/DownloadManager.h"

namespace llocr {

DownloadGroup::DownloadGroup(DownloadManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
    connect(manager, &DownloadManager::progressChanged, this,
            &DownloadGroup::progressChanged);
}

void DownloadGroup::begin()
{
    m_count = 0;
    m_done = 0;
    m_failed = false;
    m_tasks.clear();
}

void DownloadGroup::enqueue(const DownloadTask::Request &request)
{
    DownloadTask *task = m_manager->taskAt(m_manager->enqueue(request));
    ++m_count;
    m_tasks.append(task);
    connect(task, &DownloadTask::downloadFinished, this,
            [this](bool ok) { onOneFinished(ok); });
    const auto st = task->state();
    if (st == DownloadTask::State::Completed || st == DownloadTask::State::Failed
        || st == DownloadTask::State::Canceled)
        onOneFinished(st == DownloadTask::State::Completed);
}

double DownloadGroup::progress() const
{
    // Sum over *this group's* tasks, not the manager's aggregate: the manager is
    // shared with every other group and keeps up to ten terminal tasks, so a
    // second install used to be diluted by the first one's leftovers and the bar
    // stalled below 100 % (ADR 107).
    qint64 total = 0;
    qint64 received = 0;
    for (const DownloadTask *task : m_tasks) {
        if (!task)
            continue;
        const qint64 taskTotal = task->totalBytes();
        if (taskTotal <= 0)
            continue;  // size still unknown (not started yet)
        total += taskTotal;
        received += qMin(task->receivedBytes(), taskTotal);
    }
    if (total <= 0)
        return 0.0;
    return double(received) / double(total);
}

void DownloadGroup::onOneFinished(bool ok)
{
    ++m_done;
    if (!ok)
        m_failed = true;
    emit progressChanged();
    if (m_done >= m_count)
        emit allFinished(!m_failed);
}

}  // namespace llocr
