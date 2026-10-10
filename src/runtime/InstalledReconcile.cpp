#include "runtime/InstalledReconcile.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>

#include <algorithm>

#include "config/RuntimePaths.h"

namespace llocr {

namespace {

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
        if (!entry.draftPath.isEmpty() && !QFileInfo::exists(entry.draftPath))
            return false;
        return true;
    }
    return !entry.dir.isEmpty() && QFileInfo(entry.dir).isDir();
}

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
    return recorded.modelPath != scanned.modelPath || recorded.parts != scanned.parts || recorded.byteSize != scanned.byteSize || recorded.dir != scanned.dir;
}

bool selectsPath(const ModelEntry &entry, const QString &selected)
{
    if (selected.isEmpty() || entry.modelPath.isEmpty())
        return false;
    return RuntimePaths::normalized(entry.modelPath) == RuntimePaths::normalized(selected);
}

}  // namespace

ReconcileResult reconcileInstalled(const ReconcileInput &input)
{
    ReconcileResult result;

    if (!input.diskAvailable) {
        result.models = input.index;
        result.diskUnavailable = true;
        result.staleServerSelection = !input.selectedServerPath.isEmpty() && !input.selectedServerExists;
        return result;
    }

    QList<ModelEntry> merged;
    QList<int> diskMatched(input.disk.size(), 0);
    QHash<QString, int> diskByPath;
    for (int i = 0; i < input.disk.size(); ++i) {
        const QString &path = input.disk.at(i).modelPath;
        if (path.isEmpty())
            continue;
        diskByPath.insert(RuntimePaths::normalized(path), i);
    }

    for (const ModelEntry &recorded : input.index) {
        ModelEntry entry = recorded;
        if (!entryIsPresent(recorded)) {
            ++result.dropped;
            continue;
        }

        int scannedIndex = -1;
        if (!recorded.modelPath.isEmpty()) {
            const auto it = diskByPath.constFind(RuntimePaths::normalized(recorded.modelPath));
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
        merged.append(scanned);
        ++result.added;
    }

    result.models = merged;
    result.indexChanged = merged != input.index;

    for (const QString &selected : {input.selectedModelPath, input.selectedCheckModelPath, input.selectedDecisionModelPath, input.selectedLayoutModelPath}) {
        if (selected.isEmpty())
            continue;
        const bool resolved = std::any_of(merged.cbegin(), merged.cend(), [&](const ModelEntry &entry) { return selectsPath(entry, selected); });
        if (!resolved)
            result.staleModelSelections.append(selected);
    }

    result.staleServerSelection = !input.selectedServerPath.isEmpty() && !input.selectedServerExists;
    return result;
}

}  // namespace llocr
