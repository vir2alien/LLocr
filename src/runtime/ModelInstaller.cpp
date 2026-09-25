#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>

#include "runtime/RuntimeState.h"

#include "app/LaunchProfileStore.h"
#include "app/SettingsStore.h"
#include "runtime/ModelInstallTransaction.h"
#include "runtime/ModelInstaller.h"
#include "runtime/ModelPresetCatalog.h"
#include "runtime/RuntimeController.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

ModelInstaller::ModelInstaller(SettingsStore &settings, RuntimeController &runtime,
                               LaunchProfileStore &launchProfiles, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_runtime(runtime)
    , m_launchProfiles(launchProfiles)
    , m_transaction(new ModelInstallTransaction(settings, launchProfiles, this))
{
    connect(m_transaction, &ModelInstallTransaction::stateChanged, this,
            [this](int state) { setState(static_cast<State>(state)); });
    connect(m_transaction, &ModelInstallTransaction::busyChanged, this,
            [this](bool busy) { setBusy(busy); });
    connect(m_transaction, &ModelInstallTransaction::progressChanged, this,
            [this](double progress) { setProgress(progress); });
    connect(m_transaction, &ModelInstallTransaction::statusMessageChanged, this,
            [this](const QString &message) { setStatusMessage(message); });
    connect(m_transaction, &ModelInstallTransaction::installedListReplaced, this,
            [this](const QList<ModelEntry> &installed) {
                m_installed = installed;
                emit installedChanged();
            });

    reloadPresetsInternal();
    refreshInstalled();
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
    const RuntimePaths paths(m_settings.runtimeRootDir(),
                             m_settings.runtimeModelsDir());
    const QString modelsDir = paths.modelsDir();

    QString err;
    m_presets = ModelPresetCatalog::load(
        QLatin1String(ModelPresetCatalog::kBuiltInOcrPath),
        QDir(modelsDir).filePath(QStringLiteral("catalog.json")), err);
    if (!err.isEmpty())
        setStatusMessage(err);

    QString validateErr;
    m_presetsValidate = ModelPresetCatalog::load(
        QLatin1String(ModelPresetCatalog::kBuiltInValidatePath),
        QDir(modelsDir).filePath(QStringLiteral("catalogValidate.json")),
        validateErr);
    if (!validateErr.isEmpty())
        setStatusMessage(validateErr);

    emit presetsChanged();
}

void ModelInstaller::refreshInstalled()
{
    QString err;
    bool rebuilt = false;
    const RuntimePaths paths(m_settings.runtimeRootDir(),
                             m_settings.runtimeModelsDir());
    m_installed = ModelRegistry::load(paths.modelsDir(), rebuilt, err);
    if (!err.isEmpty() && !rebuilt)
        setStatusMessage(err);
    m_transaction->setInstalled(m_installed);
    emit installedChanged();
}

void ModelInstaller::rescanRegistry()
{
    QString err;
    const RuntimePaths paths(m_settings.runtimeRootDir(),
                             m_settings.runtimeModelsDir());
    m_installed = ModelRegistry::scanModelsDir(paths.modelsDir());
    ModelRegistry::save(paths.modelsDir(), m_installed, err);
    m_transaction->setInstalled(m_installed);
    refreshInstalled();
}

QVariantMap ModelInstaller::installedInfo(int index, bool forCheck) const
{
    QVariantMap out;
    if (index < 0 || index >= m_installed.size())
        return out;
    const ModelEntry &e = m_installed.at(index);

    QString display = QFileInfo(e.modelPath).completeBaseName();
    const QString q = e.quantization.trimmed();
    if (!q.isEmpty() && display.endsWith(QLatin1Char('-') + q))
        display.chop(q.size() + 1);
    if (display.isEmpty())
        display = e.title.isEmpty() ? e.repo : e.title;
    if (!q.isEmpty())
        display += QLatin1Char(' ') + q;

    out.insert(QStringLiteral("title"), display);
    out.insert(QStringLiteral("path"), e.modelPath);
    out.insert(QStringLiteral("mmprojPath"), e.mmprojPath);
    out.insert(QStringLiteral("size"), QVariant::fromValue(e.byteSize));
    out.insert(QStringLiteral("quantization"), e.quantization);
    out.insert(QStringLiteral("origin"),
               e.origin == ModelOrigin::Managed ? QStringLiteral("managed")
                                                : QStringLiteral("external"));
    out.insert(QStringLiteral("license"), e.license);
    out.insert(QStringLiteral("repo"), e.repo);
    const QString activePath = forCheck ? m_settings.checkLaunchModelPath()
                                        : m_settings.launchModelPath();
    out.insert(QStringLiteral("active"),
               !e.modelPath.isEmpty() && e.modelPath == activePath);
    out.insert(QStringLiteral("parts"), e.parts.size());
    out.insert(QStringLiteral("index"), index);
    return out;
}

int ModelInstaller::roleInstalledCount(bool forCheck) const
{
    int n = 0;
    for (const ModelEntry &e : m_installed) {
        if (matchesRole(e, forCheck))
            ++n;
    }
    return n;
}

QVariantMap ModelInstaller::roleInstalledInfo(int index, bool forCheck) const
{
    int n = -1;
    for (int i = 0; i < m_installed.size(); ++i) {
        if (!matchesRole(m_installed.at(i), forCheck))
            continue;
        if (++n == index)
            return installedInfo(i, forCheck);
    }
    return QVariantMap();
}

bool ModelInstaller::matchesRole(const ModelEntry &e, bool forCheck) const
{
    const bool ocrActive = !e.modelPath.isEmpty()
                           && e.modelPath == m_settings.launchModelPath();
    const bool checkActive = !e.modelPath.isEmpty()
                             && e.modelPath == m_settings.checkLaunchModelPath();

    if (forCheck ? checkActive : ocrActive)
        return true;

    if (!e.roles.isEmpty())
        return e.roles.contains(forCheck ? QStringLiteral("check")
                                         : QStringLiteral("ocr"));

    if (checkActive)
        return false;

    const bool vision = !e.mmprojPath.isEmpty();
    return forCheck ? !vision : vision;
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
        m_settings.forceSave();
        emit installedChanged();
        return QString();
    }

    m_settings.setLaunchModelPath(e.modelPath);
    if (!e.mmprojPath.isEmpty())
        m_settings.setLaunchMmprojPath(e.mmprojPath);
    if (!e.parser.isEmpty())
        m_settings.setParserId(e.parser);
    if (e.ctxSize > 0)
        m_launchProfiles.setActiveProfileNumber(QStringLiteral("ctx-size"),
                                                e.ctxSize);
    m_settings.forceSave();
    emit installedChanged();
    return QString();
}

