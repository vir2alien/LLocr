// Linux strong no-orphan: the child is armed to receive SIGTERM the instant
// its parent (our GUI) exits — including on SIGKILL of the parent — via
// prctl(PR_SET_PDEATHSIG, SIGTERM). This must run in the child after fork and
// before exec, hence the QProcess child-process modifier (ADR 30, §5.4).
#include <sys/prctl.h>

#include <QCoreApplication>
#include <QProcess>

#include "runtime/ProcessGuard.h"

namespace llocr {

void ProcessGuard::install(QProcess &proc)
{
    proc.setChildProcessModifier([]() {
        // Signal ordering: parent-death fires when the *thread* that created us
        // dies. The calling thread is the one that called start(); guard against
        // the common rename/exec gap: Linux clears PDEATHSIG on exec unless
        // PR_SET_PDEATHSIG is armed first and PR_SET_CHILD_CLEAR_ON_EXEC... we
        // rely on the kernel keeping it across exec (it is preserved!). We must
        // set it again *after* we know the parent is the launcher, but the
        // child process modifier runs before execve in the child, so arming
        // here is correct and the setting survives exec.
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