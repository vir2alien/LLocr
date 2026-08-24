// Windows strong no-orphan: every child is placed into one Job Object created
// by our own process, with JOB_OBJECT_LIMIT_KILL_ON_CLOSE set. When the GUI
// process dies (even abnormally) the last handle to the job closes and the OS
// terminates the whole tree (ADR 30, §5.4). The child is assigned after it is
// launched — QProcess exposes the native handle only through the process id,
// which we open and assign once the process exists.
#include <windows.h>

#include <QCoreApplication>
#include <QDebug>
#include <QProcess>

#include "runtime/ProcessGuard.h"

namespace llocr {

namespace {
HANDLE jobHandle = nullptr;

HANDLE ensureJob()
{
    if (!jobHandle) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_CLOSE;
        jobHandle = ::CreateJobObject(nullptr, nullptr);
        if (jobHandle)
            ::SetInformationJobObject(jobHandle, JobObjectExtendedLimitInformation,
                                      &info, sizeof(info));
        // Deliberately never closed: killing the job's pedigree requires the
        // handle to survive until the GUI process itself exits.
    }
    return jobHandle;
}
}  // namespace

void ProcessGuard::install(QProcess &proc)
{
    // A child-creation modifier is not required: assignment happens in
    // attachParent() after the child exists. Keep the job warm here.
    (void)proc;
    ensureJob();
}

void ProcessGuard::attachParent(QProcess &proc)
{
    const HANDLE job = ensureJob();
    if (!job)
        return;
    // The launched process handle is not exposed by QProcess; open it from the
    // pid. PROCESS_SET_QUOTA is required for AssignProcessToJobObject.
    HANDLE child = ::OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, false,
                                 static_cast<DWORD>(proc.processId()));
    if (!child) {
        qWarning() << "OpenProcess(AssignToJobObject) failed:" << ::GetLastError();
        return;
    }
    if (!::AssignProcessToJobObject(job, child))
        qWarning() << "AssignProcessToJobObject failed:" << ::GetLastError();
    ::CloseHandle(child);
    // Once all children are in the job, closing the last job handle (this
    // process's exit) kills them; the job handle stays open here until exit.
}

qint64 ProcessGuard::currentPid()
{
    return QCoreApplication::applicationPid();
}

}  // namespace llocr