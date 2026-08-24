#pragma once

#include <QString>
#include <QStringList>

#include "app/SettingsStore.h"
#include "runtime/ServerCapabilities.h"

namespace llocr {

// Value type capturing the editable launch parameters (§4.1 `launch/*`).
// It builds a command-line for llama-server via toArguments(), omitting
// flags the target binary does not support (see ServerCapabilities), and a
// shell-escaped display string via toDisplayCommand() for UI previews.
class ServerLaunchConfig
{
public:
    ServerLaunchConfig() = default;

    // Snapshot the current launch/* settings.
    static ServerLaunchConfig fromSettings(const SettingsStore &settings);

    // Program to spawn (the llama-server binary) + computed arguments.
    // Never touches a shell; the program and every argument are distinct
    // QString entries (7.1).
    QStringList toArguments(const ServerCapabilities &caps) const;

    // Human-readable, shell-escaped command line for UI preview. Must never
    // reveal secrets (7.6) — none of the launch fields hold a secret, and
    // ScriptArguments is the only way raw user text enters the command.
    QString toDisplayCommand(const ServerCapabilities &caps) const;

    // Fields mirroring settings keys; public setters used by tests/UI.
    QString program;
    QString modelPath;
    QString mmprojPath;
    QString modelAlias;
    QString host = QStringLiteral("127.0.0.1");
    int port = 0;           // 0 = auto-pick (no --port passed / caller allocates)
    int ctxSize = 8192;
    int gpuLayers = -1;     // -1 = do not pass
    int threads = 0;        // 0 = do not pass
    int batchSize = 0;      // 0 = do not pass
    int parallel = 1;       // >0 always passed
    QString flashAttn = QStringLiteral("off");
    QString cacheTypeK;     // empty = do not pass
    QString cacheTypeV;     // empty = do not pass
    bool noMmap = false;
    bool jinja = false;
    // Extra raw argv entries (already split by the caller; see §4.1
    // extraArgs). Appended verbatim; only display-escaped, never re-parsed.
    QStringList extraArgs;

    /// Splits a shell-like `extraArgs` string honoring double quotes. This is
    /// the single allowed point where free-form user text enters the argv as
    /// distinct tokens; it is NOT re-split anywhere downstream.
    static QStringList parseExtraArgs(const QString &text);
};

}  // namespace llocr