#include <sys/prctl.h>

#include <QCoreApplication>
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

}  // namespace llocr