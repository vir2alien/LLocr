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

QString userFileName(RequestProfileStore::Role role)
{
    // The check file name predates the role rename (check → blockRecognition);
    // it stays so existing user profiles survive the update.
    switch (role) {
    case RequestProfileStore::Role::BlockRecognition:
        return QStringLiteral("requestValidate.json");
    case RequestProfileStore::Role::Decision:
        return QStringLiteral("requestDecision.json");
    case RequestProfileStore::Role::Ocr:
        break;
    }
    return QStringLiteral("request.json");
}

QString roleName(RequestProfileStore::Role role)
{
    switch (role) {
    case RequestProfileStore::Role::BlockRecognition:
        return QStringLiteral("blockRecognition");
    case RequestProfileStore::Role::Decision:
        return QStringLiteral("decision");
    case RequestProfileStore::Role::Ocr:
        break;
    }
    return QStringLiteral("ocr");
}
}  // namespace

RequestProfileStore::RequestProfileStore(SettingsStore &settings, Role role, QObject *parent)
    : QObject(parent), m_settings(settings), m_role(role),
      m_profiles(new ProfileStore<RequestProfile>(QString(), kSchemaVersion, QStringLiteral("RequestProfileStore"), QString::fromUtf8(SettingsStore::kDefaultModelRecipeId))),
      m_model(new RequestParametersModel(this)), m_profileModels(new RequestProfileListModel(this))
{
    m_profiles->setUserPath(QDir(RuntimePaths::fromSettings(m_settings).profilesDir()).filePath(userFileName(role)));
    setModelProfiles(ModelProfiles::instance());

    if (m_role == Role::BlockRecognition) {
        connect(&m_settings, &SettingsStore::checkRequestProfileIdChanged, this, [this] { followActiveProfile(); });
    } else if (m_role == Role::Decision) {
        connect(&m_settings, &SettingsStore::decisionRequestProfileIdChanged, this, [this] { followActiveProfile(); });
    } else {
        connect(&m_settings, &SettingsStore::modelRecipeIdChanged, this, [this] { followActiveProfile(); });
    }
}

void RequestProfileStore::setModelProfiles(const QList<ModelProfiles::Profile> &profiles)
{
    m_modelProfiles = profiles;

    const QList<ModelProfiles::Profile> roleProfiles = ModelProfiles::forRole(profiles, roleName(m_role));
    QList<RequestProfile> builtIn;
    for (const ModelProfiles::Profile &profile : roleProfiles) {
        RequestProfile request;
        request.id = profile.id;
        request.parameters = ModelProfiles::requestWithMaxOutput(profiles, profile.id, roleName(m_role));
        request.sortByOrder();
        builtIn.append(request);
    }

    m_profileModels->resetFrom(roleProfiles);
    m_profiles->setBuiltIn(builtIn);
    m_profiles->reloadUserProfiles();
    reloadDraft();
}

bool RequestProfileStore::hasUserProfile() const
{
    return m_profiles->hasUserProfile();
}

QString RequestProfileStore::activeProfileId() const
{
    QString id;
    switch (m_role) {
    case Role::BlockRecognition:
        id = m_settings.checkRequestProfileId();
        break;
    case Role::Decision:
        id = m_settings.decisionRequestProfileId();
        break;
    case Role::Ocr:
        id = m_settings.modelRecipeId();
        break;
    }
    if (m_profiles->isKnown(id))
        return id;
    if (!m_profiles->builtIn().isEmpty())
        return m_profiles->builtIn().constFirst().id;
    return id;
}

RequestProfile RequestProfileStore::activeProfile() const
{
    return m_profiles->merged(activeProfileId());
}

void RequestProfileStore::reloadDraft()
{
    m_draftProfileId = activeProfileId();
    m_model->resetFrom(m_profiles->merged(m_draftProfileId).parameters);
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

    if (!m_profiles->putUserProfile(draft))
        return;
    emit profileChanged();
}

void RequestProfileStore::loadDefaultDraft()
{
    if (const RequestProfile *builtIn = m_profiles->findBuiltIn(m_draftProfileId))
        m_model->resetFrom(builtIn->parameters);
    else
        m_model->resetFrom(QList<RequestParameter>());
}

void RequestProfileStore::resetToDefaults()
{
    m_profiles->removeUserProfile(m_draftProfileId);
    reloadDraft();
    emit profileChanged();
}

}  // namespace llocr
