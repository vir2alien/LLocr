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

namespace llocr {

namespace {
const QString kUserFileName = QStringLiteral("serverLaunch.json");
const QString kLegacyCheckFileName = QStringLiteral("serverLaunchValidate.json");
}  // namespace

LaunchProfileStore::LaunchProfileStore(SettingsStore &settings, const QString &builtInPath, QObject *parent)
    : QObject(parent), m_settings(settings), m_profiles(new ProfileStore<LaunchProfile>(builtInPath, kUserFileName, kSchemaVersion, QStringLiteral("LaunchProfileStore"))),
      m_model(new LaunchParametersModel(this))
{
    m_profiles->setUserPath(QDir(RuntimePaths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir()).profilesDir()).filePath(kUserFileName));

    QString readError;
    QJsonParseError parseError{};
    QFile builtIn(builtInPath);
    if (builtIn.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(builtIn.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            m_policy = LaunchProfile::parsePolicy(doc.object(), readError);
            if (readError.isEmpty())
                m_fallback = LaunchProfile::parseFallback(doc.object(), readError);
        } else {
            readError = parseError.errorString();
        }
    } else {
        readError = builtIn.errorString();
    }
    if (!readError.isEmpty())
        qWarning("LaunchProfileStore: cannot load the launch profiles %s: %s", qUtf8Printable(builtInPath), qUtf8Printable(readError));

    m_model->setLockedPrefix(m_policy.size());

    m_profiles->reloadUserProfiles();

    QSet<QString> known;
    for (const LaunchParameter &p : m_policy)
        known.insert(p.name);
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
        qWarning("LaunchProfileStore: %d user launch profile(s) carried parameters the layers no longer own (they now come from the model); the stale rows were dropped", dropped);

    const QString legacy = QFileInfo(m_profiles->userPath()).absoluteDir().filePath(kLegacyCheckFileName);
    if (QFile::exists(legacy) && !QFile::remove(legacy))
        qWarning("LaunchProfileStore: cannot remove the obsolete %s", qUtf8Printable(legacy));

    connect(&m_settings, &SettingsStore::runtimeBackendChanged, this, &LaunchProfileStore::ensureProfileResolved);
    connect(&m_settings, &SettingsStore::modelRecipeIdChanged, this, &LaunchProfileStore::profileChanged);
    connect(&m_settings, &SettingsStore::checkRequestProfileIdChanged, this, &LaunchProfileStore::profileChanged);
    ensureProfileResolved();
    reloadDraft();
}

bool LaunchProfileStore::hasUserProfile() const
{
    return m_profiles->hasUserProfile();
}

QStringList LaunchProfileStore::presetIds() const
{
    QStringList ids;
    for (const LaunchProfile &p : m_profiles->builtIn())
        ids.append(p.id);
    return ids;
}

QStringList LaunchProfileStore::presetNames() const
{
    QStringList names;
    for (const LaunchProfile &p : m_profiles->builtIn())
        names.append(p.name);
    return names;
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
        if (modelLayer.isEmpty())
            combined += m_fallback;
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
}

QString LaunchProfileStore::activeProfileId() const
{
    const QString stored = m_settings.launchProfileId();
    const PlatformInfo platform = ReleaseCatalog::detectPlatform();
    QString backend = m_settings.runtimeBackend();
    if (backend.isEmpty())
        backend = platform.backend;
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

bool LaunchProfileStore::modelLayerMissing(const QString &modelId, const QString &role) const
{
    return ModelProfiles::launchFor(m_modelProfiles, modelId, role).isEmpty();
}

bool LaunchProfileStore::modelProfileMissing() const
{
    return modelLayerMissing(m_settings.modelRecipeId(), QStringLiteral("ocr"));
}

bool LaunchProfileStore::checkModelProfileMissing() const
{
    return modelLayerMissing(m_settings.checkRequestProfileId(), QStringLiteral("check"));
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
    m_model->resetFrom(compose(m_profiles->merged(m_draftProfileId), QString(), QString()).parameters);
    emit draftProfileChanged();
}

void LaunchProfileStore::selectDraftProfile(const QString &id)
{
    if (!findPreset(id) || id == m_draftProfileId)
        return;
    m_draftProfileId = id;
    m_model->resetFrom(compose(m_profiles->merged(id), QString(), QString()).parameters);
    emit draftProfileChanged();
}

bool LaunchProfileStore::setDraftValue(int row, const QString &text)
{
    return m_model->setValue(row, text);
}

bool LaunchProfileStore::appendDraftParameter(const QString &name, const QString &text)
{
    return m_model->appendRow(name, text);
}

void LaunchProfileStore::removeDraftRow(int row)
{
    m_model->removeRow(row);
}

void LaunchProfileStore::saveDraft()
{
    if (m_draftProfileId.isEmpty())
        return;

    const int policyRows = m_policy.size();
    const QList<LaunchParameter> draftRows = m_model->parameters();
    if (draftRows.size() < policyRows)
        return;

    LaunchProfile draft;
    draft.id = m_draftProfileId;
    draft.parameters = draftRows.mid(policyRows);
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
    if (const LaunchProfile *preset = findPreset(m_draftProfileId))
        m_model->resetFrom(compose(*preset, QString(), QString()).parameters);
    else
        m_model->resetFrom(QList<LaunchParameter>());
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
