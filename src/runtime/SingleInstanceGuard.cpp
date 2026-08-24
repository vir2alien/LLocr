#include "runtime/SingleInstanceGuard.h"

namespace llocr {

SingleInstanceGuard::SingleInstanceGuard(QString lockFilePath, QObject *parent)
    : QObject(parent)
    , m_lockFile(lockFilePath)
{
}

bool SingleInstanceGuard::tryAcquire(QString &errorMessage)
{
    if (m_holdsLock) {
        errorMessage.clear();
        return true;
    }
    if (m_lockFile.tryLock(0)) {
        m_holdsLock = true;
        errorMessage.clear();
        return true;
    }
    errorMessage = tr("Another LLocr instance is already running; "
                      "local server operations are disabled.");
    return false;
}

void SingleInstanceGuard::release()
{
    if (m_holdsLock) {
        m_lockFile.unlock();
        m_holdsLock = false;
    }
}

}  // namespace llocr