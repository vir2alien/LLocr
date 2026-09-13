#pragma once

#include <QList>
#include <QString>

#include <functional>

#include "runtime/ReleaseAsset.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

// A llama.cpp build already on disk under <runtimeDir>/llama.cpp-*, produced
// by scanInstalledBuilds(). Lets the UI offer previously downloaded builds for
// activation without a re-download (e.g. after a settings reset wiped the
// active-build keys but left the directories in place).
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

// Outcome of InstallTransaction::start().
struct InstallOutput {
    bool ok = false;
    QString error;
    QString warning;
    QString build;        // e.g. "b10594"
    QString tag;          // install directory tag, e.g. "llama.cpp-b10594-cuda-win-x64"
    QString serverPath;   // absolute path to the installed llama-server binary
};

// Runs the atomic installation of an already-downloaded llama.cpp release
// archive (§ Stage D task 5):
//
//   1. (download is performed by the caller/DownloadManager into runtime/staging)
//   2. size + sha256 verification of the archive;
//   3. extraction into runtime/staging/<uuid>/ with hardened ArchiveExtractor;
//   4. composition validation (no escape paths, expected files present);
//   5. locate the llama-server binary inside the staging tree;
//   6. probe it (--version / --help) and capture ServerCapabilities;
//   7. atomic rename of staging/<uuid> -> runtime/<finalTag>;
//   8. commit — a caller-supplied callback (writes SettingsStore) runs last,
//      after the rename, so a failure never leaves settings referencing a
//      half-installed build.
//
// Any failure removes the entire staging/<uuid> tree and leaves settings and
// the existing install dir untouched.
class InstallTransaction
{
public:
    /// Callback invoked once the new install is live and renamed; the caller
    /// uses it to persist the new paths (the commit step). No-op for tests.
    using CommitFn = std::function<void(const InstallOutput &out)>;

    static InstallOutput start(const QString &archivePath, const ReleaseAsset &asset,
                               RuntimePaths paths, CommitFn commit = CommitFn());

    /// Removes leftover staging/<uuid> trees from previous interrupted runs.
    /// Call once at application start.
    static void cleanupStaging(RuntimePaths paths);

    /// Removes installed runtime/<tag> directories except the active one
    /// (`keepTag`). Returns a human-readable summary or error.
    static QString cleanupUnusedBuilds(RuntimePaths paths, const QString &keepTag);

    /// Enumerates the llama.cpp builds present under <runtimeDir> (every
    /// `llama.cpp-*` directory, `staging` excluded), locating the llama-server
    /// binary inside each and parsing build/backend from the tag. Sorted by
    /// build number, newest first; entries without a parsable build last.
    static QList<InstalledBuildInfo> scanInstalledBuilds(const RuntimePaths &paths);
};

}  // namespace llocr