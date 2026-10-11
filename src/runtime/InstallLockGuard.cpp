#include "runtime/InstallLockGuard.h"

#include <QLockFile>

namespace llocr {

InstallLockGuard::~InstallLockGuard()
{
    release();
}

bool InstallLockGuard::tryLock()
{
    QLockFile &lock = m_provider();
    if (m_held) {
        if (m_lockedFile == lock.fileName())
            return true;
        // installLock() rebound to a new path and released the QLockFile we
        // held; the hold state starts over on the new lock.
        m_held = false;
        m_lockedFile.clear();
    }
    m_held = lock.tryLock(0);
    m_lockedFile = m_held ? lock.fileName() : QString();
    return m_held;
}

bool InstallLockGuard::held() const
{
    return m_held && m_lockedFile == m_provider().fileName();
}

void InstallLockGuard::release()
{
    if (!m_held)
        return;
    QLockFile &lock = m_provider();
    if (m_lockedFile == lock.fileName())
        lock.unlock();
    m_held = false;
    m_lockedFile.clear();
}

}  // namespace llocr
