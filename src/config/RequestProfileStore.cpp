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
    return role == RequestProfileStore::Role::Check ? QStringLiteral("requestValidate.json") : QStringLiteral("request.json");
}

QString roleName(RequestProfileStore::Role role)
{
    return role == RequestProfileStore::Role::Check ? QStringLiteral("check") : QStringLiteral("ocr");
}
}  // namespace

RequestProfileStore::RequestProfileStore(SettingsStore &settings, Role role, QObject *parent)
    : QObject(parent), m_settings(settings), m_role(role),
      m_profiles(new ProfileStore<RequestProfile>(QString(), kSchemaVersion, QStringLiteral("RequestProfileStore"), QString::fromUtf8(SettingsStore::kDefaultModelRecipeId))),
      m_model(new RequestParametersModel(this)), m_profileModels(new RequestProfileListModel(this))
{
    m_profiles->setUserPath(QDir(RuntimePaths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir()).profilesDir()).filePath(userFileName(role)));
    setModelProfiles(ModelProfiles::instance());

    if (m_role == Role::Check) {
        connect(&m_settings, &SettingsStore::checkRequestProfileIdChanged, this, [this] { followActiveProfile(); });
    } else {
        connect(&m_settings, &SettingsStore::modelRecipeIdChanged, this, [this] { followActiveProfile(); });
        connect(&m_settings, &SettingsStore::requestProfileIdChanged, this, [this] { followActiveProfile(); });
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
    const QString id = m_role == Role::Check ? m_settings.checkRequestProfileId() : m_settings.requestProfileId().isEmpty() ? m_settings.modelRecipeId() : m_settings.requestProfileId();
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

bool RequestProfileStore::appendDraftRow(const QString &name, const QString &text)
{
    return m_model->appendRow(name, text);
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
