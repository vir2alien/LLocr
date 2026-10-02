#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include "core/LaunchProfile.h"
#include "core/ModelProfiles.h"
#include "runtime/LaunchParametersModel.h"

class QAbstractListModel;

namespace llocr {

class SettingsStore;
template <typename T> class ProfileStore;

class LaunchProfileStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel *draftModel READ draftModel CONSTANT)
    Q_PROPERTY(QString draftProfileId READ draftProfileId NOTIFY draftProfileChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY activeProfileChanged)
    Q_PROPERTY(bool modelProfileMissing READ modelProfileMissing NOTIFY profileChanged)
    Q_PROPERTY(bool checkModelProfileMissing READ checkModelProfileMissing NOTIFY profileChanged)
    Q_PROPERTY(QString modelRuntimeNote READ modelRuntimeNote NOTIFY profileChanged)
    Q_PROPERTY(QString checkModelRuntimeNote READ checkModelRuntimeNote NOTIFY profileChanged)

public:
    explicit LaunchProfileStore(SettingsStore &settings, const QString &builtInPath = QString::fromUtf8(LaunchProfile::kBuiltInPath), QObject *parent = nullptr);

    QAbstractListModel *draftModel() const { return m_model; }
    QString draftProfileId() const { return m_draftProfileId; }
    QString activeProfileId() const;
    LaunchProfile activeProfile() const;
    LaunchProfile activeProfile(const QString &modelId, const QString &role) const;

    bool modelProfileMissing() const;
    bool checkModelProfileMissing() const;

    QString modelRuntimeNote() const;
    QString checkModelRuntimeNote() const;
    void setModelProfiles(const QList<ModelProfiles::Profile> &profiles);

    Q_INVOKABLE bool hasUserProfile() const;
    Q_INVOKABLE void reloadDraft(const QString &role = QString());
    Q_INVOKABLE bool setDraftValue(int row, const QString &text);
    Q_INVOKABLE bool appendDraftParameter(const QString &name, const QString &text);
    Q_INVOKABLE void removeDraftRow(int row);
    Q_INVOKABLE void saveDraft();
    Q_INVOKABLE void loadDefaultDraft();
    Q_INVOKABLE void resetToDefaults();

signals:
    void profileChanged();
    void draftProfileChanged();
    void activeProfileChanged();

private:
    static constexpr int kSchemaVersion = 1;

    const LaunchProfile *findPreset(const QString &id) const;
    void ensureProfileResolved();
    bool presetMatches(const LaunchProfile &preset, const QString &backend, const QString &osTag) const;
    LaunchProfile compose(const LaunchProfile &profile, const QString &modelId, const QString &role) const;
    bool modelNotInCatalog(const QString &modelId) const;
    QString targetBackend() const;

    QString modelIdForRole(const QString &role) const;
    QSet<QString> m_policyNames() const;
    QSet<QString> profileOwnedNames(const QString &role) const;
    void composeDraft();
    void modelChangedForRole(const QString &role);

    SettingsStore &m_settings;
    ProfileStore<LaunchProfile> *m_profiles;
    QList<LaunchParameter> m_policy;
    QList<LaunchParameter> m_fallback;
    QList<ModelProfiles::Profile> m_modelProfiles;
    QString m_draftProfileId;
    QString m_draftRole = QStringLiteral("ocr");
    LaunchParametersModel *m_model = nullptr;
};

}  // namespace llocr
