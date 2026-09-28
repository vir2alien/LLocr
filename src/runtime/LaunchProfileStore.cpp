#include "runtime/LaunchProfileStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>

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

    QString policyError;
    QJsonParseError parseError{};
    QFile builtIn(builtInPath);
    if (builtIn.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(builtIn.readAll(), &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject())
            m_policy = LaunchProfile::parsePolicy(doc.object(), policyError);
    } else {
        policyError = builtIn.errorString();
    }
    if (!policyError.isEmpty())
        qWarning("LaunchProfileStore: cannot load the launch policy %s: %s", qUtf8Printable(builtInPath), qUtf8Printable(policyError));
    else if (parseError.error != QJsonParseError::NoError)
        qWarning("LaunchProfileStore: cannot read the launch profiles %s: %s", qUtf8Printable(builtInPath), qUtf8Printable(parseError.errorString()));

    m_model->setLockedPrefix(m_policy.size());

    // The check role used to keep its own user copy. Its rows are a subset of
    // what the merged file now provides, so an existing copy is adopted as the
    // starting point rather than discarded without a word.
    const QString userPath = m_profiles->userPath();
    if (!QFile::exists(userPath)) {
        const QString legacy = QFileInfo(userPath).absoluteDir().filePath(kLegacyCheckFileName);
        if (QFile::exists(legacy)) {
            m_profiles->setUserPath(legacy);
            m_profiles->reloadUserProfiles();
            m_profiles->setUserPath(userPath);
        }
    } else {
        m_profiles->reloadUserProfiles();
    }

    connect(&m_settings, &SettingsStore::runtimeBackendChanged, this, &LaunchProfileStore::ensureProfileResolved);
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
    for (const LaunchParameter &p : ModelProfiles::launchFor(m_modelProfiles, modelId, role)) {
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

    // The layers are concatenated in a fixed order and renumbered, so a row
    // keeps its layer. Sorting on the per-layer `order` alone would interleave
    // them (policy order 1 next to platform order 1) and the policy rows would
    // stop being a contiguous read-only prefix of the draft.
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
}

LaunchProfile LaunchProfileStore::activeProfile() const
{
    return activeProfile(m_settings.modelRecipeId());
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
