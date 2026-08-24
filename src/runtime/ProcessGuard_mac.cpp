// macOS best-effort no-orphan. There is no PDEATHSIG analog; the plan (§5.4)
// therefore guards only on:
//   - graceful exit / SIGTERM / SIGINT: child is stopped explicitly;
//   - SIGKILL of the GUI: child MAY survive — detected on next start via
//     owner.json (PID + name + port) and offered for termination, never
//     silently killed (ADR 30).
// A full kqueue/NOTE_EXIT watchdog helper is deferred to Stage H.
#include <QCoreApplication>
#include <QProcess>

#include "runtime/ProcessGuard.h"

namespace llocr {

void ProcessGuard::install(QProcess &)
{
    // No child-process modifier on macOS: there is nothing reliable to do
    // in the child. The owner.json handshake + next-start detection cover it.
}

void ProcessGuard::attachParent(QProcess &)
{
    // No strong attach available; see §5.4.
}

qint64 ProcessGuard::currentPid()
{
    return QCoreApplication::applicationPid();
}

}  // namespace llocr