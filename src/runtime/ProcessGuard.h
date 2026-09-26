#pragma once

#include <QProcess>
#include <QString>

namespace llocr {

// Platform bits around a *child* process: the launch-time guard (PDEATHSIG on
// Linux, a Job Object on Windows, nothing on macOS) and the queries the
// orphan scan needs. Keeping them here means the no-op macOS guard is the only
// platform-specific knowledge in the runtime layer.
class ProcessGuard
{
public:
    static void install(QProcess &proc);
    static void attachParent(QProcess &proc);
    static qint64 currentPid();

    // Is a process with this id running?
    static bool isProcessAlive(qint64 pid);

    // Absolute path of the running process' executable, or an empty string when
    // the platform cannot report it (used to avoid acting on a recycled pid).
    static QString processImagePath(qint64 pid);

    // Asks a process to terminate (SIGTERM / TerminateProcess).
    static bool terminateProcess(qint64 pid);
};

}  // namespace llocr