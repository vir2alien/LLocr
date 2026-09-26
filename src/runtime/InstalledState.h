#pragma once

#include <QObject>
#include <memory>

#include "app/SettingsStore.h"
#include "runtime/RuntimePaths.h"

class QLockFile;

namespace llocr {

/// The one place that answers "where is the runtime, and where is the lock?".
///
/// Before this, `RuntimeInstaller` captured `RuntimePaths` **and** its
/// `QLockFile` in its constructor while `ModelInstaller`,
/// `ModelInstallTransaction` and `RuntimeController` built fresh paths on every
/// call — so moving the runtime directory left the installer downloading and
/// locking in the old tree while `rescanInstalledBuilds()` already looked in the
/// new one (ADR 109). Paths here are always derived from the current settings,
/// and the lock file is rebound when they change.
class InstalledState : public QObject
{
    Q_OBJECT

public:
    explicit InstalledState(SettingsStore &settings, QObject *parent = nullptr);

    /// The current paths. Never a copy frozen at construction.
    RuntimePaths paths() const;

    /// The install lock, bound to the *current* runtime directory.
    QLockFile &installLock();

    /// Creates the runtime, models, cache and profile directories.
    void ensureDirectories();

signals:
    /// The runtime or models directory changed in the settings.
    void pathsChanged();

private:
    void rebindLock();

private:
    SettingsStore &m_settings;
    // QLockFile's file name is fixed at construction, so the lock is recreated
    // when the runtime directory moves.
    std::unique_ptr<QLockFile> m_installLock;
    QString m_lockPath;
};

}  // namespace llocr
