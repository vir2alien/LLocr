#pragma once

#include <QCoreApplication>
#include <QString>

namespace llocr {

// Resolves the on-disk layout for the managed runtime, models, cache and logs
// (§3). The default root is the platform AppData location; both the root and
// the models directory can be overridden from settings. When an override path
// changes, existing files are NOT moved — the UI warns and offers a rescan.
class RuntimePaths
{
    Q_DECLARE_TR_FUNCTIONS(RuntimePaths)

public:
    explicit RuntimePaths(QString rootDir = QString(), QString modelsDir = QString());

    // Overridden values, or the platform defaults when empty.
    QString rootDir() const;
    QString modelsDir() const;

    // <rootDir>/runtime
    QString runtimeDir() const;
    // <runtimeDir>/staging
    QString stagingDir() const;
    // <runtimeDir>/<tag>  (e.g. "llama.cpp-b10594-cuda-win-x64")
    QString installDir(const QString &tag) const;
    // <rootDir>/logs
    QString logsDir() const;
    // <rootDir>/cache
    QString cacheDir() const;
    // <modelsDir>/<org>__<repo>
    QString modelDir(const QString &repo) const;
    // <rootDir>/.instance.lock  (SingleInstanceGuard — runtime-owner lock)
    QString instanceLockPath() const;
    // <runtimeDir>/.install.lock  (guards runtime installs across instances, H.6)
    QString installLockPath() const;
    // <rootDir>/logs/llama-server.log
    QString serverLogPath() const;

    /// Creates root/runtime/models/staging/cache/logs as needed. Returns an
    /// empty string on success, or a human-readable error message otherwise.
    QString ensureDirectories() const;

    /// Default app-data root for this platform, used when no override is set.
    static QString defaultRootDir();

private:
    QString m_rootDir;
    QString m_modelsDir;
};

}  // namespace llocr