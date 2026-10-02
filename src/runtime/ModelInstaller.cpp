#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <functional>

#include "runtime/RuntimeState.h"

#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "core/ModelProfiles.h"
#include "runtime/InstalledReconcile.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/ModelInstaller.h"
#include "runtime/ModelInstallTransaction.h"
#include "runtime/ModelPresetCatalog.h"
#include "runtime/RuntimeController.h"

namespace llocr {

ModelInstaller::ModelInstaller(SettingsStore &settings, RuntimeController &runtime, InstalledState &state, QObject *parent)
    : QObject(parent), m_settings(settings), m_installState(state), m_runtime(runtime), m_transaction(new ModelInstallTransaction(settings, m_installState, this)),
      m_ocrModels(new ModelQuantModel(*this, settings, false, this)), m_checkModels(new ModelQuantModel(*this, settings, true, this))
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

    reloadPresetsInternal();
    refreshInstalled();
}

QObject *ModelInstaller::quantModels() const
{
    return m_ocrModels;
}

QObject *ModelInstaller::checkQuantModels() const
{
    return m_checkModels;
}

ModelQuantModel *ModelInstaller::quantModel(bool forCheck) const
{
    return forCheck ? m_checkModels : m_ocrModels;
}

void ModelInstaller::publishInstalled()
{
    m_ocrModels->refresh();
    m_checkModels->refresh();
    emit installedChanged();
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

QString ModelInstaller::activeTitle() const
{
    const QString active = m_settings.launchModelPath();
    for (const ModelEntry &e : m_installed) {
        if (!e.modelPath.isEmpty() && e.modelPath == active)
            return e.title;
    }
    return QString();
}

QString ModelInstaller::checkActiveTitle() const
{
    const QString active = m_settings.checkLaunchModelPath();
    for (const ModelEntry &e : m_installed) {
        if (!e.modelPath.isEmpty() && e.modelPath == active)
            return e.title;
    }
    return QString();
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

    m_presets.clear();
    m_presetsValidate.clear();
    for (const ModelPreset &preset : all) {
        const QString profileId = preset.profileId.isEmpty() ? preset.id : preset.profileId;
        if (ModelProfiles::roleFor(profileId, QStringLiteral("ocr")))
            m_presets.append(preset);
        if (ModelProfiles::roleFor(profileId, QStringLiteral("check")))
            m_presetsValidate.append(preset);
    }

    m_ocrModels->refresh();
    m_checkModels->refresh();
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
        m_settings.selectModelProfile(e.repo, QStringLiteral("ocr"), false);
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

QString ModelInstaller::setActiveModel(int index, bool forCheck)
{
    if (index < 0 || index >= m_installed.size())
        return tr("Invalid model selection");
    const ModelEntry &e = m_installed.at(index);
    if (e.modelPath.isEmpty())
        return tr("This model has no model file selected");

    if (forCheck) {
        m_settings.setCheckLaunchModelPath(e.modelPath);
        if (!e.mmprojPath.isEmpty())
            m_settings.setCheckLaunchMmprojPath(e.mmprojPath);
        m_settings.selectModelProfile(e.repo, QStringLiteral("check"), true);
        m_settings.forceSave();
        publishInstalled();
        return QString();
    }

    m_settings.setLaunchModelPath(e.modelPath);
    if (!e.mmprojPath.isEmpty())
        m_settings.setLaunchMmprojPath(e.mmprojPath);
    m_settings.selectModelProfile(e.repo, QStringLiteral("ocr"), false);
    m_settings.forceSave();
    publishInstalled();
    return QString();
}

void ModelInstaller::selectQuant(const QString &key, const QString &quantId)
{
    if (quantId.isEmpty())
        return;
    if (!m_ocrModels->hasQuant(key, quantId) && !m_checkModels->hasQuant(key, quantId))
        return;
    m_settings.setSelectedQuant(key, quantId);
    m_ocrModels->refresh();
    m_checkModels->refresh();
}

int ModelInstaller::installedIndexFor(const QString &key, const QString &quantId, bool forCheck) const
{
    return quantModel(forCheck)->entryIndexFor(key, quantId);
}

QString ModelInstaller::useQuant(const QString &key, const QString &quantId, bool forCheck)
{
    const int index = installedIndexFor(key, quantId, forCheck);
    if (index < 0)
        return tr("This quantization is not installed");
    return setActiveModel(index, forCheck);
}

void ModelInstaller::downloadQuant(const QString &key, const QString &quantId, bool forCheck)
{
    if (m_busy)
        return;
    const QList<ModelPreset> &presets = presetsForRole(forCheck);
    const int preset = quantModel(forCheck)->presetIndexFor(key, quantId);
    if (preset < 0 || preset >= presets.size()) {
        setStatusMessage(tr("This quantization is not available for download"));
        setState(State::Error);
        return;
    }
    m_transaction->prepare(presets.at(preset), forCheck);
}

QString ModelInstaller::removeQuant(const QString &key, const QString &quantId, bool forCheck)
{
    const int index = installedIndexFor(key, quantId, forCheck);
    if (index < 0)
        return tr("This quantization is not installed");
    return removeModel(index);
}

QString ModelInstaller::removeModelRow(const QString &key, bool forCheck)
{
    QList<int> targets = quantModel(forCheck)->entryIndexesFor(key);
    if (targets.isEmpty())
        return tr("This model is not installed");

    const RuntimePaths paths = m_installState.paths();
    const bool ready = m_runtime.state() == RuntimeState::Ready;
    for (int index : std::as_const(targets)) {
        const ModelEntry &entry = m_installed.at(index);
        const bool active = !entry.modelPath.isEmpty() && (entry.modelPath == m_settings.launchModelPath() || entry.modelPath == m_settings.checkLaunchModelPath());
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

QString ModelInstaller::openQuantFolder(const QString &key, const QString &quantId, bool forCheck)
{
    const int index = installedIndexFor(key, quantId, forCheck);
    if (index < 0)
        return tr("This quantization is not installed");
    return openModelFolder(index);
}

QString ModelInstaller::removeModel(int index)
{
    if (index < 0 || index >= m_installed.size())
        return tr("Invalid model selection");
    const ModelEntry &e = m_installed.at(index);
    const bool active = !e.modelPath.isEmpty() && (e.modelPath == m_settings.launchModelPath() || e.modelPath == m_settings.checkLaunchModelPath());
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
    m_transaction->cancel();
}

}  // namespace llocr