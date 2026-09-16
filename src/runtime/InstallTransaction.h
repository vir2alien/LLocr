#pragma once

#include <QList>
#include <QString>

#include <functional>

#include "runtime/ReleaseAsset.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

struct InstalledBuildInfo {
    QString tag;        // install directory name, e.g. "llama.cpp-b10594-cuda-cu12-win-x64"
    QString build;      // parsed build token, e.g. "b10594"
    QString backend;    // parsed backend token ("cuda-cu12"), empty when unparsable
    QString serverPath; // absolute llama-server path; empty when not found

    bool operator==(const InstalledBuildInfo &o) const
    {
        return tag == o.tag && build == o.build && backend == o.backend
               && serverPath == o.serverPath;
    }
};

struct InstallOutput {
    bool ok = false;
    QString error;
    QString warning;
    QString build;        // e.g. "b10594"
    QString tag;          // install directory tag, e.g. "llama.cpp-b10594-cuda-win-x64"
    QString serverPath;   // absolute path to the installed llama-server binary
};

class InstallTransaction
{
public:
    using CommitFn = std::function<void(const InstallOutput &out)>;

    static InstallOutput start(const QString &archivePath, const ReleaseAsset &asset,
                               RuntimePaths paths, CommitFn commit = CommitFn());
    static void cleanupStaging(RuntimePaths paths);
    static QString cleanupUnusedBuilds(RuntimePaths paths, const QString &keepTag);
    static QList<InstalledBuildInfo> scanInstalledBuilds(const RuntimePaths &paths);
};

}  // namespace llocr