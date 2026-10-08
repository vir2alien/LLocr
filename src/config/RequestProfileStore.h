#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include "config/RequestParametersModel.h"
#include "config/RequestProfileListModel.h"
#include "core/ModelProfiles.h"

class QAbstractListModel;

namespace llocr {

class SettingsStore;
template <typename T> class ProfileStore;

class RequestProfileStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel *draftModel READ draftModel CONSTANT)
    Q_PROPERTY(QAbstractListModel *profileModel READ profileModel CONSTANT)
    Q_PROPERTY(QString draftProfileId READ draftProfileId NOTIFY draftProfileChanged)

public:
    enum class Role {
        Ocr,
        Check,
    };

    explicit RequestProfileStore(SettingsStore &settings, Role role = Role::Ocr, QObject *parent = nullptr);
    void setModelProfiles(const QList<ModelProfiles::Profile> &profiles);

    QAbstractListModel *draftModel() const { return m_model; }
    QAbstractListModel *profileModel() const { return m_profileModels; }

    QString draftProfileId() const { return m_draftProfileId; }

    QString activeProfileId() const;
    RequestProfile activeProfile() const;

    Q_INVOKABLE bool hasUserProfile() const;
    Q_INVOKABLE void reloadDraft();
    Q_INVOKABLE bool setDraftValue(int row, const QString &text);
    Q_INVOKABLE void saveDraft();
    Q_INVOKABLE void loadDefaultDraft();
    Q_INVOKABLE void resetToDefaults();

signals:
    void profileChanged();
    void draftProfileChanged();

private:
    SettingsStore &m_settings;
    Role m_role;
    ProfileStore<RequestProfile> *m_profiles;
    QList<ModelProfiles::Profile> m_modelProfiles;
    QString m_draftProfileId;
    RequestParametersModel *m_model = nullptr;
    RequestProfileListModel *m_profileModels = nullptr;
    void loadBuiltInFromModelProfiles();
    void followActiveProfile();
};

}  // namespace llocr
