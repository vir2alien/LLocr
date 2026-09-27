#pragma once

#include <memory>
#include <QObject>

#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"

class QLockFile;

namespace llocr {

class InstalledState : public QObject
{
    Q_OBJECT

public:
    explicit InstalledState(SettingsStore &settings, QObject *parent = nullptr);
    RuntimePaths paths() const;
    QLockFile &installLock();
    void ensureDirectories();

signals:
    void pathsChanged();

private:
    void rebindLock();

private:
    SettingsStore &m_settings;
    std::unique_ptr<QLockFile> m_installLock;
    QString m_lockPath;
};

}  // namespace llocr
