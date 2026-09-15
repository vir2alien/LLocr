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
}

void DownloadGroup::enqueue(const DownloadTask::Request &request)
{
    DownloadTask *task = m_manager->taskAt(m_manager->enqueue(request));
    ++m_count;
    connect(task, &DownloadTask::downloadFinished, this,
            [this](bool ok) { onOneFinished(ok); });
    const auto st = task->state();
    if (st == DownloadTask::State::Completed || st == DownloadTask::State::Failed
        || st == DownloadTask::State::Canceled)
        onOneFinished(st == DownloadTask::State::Completed);
}

double DownloadGroup::progress() const
{
    const qint64 total = m_manager->totalBytes();
    const qint64 received = m_manager->receivedBytes();
    return total > 0 ? double(received) / double(total) : 0.0;
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
