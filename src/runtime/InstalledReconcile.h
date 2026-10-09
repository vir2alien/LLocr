#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include "runtime/ModelRegistry.h"

namespace llocr {

struct ReconcileInput {
    QList<ModelEntry> index;  ///< what index.json recorded
    QList<ModelEntry> disk;   ///< what a scan of the models directory found
    bool diskAvailable = true;

    QString selectedModelPath;          ///< settings: launch/launchModelPath
    QString selectedCheckModelPath;     ///< settings: launch/checkLaunchModelPath
    QString selectedDecisionModelPath;  ///< settings: decision/modelPath
    QString selectedServerPath;         ///< settings: launch/serverPath
    bool selectedServerExists = false;
};

struct ReconcileResult {
    QList<ModelEntry> models;          ///< index order first, then whatever the scan added
    QStringList staleModelSelections;  ///< selected model paths that no longer resolve
    bool staleServerSelection = false;

    int added = 0;      ///< on disk, absent from the index
    int dropped = 0;    ///< in the index, files gone
    int refreshed = 0;  ///< disk facts that changed

    bool indexChanged = false;
    bool diskUnavailable = false;
};

struct ReconcileSelections {
    QString modelPath;
    QString checkModelPath;
    QString decisionModelPath;
    QString serverPath;
    bool serverExists = false;
};

ReconcileResult reconcileInstalled(const ReconcileInput &input);

}  // namespace llocr
