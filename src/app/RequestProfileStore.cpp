#include "app/RequestProfileStore.h"

#include <QDir>

#include "app/ProfileStore.h"
#include "app/SettingsStore.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

namespace {
constexpr int kSchemaVersion = 2;
}  // namespace

RequestProfileStore::RequestProfileStore(SettingsStore &settings,
                                         const QString &builtInPath,
                                         Role role,
                                         QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_role(role)
    , m_profiles(new ProfileStore<RequestProfile>(
          builtInPath,
          role == Role::Check ? QStringLiteral("requestValidate.json")
                              : QStringLiteral("request.json"),
          kSchemaVersion, QStringLiteral("RequestProfileStore"),
          // A legacy profile file may carry no id: the entry belongs to the
          // default model adapter (ADR 110).
          QString::fromUtf8(SettingsStore::kDefaultModelRecipeId)))
    , m_model(new RequestParametersModel(this))
{
    m_profiles->setUserPath(
        QDir(RuntimePaths(m_settings.runtimeRootDir(), m_settings.runtimeModelsDir())
                 .profilesDir())
            .filePath(role == Role::Check ? QStringLiteral("requestValidate.json")
                                          : QStringLiteral("request.json")));
    // A built-in profile without an id belongs to the default model adapter too.
    const QString fallback = QString::fromUtf8(SettingsStore::kDefaultModelRecipeId);
    for (RequestProfile &profile : m_profiles->mutableBuiltIn()) {
        if (profile.id.isEmpty())
            profile.id = fallback;
    }
    // The user path depends on the settings, so the copy is loaded here rather
    // than in the ProfileStore constructor (ADR 117).
    m_profiles->reloadUserProfiles();
    reloadDraft();
}

bool RequestProfileStore::hasUserProfile() const
{
    return m_profiles->hasUserProfile();
}

QString RequestProfileStore::activeProfileId() const
{
    // The check role always had its own key; the OCR role now does too, so the
    // sampling profile is chosen independently of the model adapter (ADR 110).
    // An empty value keeps the historical behaviour: follow the model.
    const QString id = m_role == Role::Check
                           ? m_settings.checkRequestProfileId()
                           : m_settings.requestProfileId().isEmpty()
                                 ? m_settings.modelRecipeId()
                                 : m_settings.requestProfileId();
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

void RequestProfileStore::selectDraftProfile(const QString &id)
{
    if (id == m_draftProfileId)
        return;
    if (!m_profiles->isKnown(id))
        return;
    m_draftProfileId = id;
    m_model->resetFrom(m_profiles->merged(id).parameters);
    emit draftProfileChanged();
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
