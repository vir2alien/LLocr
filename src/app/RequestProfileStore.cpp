#include "app/RequestProfileStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "app/SettingsStore.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

namespace {

constexpr int kSchemaVersion = 2;

}  // namespace

RequestProfileStore::RequestProfileStore(SettingsStore &settings,
                                         const QString &builtInPath,
                                         QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_model(new RequestParametersModel(this))
{
    QString error;
    QFile builtIn(builtInPath);
    if (builtIn.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(builtIn.readAll(),
                                                          &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            error = parseError.errorString();
        } else {
            m_profiles = RequestProfile::profilesFromJson(doc.object(), error);
        }
    } else {
        error = builtIn.errorString();
    }
    if (!error.isEmpty())
        qWarning("RequestProfileStore: cannot load built-in profiles %s: %s",
                 qUtf8Printable(builtInPath), qUtf8Printable(error));

    for (RequestProfile &profile : m_profiles) {
        if (profile.id.isEmpty())
            profile.id = QString::fromUtf8(SettingsStore::kDefaultModelRecipeId);
    }

    reloadUserProfiles();
    reloadDraft();
}

QString RequestProfileStore::userPath() const
{
    return QDir(RuntimePaths(m_settings.runtimeRootDir(),
                             m_settings.runtimeModelsDir())
                    .profilesDir())
        .filePath(QStringLiteral("request.json"));
}

bool RequestProfileStore::hasUserProfile() const
{
    return QFile::exists(userPath());
}

void RequestProfileStore::reloadUserProfiles()
{
    m_userProfiles.clear();
    QFile userFile(userPath());
    if (!userFile.exists())
        return;
    QString error;
    QList<RequestProfile> parsedProfiles;
    if (userFile.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(userFile.readAll(),
                                                          &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            error = parseError.errorString();
        else
            parsedProfiles = RequestProfile::profilesFromJson(doc.object(), error);
    } else {
        error = userFile.errorString();
    }
    if (!error.isEmpty())
        qWarning("RequestProfileStore: cannot load user profiles %s: %s "
                 "(falling back to the built-in profiles)",
                 qUtf8Printable(userPath()), qUtf8Printable(error));

    for (const RequestProfile &profile : parsedProfiles) {
        RequestProfile copy = profile;
        if (copy.id.isEmpty())
            copy.id = QString::fromUtf8(SettingsStore::kDefaultModelRecipeId);
        m_userProfiles.insert(copy.id, copy);
    }
}

void RequestProfileStore::persistUserProfiles()
{
    if (m_userProfiles.isEmpty()) {
        QFile file(userPath());
        if (file.exists() && !file.remove())
            qWarning("RequestProfileStore: cannot remove user profiles %s: %s",
                     qUtf8Printable(userPath()),
                     qUtf8Printable(file.errorString()));
        return;
    }

    QDir().mkpath(QFileInfo(userPath()).absolutePath());
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    QJsonArray profiles;
    for (const RequestProfile &profile : m_userProfiles)
        profiles.append(profile.toJson());
    root.insert(QStringLiteral("profiles"), profiles);

    QSaveFile file(userPath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("RequestProfileStore: cannot write user profiles %s: %s",
                 qUtf8Printable(userPath()), qUtf8Printable(file.errorString()));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning("RequestProfileStore: cannot commit user profiles %s: %s",
                 qUtf8Printable(userPath()), qUtf8Printable(file.errorString()));
        return;
    }
}

const RequestProfile *RequestProfileStore::findBuiltIn(const QString &id) const
{
    for (const RequestProfile &profile : m_profiles)
        if (profile.id == id)
            return &profile;
    return nullptr;
}

QString RequestProfileStore::activeProfileId() const
{
    const QString id = m_settings.modelRecipeId();
    if (findBuiltIn(id) || m_userProfiles.contains(id))
        return id;
    if (!m_profiles.isEmpty())
        return m_profiles.constFirst().id;
    return id;
}

RequestProfile RequestProfileStore::mergedProfile(const QString &id) const
{
    static const RequestProfile kEmpty;
    const RequestProfile *builtIn = findBuiltIn(id);
    if (!builtIn && !m_userProfiles.contains(id))
        return kEmpty;
    return RequestProfile::merge(builtIn ? *builtIn : kEmpty,
                                 m_userProfiles.value(id, kEmpty));
}

RequestProfile RequestProfileStore::activeProfile() const
{
    return mergedProfile(activeProfileId());
}

void RequestProfileStore::loadDraftRows()
{
    m_model->resetFrom(mergedProfile(m_draftProfileId).parameters);
}

void RequestProfileStore::reloadDraft()
{
    m_draftProfileId = activeProfileId();
    loadDraftRows();
    emit draftProfileChanged();
}

void RequestProfileStore::selectDraftProfile(const QString &id)
{
    if (id == m_draftProfileId)
        return;
    if (!findBuiltIn(id) && !m_userProfiles.contains(id))
        return;
    m_draftProfileId = id;
    loadDraftRows();
    emit draftProfileChanged();
}

bool RequestProfileStore::setDraftValue(int row, const QString &text)
{
    return m_model->setValue(row, text);
}

void RequestProfileStore::saveDraft()
{
    if (m_draftProfileId.isEmpty())
        return;

    RequestProfile draft;
    draft.id = m_draftProfileId;
    draft.parameters = m_model->parameters();
    draft.sortByOrder();

    bool changed = false;
    const RequestProfile *builtIn = findBuiltIn(m_draftProfileId);
    if (builtIn && draft == *builtIn) {
        if (m_userProfiles.contains(m_draftProfileId)) {
            m_userProfiles.remove(m_draftProfileId);
            persistUserProfiles();
            changed = true;
        }
    } else {
        m_userProfiles.insert(m_draftProfileId, draft);
        persistUserProfiles();
        changed = true;
    }

    if (changed)
        emit profileChanged();
}

void RequestProfileStore::loadDefaultDraft()
{
    const RequestProfile *builtIn = findBuiltIn(m_draftProfileId);
    m_model->resetFrom(builtIn ? builtIn->parameters
                               : QList<RequestParameter>());
}

void RequestProfileStore::resetToDefaults()
{
    if (m_userProfiles.remove(m_draftProfileId))
        persistUserProfiles();
    loadDraftRows();
    emit profileChanged();
}

}  // namespace llocr
