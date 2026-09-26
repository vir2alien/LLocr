#include <sys/prctl.h>
#include <csignal>

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>

#include "runtime/ProcessGuard.h"

namespace llocr {

void ProcessGuard::install(QProcess &proc)
{
    proc.setChildProcessModifier([]() {
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
    });
}

void ProcessGuard::attachParent(QProcess &)
{
    // The PDEATHSIG modifier already armed the child pre-exec.
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
    const QString link = QStringLiteral("/proc/%1/exe").arg(pid);
    const QFileInfo info(link);
    // /proc/<pid>/exe is a symlink to the binary; the link target is the image.
    return info.symLinkTarget();
}

bool ProcessGuard::terminateProcess(qint64 pid)
{
    if (pid <= 0)
        return false;
    return ::kill(static_cast<pid_t>(pid), SIGTERM) == 0;
}

}  // namespace llocr