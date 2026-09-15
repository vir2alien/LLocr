#pragma once

#include <QObject>

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
    double progress() const;

signals:
    void progressChanged();
    void allFinished(bool ok);

private:
    void onOneFinished(bool ok);

    DownloadManager *m_manager = nullptr;
    int m_count = 0;
    int m_done = 0;
    bool m_failed = false;
};

}  // namespace llocr