QString ModelInstaller::activatePreset(int index, bool forCheck)
{
    const QList<ModelPreset> &list = forCheck ? m_presetsValidate : m_presets;
    if (index < 0 || index >= list.size())
        return tr("Invalid model selection");
    const ModelPreset &p = list.at(index);
    const QString modelLeaf = ModelCatalog::leafName(p.model);
    for (int i = 0; i < m_installed.size(); ++i) {
        const ModelEntry &e = m_installed.at(i);
        if (e.repo != p.repo)
            continue;
        if (!e.modelPath.isEmpty()
            && ModelCatalog::leafName(e.modelPath) == modelLeaf)
            return setActiveModel(i, forCheck);
    }
    return tr("The preset is not installed");
}

QString ModelInstaller::removeModel(int index)
{
    if (index < 0 || index >= m_installed.size())
        return tr("Invalid model selection");
    const ModelEntry &e = m_installed.at(index);
    const bool active = !e.modelPath.isEmpty()
                        && (e.modelPath == m_settings.launchModelPath()
                            || e.modelPath == m_settings.checkLaunchModelPath());
    const bool ready = m_runtime.state() == RuntimeState::Ready;
    const RuntimePaths currentPaths(m_settings.runtimeRootDir(),
                                    m_settings.runtimeModelsDir());
    const QString guard =
        ModelRegistry::removalError(e, currentPaths.modelsDir(), active, ready);
    if (!guard.isEmpty())
        return guard;

    QDir d(e.dir);

    bool sharedDir = false;
    for (const ModelEntry &x : std::as_const(m_installed)) {
        if (&x == &e)
            continue;
        if (QFileInfo(x.dir).canonicalFilePath()
            == QFileInfo(e.dir).canonicalFilePath()) {
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
                if (QFileInfo(x.dir).canonicalFilePath()
                        == QFileInfo(e.dir).canonicalFilePath()
                    && x.mmprojPath == e.mmprojPath) {
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

    QList<ModelEntry> updated = m_installed;
    updated.removeIf([&](const ModelEntry &x) { return x.id == e.id; });
    QString saveErr;
    if (!ModelRegistry::save(currentPaths.modelsDir(), updated, saveErr)) {
        refreshInstalled();
        return tr("Model files removed, but the registry could not be saved: %1")
                   .arg(saveErr);
    }
    m_installed = updated;
    m_transaction->setInstalled(m_installed);
    emit installedChanged();
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

bool ModelInstaller::isPresetInstalled(const ModelPreset &p) const
{
    return !presetInstalledModelPath(p).isEmpty();
}

QVariantMap ModelInstaller::presetInfo(int index, bool forCheck) const
{
    const QList<ModelPreset> &list = forCheck ? m_presetsValidate : m_presets;
    QVariantMap out;
    if (index < 0 || index >= list.size())
        return out;
    const ModelPreset &p = list.at(index);
    out.insert(QStringLiteral("id"), p.id);
    out.insert(QStringLiteral("title"), p.title);
    out.insert(QStringLiteral("repo"), p.repo);
    out.insert(QStringLiteral("license"), p.license);
    out.insert(QStringLiteral("ctxSize"), p.ctxSize);
    out.insert(QStringLiteral("minBuild"), p.minBuild);
    out.insert(QStringLiteral("installed"), isPresetInstalled(p));
    const QString targetPath = presetInstalledModelPath(p);
    out.insert(QStringLiteral("active"),
               forCheck ? targetPath == m_settings.checkLaunchModelPath()
                        : targetPath == m_settings.launchModelPath());
    return out;
}

QString ModelInstaller::presetInstalledModelPath(const ModelPreset &p) const
{
    const QString modelLeaf = ModelCatalog::leafName(p.model);
    for (const ModelEntry &e : std::as_const(m_installed)) {
        if (e.repo != p.repo)
            continue;
        if (!e.modelPath.isEmpty()
            && ModelCatalog::leafName(e.modelPath) == modelLeaf)
            return e.modelPath;
    }
    return QString();
}

void ModelInstaller::preparePreset(int index, bool forCheck)
{
    if (m_busy)
        return;
    const QList<ModelPreset> &list = forCheck ? m_presetsValidate : m_presets;
    if (index < 0 || index >= list.size()) {
        setStatusMessage(tr("No preset selected"));
        setState(State::Error);
        return;
    }
    m_transaction->prepare(list.at(index), forCheck);
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