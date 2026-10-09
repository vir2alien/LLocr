#include "runtime/LaunchProfileStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSet>

#include "config/ProfileStore.h"
#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "core/ModelProfiles.h"
#include "runtime/ReleaseCatalog.h"

#include <algorithm>

namespace llocr {

namespace {
const QString kUserFileName = QStringLiteral("serverLaunch.json");
const QString kLegacyCheckFileName = QStringLiteral("serverLaunchValidate.json");
}  // namespace

LaunchProfileStore::LaunchProfileStore(SettingsStore &settings, const QString &builtInPath, QObject *parent)
    : QObject(parent), m_settings(settings), m_profiles(new ProfileStore<LaunchProfile>(builtInPath, kSchemaVersion, QStringLiteral("LaunchProfileStore"))), m_model(new LaunchParametersModel(this))
{
    m_profiles->setUserPath(QDir(RuntimePaths::fromSettings(m_settings).profilesDir()).filePath(kUserFileName));

    QString readError;
    QJsonParseError parseError{};
    QFile builtIn(builtInPath);
    if (builtIn.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(builtIn.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject())
            m_policy = LaunchProfile::parsePolicy(doc.object(), readError);
        else
            readError = parseError.errorString();
    } else {
        readError = builtIn.errorString();
    }
    if (!readError.isEmpty())
        qWarning("LaunchProfileStore: cannot load the launch profiles %s: %s", qUtf8Printable(builtInPath), qUtf8Printable(readError));

    m_profiles->reloadUserProfiles();

    // The draft (and the user copy) is the platform layer alone: the settings
    // table edits the machine's hardware parameters, everything else the app
    // and the model profiles own. A user copy carrying any other name — a
    // leftover of an older shape, or a hand edit — is dropped wholesale.
    QSet<QString> known;
    for (const LaunchProfile &preset : m_profiles->builtIn()) {
        for (const LaunchParameter &p : preset.parameters)
            known.insert(p.name);
    }
    const int dropped = m_profiles->dropUserProfiles([&known](const QString &, const LaunchProfile &profile) {
        for (const LaunchParameter &p : profile.parameters) {
            if (!known.contains(p.name))
                return true;
        }
        return false;
    });
    if (dropped > 0)
        qWarning("LaunchProfileStore: %d user launch profile(s) carried parameters outside the platform layer (policy, model and generation values are not the user's to store); the stale copies were "
                 "dropped",
                 dropped);

    const QString legacy = QFileInfo(m_profiles->userPath()).absoluteDir().filePath(kLegacyCheckFileName);
    if (QFile::exists(legacy) && !QFile::remove(legacy))
        qWarning("LaunchProfileStore: cannot remove the obsolete %s", qUtf8Printable(legacy));

    connect(&m_settings, &SettingsStore::runtimeBackendChanged, this, &LaunchProfileStore::ensureProfileResolved);
    connect(&m_settings, &SettingsStore::modelRecipeIdChanged, this, [this] { emit profileChanged(); });
    connect(&m_settings, &SettingsStore::checkRequestProfileIdChanged, this, [this] { emit profileChanged(); });
    ensureProfileResolved();
    reloadDraft();
}

bool LaunchProfileStore::hasUserProfile() const
{
    return m_profiles->hasUserProfile();
}

QString LaunchProfileStore::targetBackend() const
{
    const QString configured = m_settings.runtimeBackend();
    return configured.isEmpty() ? ReleaseCatalog::detectPlatform().backend : configured;
}

const LaunchProfile *LaunchProfileStore::findPreset(const QString &id) const
{
    return m_profiles->findBuiltIn(id);
}

bool LaunchProfileStore::presetMatches(const LaunchProfile &preset, const QString &backend, const QString &osTag) const
{
    const bool backendOk = preset.backend.isEmpty() || preset.backend == backend;
    const bool osOk = preset.os.isEmpty() || preset.os == osTag;
    return backendOk && osOk;
}

LaunchProfile LaunchProfileStore::compose(const LaunchProfile &profile, const QString &modelId, const QString &role) const
{
    LaunchProfile out = profile;

    QList<LaunchParameter> combined = m_policy + profile.parameters;
    if (!modelId.isEmpty()) {
        const QList<LaunchParameter> modelLayer = ModelProfiles::launchFor(m_modelProfiles, modelId, role);
        for (const LaunchParameter &p : modelLayer) {
            bool replaced = false;
            for (LaunchParameter &existing : combined) {
                if (existing.name == p.name) {
                    existing = p;
                    replaced = true;
                    break;
                }
            }
            if (!replaced)
                combined.append(p);
        }
    }

    out.parameters = combined;
    int order = 1;
    for (LaunchParameter &parameter : out.parameters)
        parameter.order = order++;
    out.sortByOrder();
    return out;
}

void LaunchProfileStore::ensureProfileResolved()
{
    const QString resolved = activeProfileId();
    if (resolved != m_settings.launchProfileId()) {
        m_settings.setLaunchProfileId(resolved);
        emit activeProfileChanged();
        emit profileChanged();
    }
    if (resolved != m_draftProfileId)
        reloadDraft();
}

QString LaunchProfileStore::activeProfileId() const
{
    const QString stored = m_settings.launchProfileId();
    const PlatformInfo platform = ReleaseCatalog::detectPlatform();
    const QString backend = targetBackend();
    const QString osTag = platform.osTag;

    if (const LaunchProfile *storedPreset = findPreset(stored); storedPreset && presetMatches(*storedPreset, backend, osTag))
        return stored;

    for (const LaunchProfile &p : m_profiles->builtIn()) {
        if (presetMatches(p, backend, osTag))
            return p.id;
    }
    for (const LaunchProfile &p : m_profiles->builtIn()) {
        if (p.backend.isEmpty() || p.backend == backend)
            return p.id;
    }
    return stored;
}

void LaunchProfileStore::setModelProfiles(const QList<ModelProfiles::Profile> &profiles)
{
    m_modelProfiles = profiles;
    emit profileChanged();
}

bool LaunchProfileStore::modelNotInCatalog(const QString &modelId) const
{
    return ModelProfiles::find(m_modelProfiles, modelId) == nullptr;
}

bool LaunchProfileStore::modelProfileMissing() const
{
    return modelNotInCatalog(modelIdForRole(QStringLiteral("ocr")));
}

bool LaunchProfileStore::checkModelProfileMissing() const
{
    return modelNotInCatalog(modelIdForRole(QStringLiteral("blockRecognition")));
}

bool LaunchProfileStore::decisionModelProfileMissing() const
{
    return modelNotInCatalog(modelIdForRole(QStringLiteral("decision")));
}

QString LaunchProfileStore::modelRuntimeNote() const
{
    return ModelProfiles::runtimeNoteFor(m_modelProfiles, modelIdForRole(QStringLiteral("ocr")));
}

QString LaunchProfileStore::checkModelRuntimeNote() const
{
    return ModelProfiles::runtimeNoteFor(m_modelProfiles, modelIdForRole(QStringLiteral("blockRecognition")));
}

QString LaunchProfileStore::decisionModelRuntimeNote() const
{
    return ModelProfiles::runtimeNoteFor(m_modelProfiles, modelIdForRole(QStringLiteral("decision")));
}

QString LaunchProfileStore::modelIdForRole(const QString &role) const
{
    if (role == QLatin1String("blockRecognition"))
        return m_settings.checkRequestProfileId();
    if (role == QLatin1String("decision"))
        return m_settings.decisionRequestProfileId();
    return m_settings.modelRecipeId();
}

void LaunchProfileStore::composeDraft()
{
    // The table shows the platform layer alone: the machine's hardware
    // parameters, every row editable. The policy and the model's own rows are
    // the app's and the weights' — they reach the server through compose(),
    // not through the settings.
    m_model->resetFrom(m_profiles->merged(m_draftProfileId).parameters);
}

LaunchProfile LaunchProfileStore::activeProfile() const
{
    return activeProfile(m_settings.modelRecipeId(), QStringLiteral("ocr"));
}

LaunchProfile LaunchProfileStore::activeProfile(const QString &modelId, const QString &role) const
{
    return compose(m_profiles->merged(activeProfileId()), modelId, role);
}

void LaunchProfileStore::reloadDraft()
{
    m_draftProfileId = activeProfileId();
    composeDraft();
    emit draftProfileChanged();
}

bool LaunchProfileStore::setDraftValue(int row, const QString &text)
{
    return m_model->setValue(row, text);
}

void LaunchProfileStore::saveDraft()
{
    if (m_draftProfileId.isEmpty())
        return;

    LaunchProfile draft;
    draft.id = m_draftProfileId;
    draft.parameters = m_model->parameters();
    draft.sortByOrder();
    if (const LaunchProfile *preset = findPreset(m_draftProfileId)) {
        draft.name = preset->name;
        draft.os = preset->os;
        draft.backend = preset->backend;
        draft.description = preset->description;
    }

    const bool fileChanged = m_profiles->putUserProfile(draft);

    if (m_settings.launchProfileId() != m_draftProfileId) {
        m_settings.setLaunchProfileId(m_draftProfileId);
        emit activeProfileChanged();
    }
    if (fileChanged)
        emit profileChanged();
}

void LaunchProfileStore::loadDefaultDraft()
{
    const LaunchProfile *preset = findPreset(m_draftProfileId);
    if (!preset) {
        m_model->resetFrom(QList<LaunchParameter>());
        return;
    }
    m_model->resetFrom(preset->parameters);
}

void LaunchProfileStore::resetToDefaults()
{
    m_profiles->resetToBuiltIn();
    m_settings.setLaunchProfileId(QString());
    ensureProfileResolved();
    reloadDraft();
    emit profileChanged();
}

}  // namespace llocr
