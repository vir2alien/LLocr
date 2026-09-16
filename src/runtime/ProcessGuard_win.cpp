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
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        jobHandle = ::CreateJobObject(nullptr, nullptr);
        if (jobHandle)
            ::SetInformationJobObject(jobHandle, JobObjectExtendedLimitInformation,
                                      &info, sizeof(info));
    }
    return jobHandle;
}
}  // namespace

void ProcessGuard::install(QProcess &proc)
{
    (void)proc;
    ensureJob();
}

void ProcessGuard::attachParent(QProcess &proc)
{
    const HANDLE job = ensureJob();
    if (!job)
        return;
    HANDLE child = ::OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, false,
                                 static_cast<DWORD>(proc.processId()));
    if (!child) {
        qWarning() << "OpenProcess(AssignToJobObject) failed:" << ::GetLastError();
        return;
    }
    if (!::AssignProcessToJobObject(job, child))
        qWarning() << "AssignProcessToJobObject failed:" << ::GetLastError();
    ::CloseHandle(child);
}

qint64 ProcessGuard::currentPid()
{
    return QCoreApplication::applicationPid();
}

}  // namespace llocr