#pragma once

#include <QProcess>

namespace llocr {

// Platform-specific binding of a child process to the lifetime of the parent
// GUI (§5.4). The guarantee differs per OS:
//   - Windows: Job Object with JOB_OBJECT_LIMIT_KILL_ON_CLOSE (strong).
//   - Linux:   prctl(PR_SET_PDEATHSIG, SIGTERM) (strong).
//   - macOS:   no PDEATHSIG analog → best-effort only (see §5.4 / Stage H
//              watchdog). owner.json + next-start detection complements it.
//
// Pid (int) and the process name are enough for the macOS owner.json record;
// on Windows/Linux attachParent() is where the job/death signal is actually
// armed, immediately after the process has started.
class ProcessGuard
{
public:
    // Installs the OS-appropriate child-process modifier onto `proc` so the
    // freshly forked/created child is bound to this process's lifetime.
    static void install(QProcess &proc);

    // Called right after the process has started (all platforms). On Windows
    // this assigns it to our Job Object (strong kill-on-close). On Linux the
    // PDEATHSIG modifier already armed the child, so this is a no-op. On macOS
    // there is no death signal — the best-effort owner.json handshake is
    // managed by the caller, not here.
    static void attachParent(QProcess &proc);

    // The PID of the current process; used by macOS owner.json correlation.
    static qint64 currentPid();
};

}  // namespace llocr