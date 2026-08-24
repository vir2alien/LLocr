#pragma once

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
public:
    // Runs `--version`, and `--help` (for capability refinement / as a fallback
    // when --version fails), with a bounded wall-clock probe timeout. Never
    // touches a shell (§7.1): only setProgram() + setArguments().
    static ProbeResult probe(const QString &binaryPath, int timeoutMs = 10000);

    // --version may legitimately be unknown on exotic builds; --help is the
    // secondary probe. Returns the joined diagnostics for the probe UI line.
    static QString probeSummary(const ProbeResult &r);

    // Searches PATH (QStandardPaths::findExecutable), then the common install
    // roots (macOS/Linux /usr/local/bin, Homebrew; Windows %LOCALAPPDATA%).
    // Returns the first candidate that probes ok, or an empty string.
    static QString autoDiscover(int timeoutMs = 10000);

    // Makes the binary executable. Files inside the managed runtime root are
    // restored directly; a manually-selected path merely reports that a
    // confirmation request is required (the UI drives that, §5 task 1 / 7.8).
    // Returns empty on success, else a message describing what is needed.
    static QString ensureExecutable(const QString &binaryPath, bool pathManaged,
                                    bool &needsConfirmation);

private:
    // Runs one argv with a timeout; returns captured stdout+stderr, or sets
    // `error`. Never re-enters a shell.
    static QString runProbe(const QString &binaryPath, QStringList args,
                            int timeoutMs, QString &error);
};

}  // namespace llocr