#include <QCoreApplication>
#include <QProcess>

#include "runtime/ProcessGuard.h"

#if defined(Q_OS_MACOS)
#include <csignal>
#include <libproc.h>
#include <unistd.h>
#endif

namespace llocr {

void ProcessGuard::install(QProcess &)
{
    // No child-process modifier on macOS: there is nothing reliable to do
    // in the child. The owner.json record written next to the runtime directory
    // plus the next-start scan in ServerOwner cover it.
}

void ProcessGuard::attachParent(QProcess &)
{
    // No strong attach available;
}

qint64 ProcessGuard::currentPid()
{
    return QCoreApplication::applicationPid();
}

bool ProcessGuard::isProcessAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
    // Signal 0 performs the permission/existence check without delivering.
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
}

QString ProcessGuard::processImagePath(qint64 pid)
{
    if (pid <= 0)
        return {};
    char buffer[PROC_PIDPATHINFO_MAXSIZE] = {};
    const int length = ::proc_pidpath(static_cast<int>(pid), buffer, sizeof(buffer));
    if (length <= 0)
        return {};
    return QString::fromLocal8Bit(buffer, length);
}

bool ProcessGuard::terminateProcess(qint64 pid)
{
    if (pid <= 0)
        return false;
    return ::kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}

}  // namespace llocr