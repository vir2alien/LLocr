#include "runtime/InstalledState.h"

#include <QLockFile>

namespace llocr {

InstalledState::InstalledState(SettingsStore &settings, QObject *parent) : QObject(parent), m_settings(settings)
{
    rebindLock();
    connect(&m_settings, &SettingsStore::runtimeRootDirChanged, this, [this]() {
        rebindLock();
        emit pathsChanged();
    });
    connect(&m_settings, &SettingsStore::runtimeModelsDirChanged, this, [this]() {
        rebindLock();
        emit pathsChanged();
    });
}

RuntimePaths InstalledState::paths() const
{
    return RuntimePaths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir());
}

QLockFile &InstalledState::installLock()
{
    // A lock held by a moved runtime directory must not keep guarding the old
    // one, and the lock file name must never point somewhere we no longer
    // install. QLockFile cannot be renamed, so it is recreated.
    const QString wanted = paths().installLockPath();
    if (wanted != m_lockPath) {
        m_installLock.reset();
        m_lockPath = wanted;
        m_installLock = std::make_unique<QLockFile>(wanted);
        m_installLock->setStaleLockTime(30 * 1000);
    }
    return *m_installLock;
}

void InstalledState::ensureDirectories()
{
    paths().ensureDirectories();
}

void InstalledState::rebindLock()
{
    installLock();
}

}  // namespace llocr
