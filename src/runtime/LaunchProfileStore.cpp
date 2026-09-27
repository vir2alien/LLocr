#include "runtime/LaunchProfileStore.h"

#include <QDebug>
#include <QDir>

#include "config/ProfileStore.h"
#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "runtime/ReleaseCatalog.h"

namespace llocr {

LaunchProfileStore::LaunchProfileStore(SettingsStore &settings, const QString &builtInPath, Role role, QObject *parent)
    : QObject(parent), m_settings(settings), m_role(role),
      m_profiles(new ProfileStore<LaunchProfile>(
          builtInPath, role == Role::Check ? QStringLiteral("serverLaunchValidate.json") : QStringLiteral("serverLaunch.json"), kSchemaVersion, QStringLiteral("LaunchProfileStore"))),
      m_model(new LaunchParametersModel(this))
{
    m_profiles->setUserPath(QDir(RuntimePaths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir()).profilesDir())
                                .filePath(role == Role::Check ? QStringLiteral("serverLaunchValidate.json") : QStringLiteral("serverLaunch.json")));
    // The path is only known after the settings are read, so the user copy is
    // loaded now rather than in the ProfileStore constructor.
    m_profiles->reloadUserProfiles();

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

void LaunchProfileStore::ensureProfileResolved()
{
    const QString resolved = activeProfileId();
    if (m_role == Role::Check) {
        if (resolved != m_settings.checkLaunchProfileId()) {
            m_settings.setCheckLaunchProfileId(resolved);
            emit activeProfileChanged();
            emit profileChanged();
        }
        return;
    }
    if (resolved != m_settings.launchProfileId()) {
        m_settings.setLaunchProfileId(resolved);
        emit activeProfileChanged();
        emit profileChanged();
    }
}

QString LaunchProfileStore::activeProfileId() const
{
    const QString stored = m_role == Role::Check ? m_settings.checkLaunchProfileId() : m_settings.launchProfileId();
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

LaunchProfile LaunchProfileStore::activeProfile() const
{
    return m_profiles->merged(activeProfileId());
}

void LaunchProfileStore::reloadDraft()
{
    m_draftProfileId = activeProfileId();
    m_model->resetFrom(m_profiles->merged(m_draftProfileId).parameters);
    emit draftProfileChanged();
}

void LaunchProfileStore::selectDraftProfile(const QString &id)
{
    if (!findPreset(id) || id == m_draftProfileId)
        return;
    m_draftProfileId = id;
    m_model->resetFrom(m_profiles->merged(id).parameters);
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

    // The active profile follows the draft whether or not the *file* changed:
    // selecting a preset that is already built-in still has to take effect.
    const bool fileChanged = m_profiles->putUserProfile(draft);

    if (m_role == Role::Check) {
        if (m_settings.checkLaunchProfileId() != m_draftProfileId) {
            m_settings.setCheckLaunchProfileId(m_draftProfileId);
            emit activeProfileChanged();
        }
    } else if (m_settings.launchProfileId() != m_draftProfileId) {
        m_settings.setLaunchProfileId(m_draftProfileId);
        emit activeProfileChanged();
    }
    if (fileChanged)
        emit profileChanged();
}

void LaunchProfileStore::loadDefaultDraft()
{
    if (const LaunchProfile *preset = findPreset(m_draftProfileId))
        m_model->resetFrom(preset->parameters);
    else
        m_model->resetFrom(QList<LaunchParameter>());
}

void LaunchProfileStore::resetToDefaults()
{
    m_profiles->resetToBuiltIn();
    if (m_role == Role::Check)
        m_settings.setCheckLaunchProfileId(QString());
    else
        m_settings.setLaunchProfileId(QString());
    ensureProfileResolved();
    reloadDraft();
    emit profileChanged();
}

void LaunchProfileStore::setActiveProfileNumber(const QString &name, double value)
{
    const QString id = activeProfileId();
    LaunchProfile profile = m_profiles->merged(id);
    LaunchParameter *row = nullptr;
    for (LaunchParameter &p : profile.parameters) {
        if (p.name == name) {
            row = &p;
            break;
        }
    }
    if (!row || row->kind != LaunchValueKind::Number)
        return;
    if (row->value.toDouble() == value)
        return;
    row->value = QVariant(value);
    m_profiles->putUserProfile(profile);
    emit profileChanged();
}

}  // namespace llocr
