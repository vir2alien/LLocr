#include "runtime/InstalledReconcile.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>

#include <algorithm>

namespace llocr {

namespace {

QString normalizedPath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

/// Does the entry still describe files that are there? An entry with no model
/// file at all is a directory reference (an external model, or a hidden one) and
/// is judged by its directory.
bool entryIsPresent(const ModelEntry &entry)
{
    if (!entry.modelPath.isEmpty()) {
        if (!QFileInfo::exists(entry.modelPath))
            return false;
        for (const QString &part : entry.parts) {
            if (!QFileInfo::exists(part))
                return false;
        }
        if (!entry.mmprojPath.isEmpty() && !QFileInfo::exists(entry.mmprojPath))
            return false;
        return true;
    }
    return !entry.dir.isEmpty() && QFileInfo(entry.dir).isDir();
}

/// The facts the filesystem can answer for itself. A scan always has the newer
/// answer for these, so they are taken wholesale rather than merged field by
/// field.
///
/// `mmprojPath` is deliberately not in this group. When a directory holds
/// several quants there is no way to tell *which* projector belongs to *which*
/// of them — the scan attaches the first one it sees to every model in the
/// folder. The index records the pairing the installer actually performed, and
/// it records its *absence* too: a model stored without a projector must not
/// acquire one because an unrelated projector happens to sit next to it, or it
/// would move from the check list to the recognition list.
ModelEntry withDiskFacts(const ModelEntry &recorded, const ModelEntry &scanned)
{
    ModelEntry merged = recorded;
    merged.modelPath = scanned.modelPath;
    merged.parts = scanned.parts;
    merged.byteSize = scanned.byteSize;
    merged.dir = scanned.dir;
    return merged;
}

bool diskFactsDiffer(const ModelEntry &recorded, const ModelEntry &scanned)
{
    return recorded.modelPath != scanned.modelPath || recorded.parts != scanned.parts
           || recorded.byteSize != scanned.byteSize || recorded.dir != scanned.dir;
}

bool selectsPath(const ReconcileInput &input, const ModelEntry &entry, const QString &selected)
{
    if (selected.isEmpty() || entry.modelPath.isEmpty())
        return false;
    return normalizedPath(entry.modelPath) == normalizedPath(selected);
}

}  // namespace

ReconcileResult reconcileInstalled(const ReconcileInput &input)
{
    ReconcileResult result;

    if (!input.diskAvailable) {
        // The filesystem had no opinion: keep the index as it is, and say so
        // rather than reporting every model as missing.
        result.models = input.index;
        result.diskUnavailable = true;
        result.staleServerSelection = !input.selectedServerPath.isEmpty()
                                      && !input.selectedServerExists;
        return result;
    }

    // Index first (it carries the curated fields and the insertion order), then
    // whatever the scan found that the index did not know about.
    QList<ModelEntry> merged;
    QList<int> diskMatched(input.disk.size(), 0);
    QHash<QString, int> diskByPath;
    for (int i = 0; i < input.disk.size(); ++i) {
        const QString &path = input.disk.at(i).modelPath;
        if (path.isEmpty())
            continue;
        diskByPath.insert(normalizedPath(path), i);
    }

    for (const ModelEntry &recorded : input.index) {
        ModelEntry entry = recorded;
        if (!entryIsPresent(recorded)) {
            ++result.dropped;
            continue;
        }

        int scannedIndex = -1;
        if (!recorded.modelPath.isEmpty()) {
            const auto it = diskByPath.constFind(normalizedPath(recorded.modelPath));
            if (it != diskByPath.constEnd()) {
                scannedIndex = it.value();
                diskMatched[scannedIndex] = 1;
            }
        }
        if (scannedIndex >= 0) {
            const ModelEntry &scanned = input.disk.at(scannedIndex);
            if (diskFactsDiffer(recorded, scanned)) {
                entry = withDiskFacts(recorded, scanned);
                ++result.refreshed;
            }
        }
        merged.append(std::move(entry));
    }

    for (int i = 0; i < input.disk.size(); ++i) {
        if (diskMatched.at(i) != 0)
            continue;
        const ModelEntry &scanned = input.disk.at(i);
        if (scanned.modelPath.isEmpty())
            continue;
        // Not in the index — it arrived from outside (a manual copy, an install
        // interrupted before the index was written), so it joins the list.
        merged.append(scanned);
        ++result.added;
    }

    result.models = merged;
    result.indexChanged = merged != input.index;

    for (const QString &selected : {input.selectedModelPath,
                                    input.selectedCheckModelPath}) {
        if (selected.isEmpty())
            continue;
        const bool resolved = std::any_of(
            merged.cbegin(), merged.cend(), [&](const ModelEntry &entry) {
                return selectsPath(input, entry, selected);
            });
        if (!resolved)
            result.staleModelSelections.append(selected);
    }

    result.staleServerSelection = !input.selectedServerPath.isEmpty()
                                  && !input.selectedServerExists;
    return result;
}

}  // namespace llocr
