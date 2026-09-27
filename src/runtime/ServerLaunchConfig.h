#pragma once

#include <QString>
#include <QStringList>

#include "core/LaunchProfile.h"
#include "runtime/ResolvedConnection.h"

namespace llocr {

class LaunchProfileStore;
class SettingsStore;
struct ServerCapabilities;

class ServerLaunchConfig
{
public:
    ServerLaunchConfig() = default;
    static ServerLaunchConfig fromSettings(const SettingsStore &settings,
                                           const LaunchProfileStore &launchProfiles,
                                           ConnectionRole role = ConnectionRole::Ocr);
    QStringList toArguments(const ServerCapabilities &caps) const;
    QString toDisplayCommand(const ServerCapabilities &caps) const;

    QString program;
    QString modelPath;
    QString mmprojPath;
    QString modelAlias;
    QString host = QStringLiteral("127.0.0.1");
    int port = 0;           // 0 = auto-pick (no --port passed / caller allocates)

    QList<LaunchParameter> parameters;

    // Value equality: the restart banner compares the configuration a running
    // server was started with against the current one (ADR 113), so a launch
    // setting that changes the arguments can never be missed.
    bool operator==(const ServerLaunchConfig &other) const;
    bool operator!=(const ServerLaunchConfig &other) const { return !(*this == other); }
};

}  // namespace llocr
