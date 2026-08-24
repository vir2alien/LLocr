#pragma once

#include <QLockFile>
#include <QObject>
#include <QString>

namespace llocr {

// Ensures a single running instance owns the managed-runtime operations
// (download / install / start / stop). Uses a QLockFile at
// <AppData>/LLocr/.instance.lock (passed in via the paths object). When the
// lock is already held by another instance, the app warns the user and
// disables Managed-side actions; External (querying a foreign server) keeps
// working normally.
class SingleInstanceGuard : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceGuard(QString lockFilePath, QObject *parent = nullptr);

    /// Attempts to take the lock. Returns true if this instance owns it.
    /// A second caller gets false and should surface an explanatory message.
    bool tryAcquire(QString &errorMessage);

    /// Releases the lock (only meaningful for the owner).
    void release();

    bool holdsLock() const { return m_holdsLock; }

private:
    QLockFile m_lockFile;
    bool m_holdsLock = false;
};

}  // namespace llocr