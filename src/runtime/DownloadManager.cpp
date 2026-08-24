#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QStorageInfo>

#include "runtime/DownloadManager.h"
#include "runtime/DownloadTask.h"

namespace llocr {

DownloadManager::DownloadManager(QObject *parent)
    : QAbstractListModel(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_freeBytesQuery([](const QString &dirPath) {
          return QStorageInfo(dirPath).bytesAvailable();
      })
{
    QNetworkProxyFactory::setUseSystemConfiguration(true);
}

DownloadManager::~DownloadManager()
{
    // Stop and detach every task before our own members are destroyed. The
    // QNetworkReply objects are children of m_nam, which is deleted in the
    // QObject base destructor (after this destructor body) — by then our
    // m_tasks/m_nam members would be gone, so any re-entrant finished() signal
    // must not reach callbacks on `this`.
    for (DownloadTask *task : m_tasks) {
        task->abortDownload();
        task->disconnect(this);
    }
}

int DownloadManager::enqueue(const DownloadTask::Request &request)
{
    auto *task = new DownloadTask(request, m_nam, this);
    task->setAllowLoopbackHttp(m_allowLoopbackHttp);
    task->setFreeBytesQuery(m_freeBytesQuery);

    const int row = m_tasks.size();
    beginInsertRows(QModelIndex(), row, row);
    m_tasks.append(task);
    endInsertRows();

    connect(task, &DownloadTask::stateChanged, this, [this, task]() {
        const int r = m_tasks.indexOf(task);
        if (r >= 0)
            emit dataChanged(index(r), index(r), {StateRole, ErrorRole});
    });
    connect(task, &DownloadTask::progressChanged, this, [this, task]() {
        const int r = m_tasks.indexOf(task);
        if (r >= 0)
            emit dataChanged(index(r), index(r),
                             {ReceivedBytesRole, TotalBytesRole, SpeedRole, EtaRole});
        recalcAggregate();
    });
    connect(task, &DownloadTask::downloadFinished, this,
            [this, task](bool ok) { onTaskFinished(task, ok); });

    recalcAggregate();
    startNextQueued();
    return row;
}

void DownloadManager::setAllowLoopbackHttp(bool allow)
{
    m_allowLoopbackHttp = allow;
    for (DownloadTask *task : m_tasks)
        task->setAllowLoopbackHttp(allow);
}

void DownloadManager::setFreeBytesQuery(DownloadTask::FreeBytesQuery query)
{
    if (query)
        m_freeBytesQuery = std::move(query);
    for (DownloadTask *task : m_tasks)
        task->setFreeBytesQuery(m_freeBytesQuery);
}

void DownloadManager::cancel(int row, bool deletePartial)
{
    if (row < 0 || row >= m_tasks.size())
        return;
    m_tasks.at(row)->cancel(deletePartial);
}

void DownloadManager::pause(int row)
{
    if (row < 0 || row >= m_tasks.size())
        return;
    m_tasks.at(row)->pause();
}

void DownloadManager::cancelAll(bool deletePartial)
{
    for (DownloadTask *task : m_tasks)
        task->cancel(deletePartial);
}

void DownloadManager::pauseAll()
{
    for (DownloadTask *task : m_tasks)
        task->pause();
}

DownloadTask *DownloadManager::taskAt(int row) const
{
    if (row < 0 || row >= m_tasks.size())
        return nullptr;
    return m_tasks.at(row);
}

int DownloadManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_tasks.size();
}

QVariant DownloadManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tasks.size())
        return {};
    const DownloadTask *task = m_tasks.at(index.row());
    switch (role) {
    case NameRole:
        return task->fileName();
    case TargetDirRole:
        return task->targetDir();
    case TotalBytesRole:
        return QVariant::fromValue<qlonglong>(task->totalBytes());
    case ReceivedBytesRole:
        return QVariant::fromValue<qlonglong>(task->receivedBytes());
    case SpeedRole:
        return task->speedBytesPerSec();
    case EtaRole:
        return task->etaSec();
    case StateRole:
        return static_cast<int>(task->state());
    case ErrorRole:
        return task->error();
    default:
        return {};
    }
}

QHash<int, QByteArray> DownloadManager::roleNames() const
{
    return {{NameRole, "name"},
            {TargetDirRole, "targetDir"},
            {TotalBytesRole, "totalBytes"},
            {ReceivedBytesRole, "receivedBytes"},
            {SpeedRole, "speed"},
            {EtaRole, "eta"},
            {StateRole, "state"},
            {ErrorRole, "error"}};
}

void DownloadManager::startNextQueued()
{
    while (countRunning() < m_maxParallel) {
        DownloadTask *next = nullptr;
        for (DownloadTask *task : m_tasks) {
            if (task->state() == DownloadTask::State::Queued) {
                next = task;
                break;
            }
        }
        if (!next)
            break;
        next->start();
    }
}

int DownloadManager::countRunning() const
{
    int count = 0;
    for (const DownloadTask *task : m_tasks) {
        const auto state = task->state();
        if (state == DownloadTask::State::Running || state == DownloadTask::State::Verifying)
            ++count;
    }
    return count;
}

void DownloadManager::recalcAggregate()
{
    qint64 total = 0;
    qint64 received = 0;
    int speed = 0;
    for (const DownloadTask *task : m_tasks) {
        if (task->totalBytes() > 0)
            total += task->totalBytes();
        received += task->receivedBytes();
        speed += task->speedBytesPerSec();
    }
    m_totalBytes = total;
    m_receivedBytes = received;
    m_speedBps = speed;
    m_etaSec = (speed > 0 && total > 0 && received < total)
                   ? static_cast<int>((total - received) / speed)
                   : 0;
    emit progressChanged();
}

void DownloadManager::onTaskFinished(DownloadTask *task, bool ok)
{
    Q_UNUSED(ok);
    const int row = m_tasks.indexOf(task);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {StateRole, ErrorRole});
    recalcAggregate();
    // Defer so a task that finished synchronously (free-space/mkdir failure)
    // cannot re-enter startNextQueued() and double-start its successors.
    QMetaObject::invokeMethod(this, [this]() { startNextQueued(); }, Qt::QueuedConnection);
}

}  // namespace llocr