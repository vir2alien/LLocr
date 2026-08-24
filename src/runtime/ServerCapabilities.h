#pragma once

#include <QString>

namespace llocr {

// Which argv-affecting features a given llama-server binary supports (§5.3).
// Stage A ships the value type and the allow-listed defaults referenced by
// ServerLaunchConfig; the probe that fills it (build parsing + `--help`) lands
// in Stage B. The struct is deliberately plain data so it can be cached as
// JSON under <cache>/capabilities-<sha1>.json.
struct ServerCapabilities {
    QString versionText;  // raw version string, if parseable
    QString build;         // e.g. "b10594"

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

    // Build support policy: refuse binaries below this (ADR 27).
    static constexpr const char *kMinimumSupportedBuild = "b4000";
};

}  // namespace llocr