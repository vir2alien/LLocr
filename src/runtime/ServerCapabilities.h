#pragma once

#include <QJsonObject>
#include <QString>

namespace llocr {

// Which argv-affecting features a given llama-server binary supports (§5.3).
// Stage A ships the value type and the allow-listed defaults referenced by
// ServerLaunchConfig. Stage B adds the detection that fills it: build parsing
// + `--help` refinement (§5.3 hybrid scheme). The struct is deliberately plain
// data so it can be cached as JSON under <cache>/capabilities-<sha1>.json.
struct ServerCapabilities {
    QString versionText;  // raw `--version` stdout, trimmed, if the probe ran it
    QString build;         // e.g. "b10594"; empty when undetermined
    bool ok = false;       // the binary answered --version/--help (responds at all)
    bool belowMinimum = false;  // build is known and < kMinimumSupportedBuild

    // `--flash-attn` exists at all.
    bool supportsFlashAttn = true;
    // `--flash-attn` takes on|off|auto (newer builds) vs. a bare boolean.
    bool supportsFlashAttnValue = false;
    // `--alias` lets the client name the loaded model (older builds lack it;
    // then the first id from /v1/models is used instead, §4.2).
    bool supportsAlias = true;
    // `--jinja` exists (older builds use a fixed chat template).
    bool supportsJinja = false;
    // `-ctk` / `-ctv` cache type flags.
    bool supportsCacheTypeK = false;
    bool supportsCacheTypeV = false;

    // Build support policy: refuse binaries below this (ADR 27). The numeric
    // gates below are the capability allowlist ranges and the minimum gate.
    static constexpr const char *kMinimumSupportedBuild = "b4000";
    static constexpr int kMinimumBuildNumber = 4000;

    // --- Detection -------------------------------------------------------
    //
    // Builds a ServerCapabilities for a binary whose --version and (optional)
    // --help outputs were captured. Never trusts the file name; validity is
    // "the binary answered" (ok). The build allowlist is applied first, then
    // the --help text refines each flag (added/removed) — §5.3 steps 2–4.
    static ServerCapabilities detect(const QString &versionOutput,
                                     const QString &helpOutput = QString());

    // Tolerant build-number parser. Returns "b10594" -> 10594, "build 1054"
    // -> 1054, unknown -> -1. `build` field is set to "" when undetermined.
    static int extractBuildNumber(const QString &versionOutput);

    // --- JSON (cache; §5.3 step 4) ---------------------------------------
    QJsonObject toJson() const;
    static ServerCapabilities fromJson(const QJsonObject &o);

    // <cacheDir>/capabilities-<sha1(path+mtime+size)>.json — the cache key for
    // a given binary path (re-probed when the file, its mtime or its size
    // changes).
    static QString cacheFileName(const QString &cacheDir, const QString &binaryPath);
};

}  // namespace llocr