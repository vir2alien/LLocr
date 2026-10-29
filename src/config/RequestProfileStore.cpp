#include "config/RequestProfileStore.h"

#include "config/ProfileStore.h"
#include "config/RequestProfileListModel.h"
#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"
#include "core/ModelProfiles.h"

#include <QDir>

namespace llocr {

namespace {
constexpr int kSchemaVersion = 2;

QString userFileName(ConnectionRole role)
{
    switch (role) {
    case ConnectionRole::BlockRecognition:
        return QStringLiteral("requestBlock.json");
    case ConnectionRole::Decision:
        return QStringLiteral("requestDecision.json");
    case ConnectionRole::Layout:
        return QStringLiteral("requestLayout.json");
    case ConnectionRole::Fix:
        return QStringLiteral("requestFix.json");
    }
    return QStringLiteral("requestBlock.json");
}

// User copies written before the task rework carry the old names.
QString legacyFileName(ConnectionRole role)
{
    switch (role) {
    case ConnectionRole::BlockRecognition:
        return QStringLiteral("requestValidate.json");
    default:
        return QString();
    }
}
}  // namespace

RequestProfileStore::RequestProfileStore(SettingsStore &settings, ConnectionRole role, QObject *parent)
    : QObject(parent), m_settings(settings), m_role(role), m_profiles(QString(), kSchemaVersion, QStringLiteral("RequestProfileStore"), QString::fromUtf8(SettingsStore::kDefaultModelRecipeId)),
      m_model(new RequestParametersModel(this)), m_profileModels(new RequestProfileListModel(this))
{
    const QString dir = RuntimePaths::fromSettings(m_settings).profilesDir();
    const QString legacy = legacyFileName(m_role);
    if (!legacy.isEmpty()) {
        const QString legacyPath = QDir(dir).filePath(legacy);
        const QString userPath = QDir(dir).filePath(userFileName(m_role));
        if (QFile::exists(legacyPath) && !QFile::exists(userPath))
            QFile::rename(legacyPath, userPath);
    }
    m_profiles.setUserPath(QDir(dir).filePath(userFileName(m_role)));
    setModelProfiles(ModelProfiles::instance());

    switch (m_role) {
    case ConnectionRole::BlockRecognition:
        connect(&m_settings, &SettingsStore::modelRecipeIdChanged, this, [this] { followActiveProfile(); });
        break;
    case ConnectionRole::Decision:
        connect(&m_settings, &SettingsStore::decisionRequestProfileIdChanged, this, [this] { followActiveProfile(); });
        break;
    case ConnectionRole::Layout:
        connect(&m_settings, &SettingsStore::layoutRequestProfileIdChanged, this, [this] { followActiveProfile(); });
        break;
    case ConnectionRole::Fix:
        connect(&m_settings, &SettingsStore::fixRequestProfileIdChanged, this, [this] { followActiveProfile(); });
        break;
    }
}

void RequestProfileStore::setModelProfiles(const QList<ModelProfiles::Profile> &profiles)
{
    m_modelProfiles = profiles;

    const QList<ModelProfiles::Profile> roleProfiles = ModelProfiles::forRole(profiles, connectionRoleName(m_role));
    QList<RequestProfile> builtIn;
    for (const ModelProfiles::Profile &profile : roleProfiles) {
        RequestProfile request;
        request.id = profile.id;
        request.parameters = ModelProfiles::requestWithMaxOutput(profiles, profile.id, connectionRoleName(m_role));
        request.sortByOrder();
        builtIn.append(request);
    }

    m_profileModels->resetFrom(roleProfiles);
    m_profiles.setBuiltIn(builtIn);
    m_profiles.reloadUserProfiles();
    reloadDraft();
}

bool RequestProfileStore::hasUserProfile() const
{
    return m_profiles.hasUserProfile();
}

QString RequestProfileStore::activeProfileId() const
{
    QString id;
    switch (m_role) {
    case ConnectionRole::BlockRecognition:
        id = m_settings.modelRecipeId();
        break;
    case ConnectionRole::Decision:
        id = m_settings.decisionRequestProfileId();
        break;
    case ConnectionRole::Layout:
        id = m_settings.layoutRequestProfileId();
        break;
    case ConnectionRole::Fix:
        id = m_settings.fixRequestProfileId();
        break;
    }
    if (m_profiles.isKnown(id))
        return id;
    if (!m_profiles.builtIn().isEmpty())
        return m_profiles.builtIn().constFirst().id;
    return id;
}

RequestProfile RequestProfileStore::activeProfile() const
{
    return m_profiles.merged(activeProfileId());
}

void RequestProfileStore::reloadDraft()
{
    m_draftProfileId = activeProfileId();
    m_model->resetFrom(m_profiles.merged(m_draftProfileId).parameters);
    emit draftProfileChanged();
}

void RequestProfileStore::followActiveProfile()
{
    if (activeProfileId() != m_draftProfileId)
        reloadDraft();
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

    if (!m_profiles.putUserProfile(draft))
        return;
    emit profileChanged();
}

void RequestProfileStore::loadDefaultDraft()
{
    if (const RequestProfile *builtIn = m_profiles.findBuiltIn(m_draftProfileId))
        m_model->resetFrom(builtIn->parameters);
    else
        m_model->resetFrom(QList<RequestParameter>());
}

void RequestProfileStore::resetToDefaults()
{
    m_profiles.removeUserProfile(m_draftProfileId);
    reloadDraft();
    emit profileChanged();
}

}  // namespace llocr
