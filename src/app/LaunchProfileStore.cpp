#include "app/LaunchProfileStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include "app/SettingsStore.h"
#include "runtime/ReleaseCatalog.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

LaunchProfileStore::LaunchProfileStore(SettingsStore &settings,
                                       const QString &builtInPath,
                                       QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_model(new LaunchParametersModel(this))
{
    QString error;
    QFile builtIn(builtInPath);
    if (builtIn.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(builtIn.readAll(),
                                                          &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            error = parseError.errorString();
        else
            m_presets = LaunchProfile::parseFile(doc.object(), error);
    } else {
        error = builtIn.errorString();
    }
    if (!error.isEmpty())
        qWarning("LaunchProfileStore: cannot load built-in profiles %s: %s",
                 qUtf8Printable(builtInPath), qUtf8Printable(error));

    reloadUserProfiles();
    connect(&m_settings, &SettingsStore::runtimeBackendChanged, this,
            &LaunchProfileStore::ensureProfileResolved);
    ensureProfileResolved();
    reloadDraft();
}

QString LaunchProfileStore::userPath() const
{
    return QDir(RuntimePaths(m_settings.runtimeRootDir(),
                             m_settings.runtimeModelsDir())
                    .profilesDir())
        .filePath(QStringLiteral("serverLaunch.json"));
}

bool LaunchProfileStore::hasUserProfile() const
{
    return QFile::exists(userPath());
}

void LaunchProfileStore::reloadUserProfiles()
{
    m_userProfiles.clear();
    QFile userFile(userPath());
    if (!userFile.exists())
        return;
    QString error;
    QJsonParseError parseError{};
    QList<LaunchProfile> parsedProfiles;
    if (userFile.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(userFile.readAll(),
                                                          &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            error = parseError.errorString();
        else
            parsedProfiles = LaunchProfile::parseFile(doc.object(), error);
    } else {
        error = userFile.errorString();
    }
    if (!error.isEmpty())
        qWarning("LaunchProfileStore: cannot load user profiles %s: %s "
                 "(falling back to the built-in presets)",
                 qUtf8Printable(userPath()), qUtf8Printable(error));
    // User copies keep only id + parameters in the map (metadata comes from
    // the built-in preset; the id is the map key anyway).
    QHash<QString, LaunchProfile> cleaned;
    for (const LaunchProfile &p : parsedProfiles) {
        LaunchProfile copy = p;
        if (const LaunchProfile *preset = findPreset(p.id)) {
            copy.name = preset->name;
            copy.os = preset->os;
            copy.backend = preset->backend;
            copy.description = preset->description;
        }
        cleaned.insert(copy.id, copy);
    }
    m_userProfiles = cleaned;
}

void LaunchProfileStore::persistUserProfiles()
{
    if (m_userProfiles.isEmpty()) {
        // No customized presets left: the file must go (a user file exists
        // only while something differs from the built-ins).
        QFile file(userPath());
        if (file.exists() && !file.remove())
            qWarning("LaunchProfileStore: cannot remove user profiles %s: %s",
                     qUtf8Printable(userPath()),
                     qUtf8Printable(file.errorString()));
        return;
    }

    QDir().mkpath(QFileInfo(userPath()).absolutePath());
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    QJsonArray profiles;
    for (const LaunchProfile &p : m_userProfiles)
        profiles.append(p.toJson());
    root.insert(QStringLiteral("profiles"), profiles);

    QSaveFile file(userPath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("LaunchProfileStore: cannot write user profiles %s: %s",
                 qUtf8Printable(userPath()), qUtf8Printable(file.errorString()));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning("LaunchProfileStore: cannot commit user profiles %s: %s",
                 qUtf8Printable(userPath()), qUtf8Printable(file.errorString()));
        return;
    }
}

QStringList LaunchProfileStore::presetIds() const
{
    QStringList ids;
    for (const LaunchProfile &p : m_presets)
        ids.append(p.id);
    return ids;
}

QStringList LaunchProfileStore::presetNames() const
{
    QStringList names;
    for (const LaunchProfile &p : m_presets)
        names.append(p.name);
    return names;
}

const LaunchProfile *LaunchProfileStore::findPreset(const QString &id) const
{
    for (const LaunchProfile &p : m_presets)
        if (p.id == id)
            return &p;
    return nullptr;
}

bool LaunchProfileStore::presetMatches(const LaunchProfile &preset,
                                       const QString &backend,
                                       const QString &osTag) const
{
    const bool backendOk = preset.backend.isEmpty() || preset.backend == backend;
    const bool osOk = preset.os.isEmpty() || preset.os == osTag;
    return backendOk && osOk;
}

QString LaunchProfileStore::activeProfileId() const
{
    const QString stored = m_settings.launchProfileId();
    // No runtime installed yet: fall back to the platform's recommended
    // backend so the resolved profile still makes sense (cpu on Windows/
    // Linux, metal on macOS).
    QString backend = m_settings.runtimeBackend();
    if (backend.isEmpty())
        backend = ReleaseCatalog::detectPlatform().backend;
    const QString osTag = ReleaseCatalog::detectPlatform().osTag;

    if (const LaunchProfile *storedPreset = findPreset(stored);
        storedPreset && presetMatches(*storedPreset, backend, osTag))
        return stored;

    // Best match: backend + os (empty tags match anything), then backend on
    // any os, then keep the stored id (nothing better exists for this
    // backend).
    for (const LaunchProfile &p : m_presets)
        if (presetMatches(p, backend, osTag))
            return p.id;
    for (const LaunchProfile &p : m_presets)
        if (p.backend.isEmpty() || p.backend == backend)
            return p.id;
    return stored;
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

LaunchProfile LaunchProfileStore::activeProfile() const
{
    const QString id = activeProfileId();
    if (const auto it = m_userProfiles.constFind(id); it != m_userProfiles.constEnd())
        return it.value();
    if (const LaunchProfile *preset = findPreset(id))
        return *preset;
    return LaunchProfile();  // no presets at all: core args only
}

void LaunchProfileStore::reloadDraft()
{
    m_draftProfileId = activeProfileId();
    if (const auto it = m_userProfiles.constFind(m_draftProfileId);
        it != m_userProfiles.constEnd())
        m_model->resetFrom(it.value().parameters);
    else if (const LaunchProfile *preset = findPreset(m_draftProfileId))
        m_model->resetFrom(preset->parameters);
    else
        m_model->resetFrom(QList<LaunchParameter>());
    emit draftProfileChanged();
}

void LaunchProfileStore::selectDraftProfile(const QString &id)
{
    if (!findPreset(id) || id == m_draftProfileId)
        return;
    m_draftProfileId = id;
    if (const auto it = m_userProfiles.constFind(id); it != m_userProfiles.constEnd())
        m_model->resetFrom(it.value().parameters);
    else
        m_model->resetFrom(findPreset(id)->parameters);
    emit draftProfileChanged();
}

bool LaunchProfileStore::setDraftValue(int row, const QString &text)
{
    return m_model->setValue(row, text);
}

bool LaunchProfileStore::appendDraftParameter(const QString &name,
                                              const QString &text)
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
        return;  // degenerate: no presets exist at all, nothing to key a copy

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

    // A user copy exists only while the preset differs from the built-in one.
    const bool hadCopy = m_userProfiles.contains(m_draftProfileId);
    const bool matchesPreset =
        findPreset(m_draftProfileId)
            && draft.parametersEqual(*findPreset(m_draftProfileId));
    bool changed = false;
    if (matchesPreset) {
        changed = hadCopy;
        if (hadCopy) {
            m_userProfiles.remove(m_draftProfileId);
            persistUserProfiles();
        }
    } else {
        m_userProfiles.insert(m_draftProfileId, draft);
        persistUserProfiles();
        changed = true;
    }

    if (m_settings.launchProfileId() != m_draftProfileId) {
        m_settings.setLaunchProfileId(m_draftProfileId);
        changed = true;
        emit activeProfileChanged();
    }
    if (changed)
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
    m_userProfiles.remove(m_draftProfileId);
    persistUserProfiles();
    m_settings.setLaunchProfileId(QString());
    ensureProfileResolved();
    reloadDraft();
    emit profileChanged();
}

void LaunchProfileStore::setActiveProfileNumber(const QString &name, double value)
{
    const QString id = activeProfileId();
    LaunchProfile profile = activeProfile();
    LaunchParameter *row = nullptr;
    for (LaunchParameter &p : profile.parameters) {
        if (p.name == name) {
            row = &p;
            break;
        }
    }
    if (!row || row->kind != LaunchValueKind::Number)
        return;  // the profile owns the parameter set; nothing to update
    if (row->value.toDouble() == value)
        return;
    row->value = QVariant(value);
    m_userProfiles.insert(id, profile);
    persistUserProfiles();
    emit profileChanged();
}

}  // namespace llocr
