#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include "config/ModelProfiles.h"
#include "core/LaunchProfile.h"
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
    Q_PROPERTY(QStringList presetIds READ presetIds CONSTANT)
    Q_PROPERTY(QStringList presetNames READ presetNames CONSTANT)

public:
    explicit LaunchProfileStore(SettingsStore &settings, const QString &builtInPath = QString::fromUtf8(LaunchProfile::kBuiltInPath), QObject *parent = nullptr);

    QAbstractListModel *draftModel() const { return m_model; }
    QString draftProfileId() const { return m_draftProfileId; }
    QStringList presetIds() const;
    QStringList presetNames() const;
    QString activeProfileId() const;
    LaunchProfile activeProfile() const;
    LaunchProfile activeProfile(const QString &modelId, const QString &role = QString()) const;
    void setModelProfiles(const QList<ModelProfiles::Profile> &profiles);

    Q_INVOKABLE bool hasUserProfile() const;
    Q_INVOKABLE void reloadDraft();
    Q_INVOKABLE void selectDraftProfile(const QString &id);
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

    SettingsStore &m_settings;
    ProfileStore<LaunchProfile> *m_profiles;
    QList<LaunchParameter> m_policy;
    QList<ModelProfiles::Profile> m_modelProfiles;
    QString m_draftProfileId;
    LaunchParametersModel *m_model;
};

}  // namespace llocr
