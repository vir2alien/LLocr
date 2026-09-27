#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "runtime/ModelRegistry.h"

namespace llocr {

/// The three sources of truth about what is installed, and the one place that
/// reconciles them.
///
/// They disagree in ordinary ways: the user deletes a model directory by hand,
/// an install is interrupted before the index is written, the settings are reset
/// while the files stay. Each pair has a natural owner, and the policy is stated
/// once here instead of being spread over the callers:
///
///   * **Existence is the filesystem's.** An index entry whose files are gone is
///     dropped, and a directory the scan finds but the index does not know is
///     added. (Before this, a deleted model stayed in the list and offered an
///     «Activate» that could only fail with «Model file not found».)
///   * **Measurable facts come from the filesystem** — `modelPath`, `parts`,
///     `byteSize`, `dir`. They can be re-derived at any time, and a stale one (a
///     resized file) is wrong. `mmprojPath` is deliberately *not* in this group:
///     a directory holding several quants has no way to say which projector
///     belongs to which of them, and the index records the absence of a pairing
///     just as meaningfully as its presence.
///   * **Curated metadata comes from the index** — `revision`, `license`,
///     `sha256`, `parser`, `prompt`, `ctxSize`, `roles`, `addedAt`, `title`,
///     `repo`, `repoId`, `mmprojPath`, `quantization`, `origin`. A scan cannot
///     know a pinned commit, a curated role split, or which projector belongs to
///     which quant, so letting it overwrite them would destroy the very
///     information the index exists to keep. `quantization` belongs here because
///     it is part of the entry's identity — the id embeds it, and the installer
///     removes an entry by id.
///   * **The settings are pointers, not membership.** A selected model or binary
///     that no longer resolves is *reported*, not silently dropped from the list:
///     the file may live outside the models directory, and the user has to decide
///     what to do about it.
struct ReconcileInput {
    QList<ModelEntry> index;  ///< what index.json recorded
    QList<ModelEntry> disk;   ///< what a scan of the models directory found
    /// False when the models directory could not be read at all. The filesystem
    /// then has no opinion, and the index is left untouched — an unreachable
    /// drive must not be mistaken for an empty models directory.
    bool diskAvailable = true;

    QString selectedModelPath;       ///< settings: launch/launchModelPath
    QString selectedCheckModelPath;  ///< settings: launch/checkLaunchModelPath
    QString selectedServerPath;      ///< settings: launch/serverPath
    bool selectedServerExists = false;
};

struct ReconcileResult {
    QList<ModelEntry> models;  ///< index order first, then whatever the scan added
    QStringList staleModelSelections;  ///< selected model paths that no longer resolve
    bool staleServerSelection = false;

    int added = 0;     ///< on disk, absent from the index
    int dropped = 0;   ///< in the index, files gone
    int refreshed = 0;  ///< disk facts that changed

    /// The result differs from the input index, so index.json must be rewritten.
    bool indexChanged = false;
    /// Nothing was reconciled because the filesystem could not be read.
    bool diskUnavailable = false;
};

/// The pointers the settings hold, so the reconciliation can report which of
/// them no longer resolve. The registry itself has no access to the settings,
/// hence the explicit hand-off.
struct ReconcileSelections {
    QString modelPath;
    QString checkModelPath;
    QString serverPath;
    bool serverExists = false;
};

ReconcileResult reconcileInstalled(const ReconcileInput &input);

}  // namespace llocr
