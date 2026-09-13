#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include "app/RequestParametersModel.h"

class QAbstractListModel;

namespace llocr {

class SettingsStore;

/// Owns the OCR request-body profiles: built-in profiles (read-only resource)
/// merged with per-profile user copies
/// (<AppData>/LLocr/profiles/request.json, written only when the user changed
/// something). Profiles are keyed by a unique id.
///
/// Two states are kept apart:
///  - the *active* profile (merged, persisted) — what the recognition pipeline
///    actually sends, read at request time via activeProfile();
///  - the *draft* (RequestParametersModel) — what the settings table shows and
///    edits; commits happen only through saveDraft() (Settings → Save), so
///    Cancel/reopen simply discards the draft.
class RequestProfileStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel *draftModel READ draftModel CONSTANT)
    Q_PROPERTY(QString draftProfileId READ draftProfileId NOTIFY draftProfileChanged)

public:
    /// `builtInPath` is overridable for unit tests; production code uses the
    /// default resource path.
    explicit RequestProfileStore(SettingsStore &settings,
                                 const QString &builtInPath =
                                     QString::fromUtf8(RequestProfile::kBuiltInPath),
                                 QObject *parent = nullptr);

    QAbstractListModel *draftModel() const { return m_model; }

    QString draftProfileId() const { return m_draftProfileId; }

    QString activeProfileId() const;

    /// The merged, persisted profile — the single source for request bodies.
    RequestProfile activeProfile() const;

    /// True when the user profiles file exists on disk.
    Q_INVOKABLE bool hasUserProfile() const;

    /// Discards unsaved draft edits: the table shows the persisted values of
    /// the active profile again.
    Q_INVOKABLE void reloadDraft();

    Q_INVOKABLE void selectDraftProfile(const QString &id);

    /// Applies a free-text edit to one draft row. Returns false when the text
    /// does not parse for that parameter's value kind (row left untouched).
    Q_INVOKABLE bool setDraftValue(int row, const QString &text);

    /// Commits the draft: writes the user copy of the edited profile, or
    /// removes that copy when the draft equals the built-in profile (a user
    /// copy exists only for changed settings). Emits profileChanged.
    Q_INVOKABLE void saveDraft();

    /// Loads the built-in rows of the edited profile into the draft
    /// (uncommitted; a following saveDraft() makes them persistent).
    Q_INVOKABLE void loadDefaultDraft();

    /// Restores defaults immediately (drops the user copy of the edited
    /// profile + draft reset) and emits profileChanged.
    Q_INVOKABLE void resetToDefaults();

signals:
    /// Emitted when a persisted profile changes: after a save or a reset.
    /// Consumers read activeProfile() again at their next use.
    void profileChanged();
    void draftProfileChanged();

private:
    QString userPath() const;
    void reloadUserProfiles();
    void persistUserProfiles();
    const RequestProfile *findBuiltIn(const QString &id) const;
    RequestProfile mergedProfile(const QString &id) const;
    void loadDraftRows();

    SettingsStore &m_settings;
    QList<RequestProfile> m_profiles;
    QHash<QString, RequestProfile> m_userProfiles;
    QString m_draftProfileId;
    RequestParametersModel *m_model;
};

}  // namespace llocr
