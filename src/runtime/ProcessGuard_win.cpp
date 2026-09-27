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
            ::SetInformationJobObject(jobHandle, JobObjectExtendedLimitInformation, &info, sizeof(info));
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
    HANDLE child = ::OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, false, static_cast<DWORD>(proc.processId()));
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

bool ProcessGuard::isProcessAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return false;
    DWORD code = 0;
    const bool alive = ::GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    ::CloseHandle(process);
    return alive;
}

QString ProcessGuard::processImagePath(qint64 pid)
{
    if (pid <= 0)
        return {};
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return {};
    wchar_t buffer[MAX_PATH] = {};
    DWORD size = MAX_PATH;
    QString path;
    if (::QueryFullProcessImageNameW(process, 0, buffer, &size))
        path = QString::fromWCharArray(buffer, static_cast<int>(size));
    ::CloseHandle(process);
    return path;
}

bool ProcessGuard::terminateProcess(qint64 pid)
{
    if (pid <= 0)
        return false;
    HANDLE process = ::OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!process)
        return false;
    const bool ok = ::TerminateProcess(process, 0) != 0;
    ::CloseHandle(process);
    return ok;
}

}  // namespace llocr