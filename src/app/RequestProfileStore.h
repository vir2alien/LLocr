#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include "app/RequestParametersModel.h"

class QAbstractListModel;

namespace llocr {

class SettingsStore;
template <typename T>
class ProfileStore;

class RequestProfileStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel *draftModel READ draftModel CONSTANT)
    Q_PROPERTY(QString draftProfileId READ draftProfileId NOTIFY draftProfileChanged)

public:
    enum class Role { Ocr, Check, };

    explicit RequestProfileStore(SettingsStore &settings,
                                 const QString &builtInPath =
                                     QString::fromUtf8(RequestProfile::kBuiltInPath),
                                 Role role = Role::Ocr,
                                 QObject *parent = nullptr);

    QAbstractListModel *draftModel() const { return m_model; }

    QString draftProfileId() const { return m_draftProfileId; }

    QString activeProfileId() const;
    RequestProfile activeProfile() const;

    Q_INVOKABLE bool hasUserProfile() const;
    Q_INVOKABLE void reloadDraft();
    Q_INVOKABLE void selectDraftProfile(const QString &id);
    Q_INVOKABLE bool setDraftValue(int row, const QString &text);
    Q_INVOKABLE bool appendDraftRow(const QString &name, const QString &text);
    Q_INVOKABLE void saveDraft();
    Q_INVOKABLE void loadDefaultDraft();
    Q_INVOKABLE void resetToDefaults();

signals:
    void profileChanged();
    void draftProfileChanged();

private:
    SettingsStore &m_settings;
    Role m_role;
    // The built-in list, the user overrides and their merge (ADR 117).
    ProfileStore<RequestProfile> *m_profiles;
    QString m_draftProfileId;
    RequestParametersModel *m_model;
};

}  // namespace llocr
