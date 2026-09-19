#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include "app/LaunchParametersModel.h"

class QAbstractListModel;

namespace llocr {

class SettingsStore;

class LaunchProfileStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel *draftModel READ draftModel CONSTANT)
    Q_PROPERTY(QString draftProfileId READ draftProfileId NOTIFY draftProfileChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY activeProfileChanged)
    Q_PROPERTY(QStringList presetIds READ presetIds CONSTANT)
    Q_PROPERTY(QStringList presetNames READ presetNames CONSTANT)

public:
    // The role picks the user-override file and the settings key that stores
    // the active profile id; the built-in profile file is chosen by the caller.
    enum class Role { Ocr, Check };

    explicit LaunchProfileStore(SettingsStore &settings,
                                const QString &builtInPath =
                                    QString::fromUtf8(LaunchProfile::kBuiltInPath),
                                Role role = Role::Ocr,
                                QObject *parent = nullptr);

    QAbstractListModel *draftModel() const { return m_model; }
    QString draftProfileId() const { return m_draftProfileId; }
    QStringList presetIds() const;
    QStringList presetNames() const;
    QString activeProfileId() const;
    LaunchProfile activeProfile() const;

    Q_INVOKABLE bool hasUserProfile() const;
    Q_INVOKABLE void reloadDraft();
    Q_INVOKABLE void selectDraftProfile(const QString &id);
    Q_INVOKABLE bool setDraftValue(int row, const QString &text);
    Q_INVOKABLE bool appendDraftParameter(const QString &name, const QString &text);
    Q_INVOKABLE void removeDraftRow(int row);
    Q_INVOKABLE void saveDraft();
    Q_INVOKABLE void loadDefaultDraft();
    Q_INVOKABLE void resetToDefaults();
    void setActiveProfileNumber(const QString &name, double value);

signals:
    void profileChanged();
    void draftProfileChanged();
    void activeProfileChanged();

private:
    QString userPath() const;
    void reloadUserProfiles();
    void persistUserProfiles();
    const LaunchProfile *findPreset(const QString &id) const;
    void ensureProfileResolved();
    bool presetMatches(const LaunchProfile &preset, const QString &backend,
                       const QString &osTag) const;

private:
    SettingsStore &m_settings;
    Role m_role;
    QList<LaunchProfile> m_presets;
    QHash<QString, LaunchProfile> m_userProfiles;  // by preset id
    QString m_draftProfileId;
    LaunchParametersModel *m_model;
};

}  // namespace llocr
