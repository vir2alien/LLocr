#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <functional>

#include "runtime/RuntimeState.h"

#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "core/ConnectionRole.h"
#include "core/ModelProfiles.h"
#include "runtime/HardwareInfo.h"
#include "runtime/InstalledFilesModel.h"
#include "runtime/InstalledReconcile.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/ModelInstaller.h"
#include "runtime/ModelInstallTransaction.h"
#include "runtime/ModelPresetCatalog.h"
#include "runtime/RuntimeController.h"

namespace llocr {

ModelInstaller::ModelInstaller(SettingsStore &settings, RuntimeController &runtime, InstalledState &state, QObject *parent)
    : QObject(parent), m_settings(settings), m_installState(state), m_runtime(runtime), m_transaction(new ModelInstallTransaction(settings, m_installState, this)),
      m_installedFiles(new InstalledFilesModel(*this, this)), m_allModels(new ModelQuantModel(*this, settings, QStringLiteral("all"), this)),
      m_pageOcrModels(new ModelQuantModel(*this, settings, QStringLiteral("pageOcr"), this)), m_checkModels(new ModelQuantModel(*this, settings, QStringLiteral("blockRecognition"), this)),
      m_decisionModels(new ModelQuantModel(*this, settings, QStringLiteral("decision"), this)), m_layoutModels(new ModelQuantModel(*this, settings, QStringLiteral("layout"), this)),
      m_rawPageOcrModels(new ModelQuantModel(*this, settings, QStringLiteral("rawPageOcr"), this)), m_fixModels(new ModelQuantModel(*this, settings, QStringLiteral("fix"), this))
{
    connect(m_transaction, &ModelInstallTransaction::stateChanged, this, [this](int state) { setState(static_cast<State>(state)); });
    connect(m_transaction, &ModelInstallTransaction::busyChanged, this, [this](bool busy) { setBusy(busy); });
    connect(m_transaction, &ModelInstallTransaction::progressChanged, this, [this](double progress) { setProgress(progress); });
    connect(m_transaction, &ModelInstallTransaction::statusMessageChanged, this, [this](const QString &message) { setStatusMessage(message); });
    connect(m_transaction, &ModelInstallTransaction::installedListReplaced, this, [this](const QList<ModelEntry> &installed) {
        m_installed = installed;
        publishInstalled();
    });
    connect(&m_settings, &SettingsStore::launchModelPathChanged, this, &ModelInstaller::publishInstalled);
    connect(&m_settings, &SettingsStore::checkLaunchModelPathChanged, this, &ModelInstaller::publishInstalled);
    connect(&m_settings, &SettingsStore::decisionLaunchModelPathChanged, this, &ModelInstaller::publishInstalled);
    connect(&m_settings, &SettingsStore::layoutLaunchModelPathChanged, this, &ModelInstaller::publishInstalled);
    connect(&m_settings, &SettingsStore::rawPageOcrLaunchModelPathChanged, this, &ModelInstaller::publishInstalled);
    connect(&m_settings, &SettingsStore::fixLaunchModelPathChanged, this, &ModelInstaller::publishInstalled);
    // The removal guard refuses files of a Ready server, so the disk block's
    // delete buttons follow the runtime state too.
    connect(&m_runtime, &RuntimeController::stateChanged, this, [this] { m_installedFiles->refresh(); });

    // The task queue rides the same transaction as a manual download: once the
    // user agreed to the missing list, every prepared install starts without a
    // per-item license dialog, and the next item waits for the previous one.
    connect(m_transaction, &ModelInstallTransaction::stateChanged, this, [this](int state) {
        if (!m_taskQueueActive)
            return;
        if (state == State::ReadyToDownload)
            installPrepared();
        else if (state == State::Idle) {
            finishRunningInstall();
            startNextQueuedInstall();
        } else if (state == State::Error) {
            m_runningInstall.reset();
            m_taskQueue.clear();
            setTaskQueueActive(false);
        }
    });

    reloadPresetsInternal();
    refreshInstalled();
}

QObject *ModelInstaller::allModels() const
{
    return m_allModels;
}

QObject *ModelInstaller::quantModels() const
{
    return m_pageOcrModels;
}

QObject *ModelInstaller::checkQuantModels() const
{
    return m_checkModels;
}

QObject *ModelInstaller::decisionQuantModels() const
{
    return m_decisionModels;
}

QObject *ModelInstaller::layoutQuantModels() const
{
    return m_layoutModels;
}

QObject *ModelInstaller::rawPageOcrQuantModels() const
{
    return m_rawPageOcrModels;
}

QObject *ModelInstaller::fixQuantModels() const
{
    return m_fixModels;
}

ModelQuantModel *ModelInstaller::quantModel(const QString &task) const
{
    if (task == QLatin1String("all"))
        return m_allModels;
    switch (connectionRoleFromName(task)) {
    case ConnectionRole::BlockRecognition:
        return m_checkModels;
    case ConnectionRole::Decision:
        return m_decisionModels;
    case ConnectionRole::Layout:
        return m_layoutModels;
    case ConnectionRole::RawPageOcr:
        return m_rawPageOcrModels;
    case ConnectionRole::Fix:
        return m_fixModels;
    case ConnectionRole::PageOcr:
        break;
    }
    return m_pageOcrModels;
}

void ModelInstaller::publishInstalled()
{
    for (ModelQuantModel *model : {m_allModels, m_pageOcrModels, m_checkModels, m_decisionModels, m_layoutModels, m_rawPageOcrModels, m_fixModels})
        model->refresh();
    m_installedFiles->setSource(m_installed);
    emit installedChanged();
}

QObject *ModelInstaller::installedFiles() const
{
    return m_installedFiles;
}

ModelInstaller::~ModelInstaller() = default;

void ModelInstaller::shutdown()
{
    m_transaction->shutdown();
}

void ModelInstaller::setState(State next)
{
    if (m_state == next)
        return;
    m_state = next;
    emit stateChanged();
}

void ModelInstaller::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void ModelInstaller::setProgress(double p)
{
    if (qFuzzyCompare(m_progress, p) || p < 0.0 || p > 1.0)
        return;
    m_progress = p;
    emit progressChanged();
}

void ModelInstaller::setStatusMessage(const QString &msg)
{
    if (m_statusMessage == msg)
        return;
    m_statusMessage = msg;
    emit statusMessageChanged();
}

void ModelInstaller::retranslate()
{
    m_transaction->retranslate();
}

const QList<ModelPreset> &ModelInstaller::presetsForRole(const QString &task) const
{
    if (task == QLatin1String("all"))
        return m_presetsAll;
    switch (connectionRoleFromName(task)) {
    case ConnectionRole::BlockRecognition:
        return m_presetsValidate;
    case ConnectionRole::Decision:
        return m_presetsDecision;
    case ConnectionRole::Layout:
        return m_presetsLayout;
    case ConnectionRole::RawPageOcr:
        return m_presetsRawPageOcr;
    case ConnectionRole::Fix:
        return m_presetsFix;
    case ConnectionRole::PageOcr:
        break;
    }
    return m_presets;
}

void ModelInstaller::reloadPresets()
{
    reloadPresetsInternal();
}

void ModelInstaller::reloadPresetsInternal()
{
    const QString modelsDir = m_installState.paths().modelsDir();

    QString err;
    const QList<ModelPreset> all = ModelPresetCatalog::load(ModelPresetCatalog::expand(ModelProfiles::instance()), QDir(modelsDir).filePath(QStringLiteral("catalog.json")), err);
    if (!err.isEmpty())
        setStatusMessage(err);

    m_presetsAll = all;
    m_presets.clear();
    m_presetsValidate.clear();
    m_presetsDecision.clear();
    m_presetsLayout.clear();
    m_presetsRawPageOcr.clear();
    m_presetsFix.clear();
    for (const ModelPreset &preset : all) {
        const QString profileId = preset.profileId.isEmpty() ? preset.id : preset.profileId;
        if (ModelProfiles::roleFor(profileId, QStringLiteral("pageOcr")))
            m_presets.append(preset);
        if (ModelProfiles::roleFor(profileId, QStringLiteral("blockRecognition")))
            m_presetsValidate.append(preset);
        if (ModelProfiles::roleFor(profileId, QStringLiteral("decision")))
            m_presetsDecision.append(preset);
        if (ModelProfiles::roleFor(profileId, QStringLiteral("layout")))
            m_presetsLayout.append(preset);
        if (ModelProfiles::roleFor(profileId, QStringLiteral("rawPageOcr")))
            m_presetsRawPageOcr.append(preset);
        if (ModelProfiles::roleFor(profileId, QStringLiteral("fix")))
            m_presetsFix.append(preset);
    }

    publishInstalled();
    emit presetsChanged();
}

void ModelInstaller::refreshInstalled()
{
    QString err;
    bool rebuilt = false;
    ReconcileResult report;
    ReconcileSelections selections;
    selections.modelPath = m_settings.launchModelPath();
    selections.checkModelPath = m_settings.checkLaunchModelPath();
    selections.decisionModelPath = m_settings.decisionLaunchModelPath();
    selections.layoutModelPath = m_settings.layoutLaunchModelPath();
    selections.serverPath = m_settings.serverPath();
    selections.serverExists = QFileInfo::exists(m_settings.serverPath());
    m_installed = ModelRegistry::load(m_installState.paths().modelsDir(), rebuilt, err, &report, selections);
    if (!err.isEmpty() && !rebuilt)
        setStatusMessage(err);
    syncProfileToActiveModel();
    reportStaleSelections(report);
    m_transaction->setInstalled(m_installed);
    publishInstalled();
}

void ModelInstaller::syncProfileToActiveModel()
{
    const QString active = m_settings.launchModelPath();
    for (const ModelEntry &e : m_installed) {
        if (e.modelPath.isEmpty() || e.modelPath != active || e.repo.isEmpty())
            continue;
        m_settings.selectModelProfile(e.repo, QStringLiteral("pageOcr"));
        break;
    }
}

void ModelInstaller::reportStaleSelections(const ReconcileResult &report)
{
    if (report.diskUnavailable)
        return;  // no filesystem opinion — nothing to accuse anyone of
    for (const QString &path : report.staleModelSelections) {
        setStatusMessage(tr("The selected model is no longer on disk: %1 — "
                            "pick another one in Settings → Models.")
                             .arg(QFileInfo(path).fileName()));
        return;
    }
}

void ModelInstaller::rescanRegistry()
{
    QString err;
    const QString modelsDir = m_installState.paths().modelsDir();
    m_installed = ModelRegistry::scanModelsDir(modelsDir);
    ModelRegistry::save(modelsDir, m_installed, err);
    m_transaction->setInstalled(m_installed);
    refreshInstalled();
}

QString ModelInstaller::setActiveModel(int index, const QString &role)
{
    if (index < 0 || index >= m_installed.size())
        return tr("Invalid model selection");
    const ModelEntry &e = m_installed.at(index);
    if (e.modelPath.isEmpty())
        return tr("This model has no model file selected");

    m_settings.setLaunchModelPathForRole(role, e.modelPath);
    if (!e.mmprojPath.isEmpty())
        m_settings.setLaunchMmprojPathForRole(role, e.mmprojPath);
    m_settings.setLaunchDraftPathForRole(role, e.draftPath);
    m_settings.selectModelProfile(e.repo, role);
    m_settings.forceSave();
    publishInstalled();
    return QString();
}

void ModelInstaller::selectQuant(const QString &key, const QString &quantId)
{
    if (quantId.isEmpty())
        return;
    const QList<ModelQuantModel *> models = {m_allModels, m_pageOcrModels, m_checkModels, m_decisionModels, m_layoutModels, m_rawPageOcrModels, m_fixModels};
    const bool known = std::any_of(models.cbegin(), models.cend(), [&](const ModelQuantModel *model) { return model->hasQuant(key, quantId); });
    if (!known)
        return;
    m_settings.setSelectedQuant(key, quantId);
    publishInstalled();
}

int ModelInstaller::installedIndexFor(const QString &key, const QString &quantId, const QString &role) const
{
    return quantModel(role)->entryIndexFor(key, quantId);
}

int ModelInstaller::activeTaskCount(const ModelEntry &entry) const
{
    if (entry.modelPath.isEmpty())
        return 0;
    int count = 0;
    for (const ConnectionRole role : {ConnectionRole::PageOcr, ConnectionRole::RawPageOcr, ConnectionRole::BlockRecognition, ConnectionRole::Decision, ConnectionRole::Layout, ConnectionRole::Fix}) {
        if (entry.modelPath == m_settings.launchModelPathForRole(connectionRoleName(role)))
            ++count;
    }
    return count;
}

bool ModelInstaller::isActiveForAnyTask(const ModelEntry &entry) const
{
    return activeTaskCount(entry) > 0;
}

QString ModelInstaller::removalBlockerFor(const ModelEntry &entry) const
{
    return ModelRegistry::removalError(entry, m_installState.paths().modelsDir(), activeTaskCount(entry) > 0, m_runtime.state() == RuntimeState::Ready);
}

QString ModelInstaller::removeInstalledEntry(const QString &entryId)
{
    for (int i = 0; i < m_installed.size(); ++i) {
        if (m_installed.at(i).id == entryId)
            return removeModel(i);
    }
    return tr("This model is not installed");
}

QString ModelInstaller::openInstalledEntryFolder(const QString &entryId)
{
    for (int i = 0; i < m_installed.size(); ++i) {
        if (m_installed.at(i).id == entryId)
            return openModelFolder(i);
    }
    return tr("This model is not installed");
}

void ModelInstaller::downloadQuant(const QString &key, const QString &quantId, const QString &role)
{
    if (m_busy)
        return;
    const QList<ModelPreset> &presets = presetsForRole(role);
    const int preset = quantModel(role)->presetIndexFor(key, quantId);
    if (preset < 0 || preset >= presets.size()) {
        setStatusMessage(tr("This quantization is not available for download"));
        setState(State::Error);
        return;
    }
    m_transaction->prepare(presets.at(preset), role);
}

QString ModelInstaller::removeQuant(const QString &key, const QString &quantId, const QString &role)
{
    const int index = installedIndexFor(key, quantId, role);
    if (index < 0)
        return tr("This quantization is not installed");
    return removeModel(index);
}

QString ModelInstaller::removeModelRow(const QString &key, const QString &role)
{
    QList<int> targets = quantModel(role)->entryIndexesFor(key);
    if (targets.isEmpty())
        return tr("This model is not installed");

    const RuntimePaths paths = m_installState.paths();
    const bool ready = m_runtime.state() == RuntimeState::Ready;
    for (int index : std::as_const(targets)) {
        const ModelEntry &entry = m_installed.at(index);
        const bool active = isActiveForAnyTask(entry);
        const QString guard = ModelRegistry::removalError(entry, paths.modelsDir(), active, ready);
        if (!guard.isEmpty())
            return guard;
    }

    std::sort(targets.begin(), targets.end(), std::greater<int>());
    QString firstError;
    for (int index : std::as_const(targets)) {
        const QString error = removeModel(index);
        if (!error.isEmpty() && firstError.isEmpty())
            firstError = error;
    }
    return firstError;
}

QString ModelInstaller::openQuantFolder(const QString &key, const QString &quantId, const QString &role)
{
    const int index = installedIndexFor(key, quantId, role);
    if (index < 0)
        return tr("This quantization is not installed");
    return openModelFolder(index);
}

QString ModelInstaller::removeModel(int index)
{
    if (index < 0 || index >= m_installed.size())
        return tr("Invalid model selection");
    const ModelEntry &e = m_installed.at(index);
    const bool active = isActiveForAnyTask(e);
    const bool ready = m_runtime.state() == RuntimeState::Ready;
    const RuntimePaths currentPaths = m_installState.paths();
    const QString guard = ModelRegistry::removalError(e, currentPaths.modelsDir(), active, ready);
    if (!guard.isEmpty())
        return guard;

    QDir d(e.dir);

    bool sharedDir = false;
    for (const ModelEntry &x : std::as_const(m_installed)) {
        if (&x == &e)
            continue;
        if (QFileInfo(x.dir).canonicalFilePath() == QFileInfo(e.dir).canonicalFilePath()) {
            sharedDir = true;
            break;
        }
    }

    if (sharedDir) {
        QStringList owned = e.parts;
        owned.prepend(e.modelPath);
        bool mmprojShared = false;
        if (!e.mmprojPath.isEmpty()) {
            for (const ModelEntry &x : std::as_const(m_installed)) {
                if (&x == &e)
                    continue;
                if (QFileInfo(x.dir).canonicalFilePath() == QFileInfo(e.dir).canonicalFilePath() && x.mmprojPath == e.mmprojPath) {
                    mmprojShared = true;
                    break;
                }
            }
            if (!mmprojShared)
                owned << e.mmprojPath;
        }
        bool draftShared = false;
        if (!e.draftPath.isEmpty()) {
            for (const ModelEntry &x : std::as_const(m_installed)) {
                if (&x == &e)
                    continue;
                if (QFileInfo(x.dir).canonicalFilePath() == QFileInfo(e.dir).canonicalFilePath() && x.draftPath == e.draftPath) {
                    draftShared = true;
                    break;
                }
            }
            if (!draftShared)
                owned << e.draftPath;
        }
        for (const QString &path : std::as_const(owned)) {
            QFile f(path);
            if (f.exists() && !f.remove())
                return tr("Unable to remove model file: %1").arg(path);
        }
        if (d.isEmpty())
            QDir().rmdir(e.dir);
    } else if (!d.isEmpty()) {
        if (!d.removeRecursively())
            return tr("Unable to remove model directory: %1").arg(e.dir);
    }

    QString saveErr;
    QList<ModelEntry> updated;
    if (!ModelRegistry::update(
            currentPaths.modelsDir(),
            [&updated, &e](QList<ModelEntry> &entries) {
                entries.removeIf([&](const ModelEntry &x) { return x.id == e.id; });
                updated = entries;
                return entries;
            },
            saveErr)) {
        refreshInstalled();
        return tr("Model files removed, but the registry could not be saved: %1").arg(saveErr);
    }
    m_installed = updated;
    m_transaction->setInstalled(m_installed);
    publishInstalled();
    return QString();
}

QString ModelInstaller::openModelFolder(int index)
{
    if (index < 0 || index >= m_installed.size())
        return tr("Invalid model selection");
    const ModelEntry &e = m_installed.at(index);
    QString dir = e.dir;
    if (dir.isEmpty() && !e.modelPath.isEmpty())
        dir = QFileInfo(e.modelPath).absolutePath();
    if (dir.isEmpty())
        return tr("Model folder not found");
    if (!QDir(dir).exists())
        return tr("Model folder not found: %1").arg(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    return QString();
}

void ModelInstaller::installPrepared()
{
    m_transaction->installPrepared();
}

void ModelInstaller::cancelInstall()
{
    m_runningInstall.reset();
    m_taskQueue.clear();
    setTaskQueueActive(false);
    m_transaction->cancel();
}

QVariantList ModelInstaller::applyTaskChoices(const QVariantList &choices)
{
    QVariantList missing;
    QList<std::pair<QString, QString>> missingKeys;
    for (const QVariant &choiceVar : choices) {
        const QVariantMap choice = choiceVar.toMap();
        const QString task = choice.value(QStringLiteral("task")).toString();
        // An unknown id would silently become pageOcr; only the six real ones
        // pass this round-trip.
        if (task != connectionRoleName(connectionRoleFromName(task)))
            continue;
        const QString key = choice.value(QStringLiteral("key")).toString();
        const QString quantId = choice.value(QStringLiteral("quantId")).toString();
        if (key.isEmpty() || quantId.isEmpty())
            continue;
        ModelQuantModel *model = quantModel(task);
        const int entry = model->entryIndexFor(key, quantId);
        if (entry >= 0) {
            setActiveModel(entry, task);
            // The remembered pick must follow, or the next open shows a stale
            // saved quantization instead of the one just activated.
            m_settings.setSelectedQuant(key, quantId);
            continue;
        }

        // Not on disk: the choice is still recorded (the profile intent
        // survives a canceled queue), and the download dialog lists it — one
        // row per model+quant, no matter how many tasks picked it. The
        // download's final path goes into the task's launch settings now: the
        // refresh-time profile repair keys off the launch path, and the old
        // model file still sitting there must not read as the user's choice.
        const QList<ModelPreset> &presets = presetsForRole(task);
        const int preset = model->presetIndexFor(key, quantId);
        if (preset >= 0 && preset < presets.size()) {
            m_settings.selectModelProfile(presets.at(preset).repo, task);
            m_settings.setSelectedQuant(key, quantId);
            const ModelProfiles::Profile *profile = ModelProfiles::findBuiltIn(key);
            if (profile) {
                for (const ModelProfiles::Quant &q : profile->files.quants) {
                    if (q.id.compare(quantId, Qt::CaseInsensitive) != 0)
                        continue;
                    const QString dir = QDir(m_installState.paths().modelsDir()).filePath(ModelInstallTransaction::repoDirName(presets.at(preset).repo));
                    m_settings.setLaunchModelPathForRole(task, QDir(dir).filePath(q.file));
                    break;
                }
            }
        }
        const QVariantMap row = model->rowFor(key);
        QVariantList quants = row.value(QStringLiteral("quants")).toList();
        qint64 size = 0;
        for (const QVariant &q : quants) {
            const QVariantMap quant = q.toMap();
            if (quant.value(QStringLiteral("id")).toString().compare(quantId, Qt::CaseInsensitive) == 0)
                size = quant.value(QStringLiteral("size")).toLongLong();
        }
        const auto known = std::find_if(missingKeys.cbegin(), missingKeys.cend(), [&](const auto &k) { return k.first == key && k.second.compare(quantId, Qt::CaseInsensitive) == 0; });
        if (known != missingKeys.cend()) {
            QVariantMap merged = missing.at(known - missingKeys.cbegin()).toMap();
            QVariantList tasks = merged.value(QStringLiteral("tasks")).toList();
            tasks.append(task);
            merged.insert(QStringLiteral("tasks"), tasks);
            missing[known - missingKeys.cbegin()] = merged;
            continue;
        }
        missingKeys.append({key, quantId});
        QVariantMap item;
        item.insert(QStringLiteral("task"), task);
        item.insert(QStringLiteral("tasks"), QVariantList{task});
        item.insert(QStringLiteral("key"), key);
        item.insert(QStringLiteral("quantId"), quantId);
        item.insert(QStringLiteral("model"), row.value(QStringLiteral("title")));
        item.insert(QStringLiteral("size"), size);
        missing.append(item);
    }
    m_settings.forceSave();
    publishInstalled();
    return missing;
}

// The wizard's auto-selection (ADR 169/170): every task gets its role-default
// profile, the quantization is the best one the memory budget allows — an
// already installed quant always wins over a download of the same grade.
QVariantList ModelInstaller::defaultTaskChoices(qint64 budgetBytes)
{
    const qint64 budget = budgetBytes >= 0 ? budgetBytes : HardwareInfo::detect().memoryBudgetBytes();
    static const QStringList kTasks = {
        QStringLiteral("pageOcr"), QStringLiteral("rawPageOcr"), QStringLiteral("blockRecognition"), QStringLiteral("decision"), QStringLiteral("layout"), QStringLiteral("fix")};
    QVariantList choices;
    for (const QString &task : kTasks) {
        const QString key = ModelProfiles::defaultIdForRole(ModelProfiles::instance(), task);
        ModelQuantModel *model = quantModel(task);
        if (key.isEmpty() || !model || model->rowIndexOf(key) < 0)
            continue;
        const QVariantList quants = model->rowFor(key).value(QStringLiteral("quants")).toList();
        if (quants.isEmpty())
            continue;
        const auto installed = [](const QVariantMap &q) { return q.value(QStringLiteral("installed")).toBool(); };
        const auto fits = [budget](const QVariantMap &q) {
            const qint64 size = q.value(QStringLiteral("size")).toLongLong();
            return size > 0 && size <= budget;
        };
        int pick = -1;
        for (int i = 0; i < quants.size(); ++i) {
            if (installed(quants.at(i).toMap()) && fits(quants.at(i).toMap())) {
                pick = i;
                break;
            }
        }
        if (pick < 0) {
            for (int i = 0; i < quants.size(); ++i) {
                if (installed(quants.at(i).toMap())) {
                    pick = i;
                    break;
                }
            }
        }
        if (pick < 0) {
            for (int i = 0; i < quants.size(); ++i) {
                if (fits(quants.at(i).toMap())) {
                    pick = i;
                    break;
                }
            }
        }
        if (pick < 0)
            pick = quants.size() - 1;

        QVariantMap choice;
        choice.insert(QStringLiteral("task"), task);
        choice.insert(QStringLiteral("key"), key);
        choice.insert(QStringLiteral("quantId"), quants.at(pick).toMap().value(QStringLiteral("id")).toString());
        choices.append(choice);
    }
    return choices;
}

void ModelInstaller::queueTaskInstalls(const QVariantList &choices)
{
    if (m_busy)
        return;
    for (const QVariant &choiceVar : choices) {
        const QVariantMap choice = choiceVar.toMap();
        QStringList tasks;
        for (const QVariant &t : choice.value(QStringLiteral("tasks")).toList())
            tasks << t.toString();
        if (tasks.isEmpty())
            tasks << choice.value(QStringLiteral("task")).toString();
        tasks.removeIf([](const QString &task) { return task != connectionRoleName(connectionRoleFromName(task)); });
        const QString key = choice.value(QStringLiteral("key")).toString();
        const QString quantId = choice.value(QStringLiteral("quantId")).toString();
        if (tasks.isEmpty() || key.isEmpty() || quantId.isEmpty())
            continue;
        const auto queued = std::find_if(m_taskQueue.cbegin(), m_taskQueue.cend(), [&](const TaskInstall &item) { return item.key == key && item.quantId.compare(quantId, Qt::CaseInsensitive) == 0; });
        if (queued != m_taskQueue.cend()) {
            for (const QString &task : tasks)
                if (!queued->tasks.contains(task))
                    m_taskQueue[queued - m_taskQueue.cbegin()].tasks.append(task);
            continue;
        }
        m_taskQueue.append({tasks, key, quantId});
    }
    if (m_taskQueue.isEmpty())
        return;
    setTaskQueueActive(true);
    startNextQueuedInstall();
}

void ModelInstaller::cancelTaskInstalls()
{
    m_runningInstall.reset();
    m_taskQueue.clear();
    setTaskQueueActive(false);
    if (m_transaction->busy())
        m_transaction->cancel();
}

void ModelInstaller::setTaskQueueActive(bool active)
{
    if (m_taskQueueActive == active)
        return;
    m_taskQueueActive = active;
    emit taskQueueStateChanged();
}

void ModelInstaller::startNextQueuedInstall()
{
    if (m_taskQueue.isEmpty()) {
        setTaskQueueActive(false);
        return;
    }
    const TaskInstall item = m_taskQueue.takeFirst();
    m_runningInstall = item;
    ModelQuantModel *model = quantModel(item.tasks.first());
    const QList<ModelPreset> &presets = presetsForRole(item.tasks.first());
    const int preset = model->presetIndexFor(item.key, item.quantId);
    if (preset < 0 || preset >= presets.size()) {
        setStatusMessage(tr("This quantization is not available for download"));
        setState(State::Error);
        m_runningInstall.reset();
        m_taskQueue.clear();
        setTaskQueueActive(false);
        return;
    }
    m_transaction->prepare(presets.at(preset), item.tasks.first());
}

void ModelInstaller::finishRunningInstall()
{
    if (!m_runningInstall)
        return;
    const TaskInstall item = *m_runningInstall;
    m_runningInstall.reset();
    // The transaction already activated the first task; the rest ride the
    // same download and are activated here.
    for (const QString &task : item.tasks.mid(1)) {
        const int entry = quantModel(task)->entryIndexFor(item.key, item.quantId);
        if (entry >= 0)
            setActiveModel(entry, task);
    }
}

}  // namespace llocr