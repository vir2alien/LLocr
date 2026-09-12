#pragma once

#include <QString>
#include <QStringList>

#include "core/LaunchProfile.h"

namespace llocr {

class LaunchProfileStore;
class SettingsStore;
struct ServerCapabilities;  // NB: must match the real definition's kind (struct)

// Value type capturing the llama-server command line: the connection/model
// fields (model/mmproj/alias/host/port) still mirror their own settings, while
// every tunable argument comes from the active launch profile (§ launch
// profiles decision). toArguments() emits the core flags first, then the
// profile rows in profile order — skipping rows whose name is reserved
// (model/mmproj/alias/host/port are single-sourced above). It builds a
// shell-free argv and a shell-escaped display string via toDisplayCommand()
// for UI previews.
class ServerLaunchConfig
{
public:
    ServerLaunchConfig() = default;

    // Snapshot the connection fields from settings + the active launch
    // profile's parameter list.
    static ServerLaunchConfig fromSettings(const SettingsStore &settings,
                                           const LaunchProfileStore &launchProfiles);

    // Program to spawn (the llama-server binary) + computed arguments.
    // Never touches a shell; the program and every argument are distinct
    // QString entries (7.1).
    QStringList toArguments(const ServerCapabilities &caps) const;

    // Human-readable, shell-escaped command line for UI preview. Must never
    // reveal secrets (7.6) — none of the launch fields hold a secret, and
    // ScriptArguments is the only way raw user text enters the command.
    QString toDisplayCommand(const ServerCapabilities &caps) const;

    // Connection fields (never part of a launch profile).
    QString program;
    QString modelPath;
    QString mmprojPath;
    QString modelAlias;
    QString host = QStringLiteral("127.0.0.1");
    int port = 0;           // 0 = auto-pick (no --port passed / caller allocates)

    // Tunable arguments from the active launch profile.
    QList<LaunchParameter> parameters;
};

}  // namespace llocr
