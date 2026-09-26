#pragma once

#include <QString>

namespace llocr {

/// The record LlamaServerProcess writes next to the runtime directory while a
/// managed server is running (`<rootDir>/owner.json`). On platforms without a
/// launch-time guard (macOS) it is the only way to find a server that outlived
/// its app — the record used to be written and cleared but never read back
/// (ADR 30/47 promised the next-start detection, ADR 107 implements it).
struct ServerOwnerRecord {
    qint64 pid = 0;        ///< llama-server process id
    qint64 parentPid = 0;  ///< the LLocr process that started it
    int port = 0;          ///< port it was serving on
    QString program;       ///< absolute path of the binary

    bool isValid() const { return pid > 0; }
};

class ServerOwner
{
public:
    /// Reads the record; returns an invalid record when the file is missing or
    /// malformed. A half-written file is treated as "nothing to clean up".
    static ServerOwnerRecord read(const QString &ownerJsonPath);

    /// True when the recorded process is still running *and* is plausibly the
    /// server we started: the image must match, or — when the platform cannot
    /// report it — the recorded parent must be gone. A recycled pid is therefore
    /// never reported, and never killed.
    static bool isOrphan(const ServerOwnerRecord &record);

    /// Convenience: read + isOrphan in one step.
    static bool findOrphan(const QString &ownerJsonPath, ServerOwnerRecord *out);

    /// Removes the record file. Called after a successful termination.
    static void clear(const QString &ownerJsonPath);
};

}  // namespace llocr
