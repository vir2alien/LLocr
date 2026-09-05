#pragma once

#include <QCoreApplication>
#include <QProcess>
#include <QString>

#include "runtime/ServerCapabilities.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

// Outcome of probing a candidate binary path: whether it answers, and the
// detected capabilities. `capabilities.ok` mirrors `ok`.
struct ProbeResult {
    bool ok = false;
    QString version;   // trimmed `--version` stdout, if any
    QString error;     // human-readable when !ok
    ServerCapabilities capabilities;
};

// Locates, probes and validates an llama-server binary (§5 Stage B task 1).
// The validity criterion is a successful probe, NEVER a substring match on the
// file name — renamed/wrapped binaries are allowed (ADR 41 note).
class RuntimeLocator
{
    Q_DECLARE_TR_FUNCTIONS(RuntimeLocator)

public:
    // Runs `--version`, and `--help` (for capability refinement / as a fallback
    // when --version fails), with a bounded wall-clock probe timeout. Never
    // touches a shell (§7.1): only setProgram() + setArguments(). Always probes
    // freshly — user-facing "Check" must reflect the current on-disk state.
    static ProbeResult probe(const QString &binaryPath, int timeoutMs = 5000);

    // probe() but served from a one-entry cache keyed on (path, mtime, size)
    // when the binary is unchanged since the last probe. Used by the manage
    // startServer() path so a repeated recognition round-trip does not re-spawn
    // the binary (and stall the main thread) for an identical file (§H.7).
    static ProbeResult probeCached(const QString &binaryPath, int timeoutMs = 5000);

    // probeCached() with an additional persistent JSON cache under `cacheDir`
    // (ServerCapabilities::cacheFileName, §5.3 step 4 / ADR 41): an unchanged
    // binary (same path/mtime/size) is not re-spawned even across app runs —
    // the first start of a session serves from disk instead of spawning
    // --version/--help again. Only successful probes are cached; a changed file
    // misses and re-probes. Used by the managed startServer() path.
    static ProbeResult probeCached(const QString &binaryPath, const QString &cacheDir,
                                   int timeoutMs = 5000);

    // --version may legitimately be unknown on exotic builds; --help is the
    // secondary probe. Returns the joined diagnostics for the probe UI line.
    static QString probeSummary(const ProbeResult &r);

    // Searches PATH (QStandardPaths::findExecutable), then the common install
    // roots (macOS/Linux /usr/local/bin, Homebrew; Windows %LOCALAPPDATA%).
    // Returns the first candidate that probes ok, or an empty string.
    static QString autoDiscover(int timeoutMs = 5000);

    // Makes the binary executable. Files inside the managed runtime root are
    // restored directly; a manually-selected path merely reports that a
    // confirmation request is required (the UI drives that, §5 task 1 / 7.8).
    // Returns empty on success, else a message describing what is needed.
    static QString ensureExecutable(const QString &binaryPath, bool pathManaged,
                                    bool &needsConfirmation);

    // Cache key for probe(); public merely so the implementation can store a
    // cached result keyed on it.
    struct ProbeKey {
        QString path;
        qint64 mtimeMs;
        qint64 size;
        bool operator==(const ProbeKey &o) const
        {
            return path == o.path && mtimeMs == o.mtimeMs && size == o.size;
        }
    };

private:
    // Runs one argv with a timeout; returns captured stdout+stderr, or sets
    // `error`. Never re-enters a shell.
    static QString runProbe(const QString &binaryPath, QStringList args,
                            int timeoutMs, QString &error);

    static bool probeFromCache(const QString &binaryPath, ProbeResult &out);
    static void cacheProbe(const QString &binaryPath, const ProbeResult &result);
    // Persistent (per-binary) cache accessors; see probeCached(cacheDir).
    static bool probeFromDiskCache(const QString &binaryPath, const QString &cacheDir,
                                   ProbeResult &out);
    static void writeDiskCache(const QString &binaryPath, const QString &cacheDir,
                               const ProbeResult &result);
    // Shared body of probe()/probeCached(): the path validation + --version /
    // --help runs that produce a ProbeResult.
    static ProbeResult probeImpl(const QString &binaryPath, int timeoutMs);
};

}  // namespace llocr