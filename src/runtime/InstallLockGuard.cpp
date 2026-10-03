#include "runtime/InstallLockGuard.h"

#include <QLockFile>

namespace llocr {

InstallLockGuard::~InstallLockGuard()
{
    release();
}

bool InstallLockGuard::tryLock()
{
    if (m_held)
        return true;
    m_held = m_lock.tryLock(0);
    return m_held;
}

void InstallLockGuard::release()
{
    if (!m_held)
        return;
    m_lock.unlock();
    m_held = false;
}

}  // namespace llocr