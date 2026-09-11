#pragma once

#include <QObject>
#include <QString>

#include "app/RequestParametersModel.h"

class QAbstractListModel;

namespace llocr {

class SettingsStore;

/// Owns the OCR request-body profile: built-in defaults (read-only resource)
/// merged with an optional user profile (<AppData>/LLocr/profiles/request.json,
/// written only when the user changed something).
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

public:
    /// `builtInPath` is overridable for unit tests; production code uses the
    /// default resource path.
    explicit RequestProfileStore(SettingsStore &settings,
                                 const QString &builtInPath =
                                     QString::fromUtf8(RequestProfile::kBuiltInPath),
                                 QObject *parent = nullptr);

    QAbstractListModel *draftModel() const { return m_model; }

    /// The merged, persisted profile — the single source for request bodies.
    const RequestProfile &activeProfile() const { return m_active; }

    /// True when the user profile file exists on disk.
    Q_INVOKABLE bool hasUserProfile() const;

    /// Discards unsaved draft edits: the table shows the persisted values again.
    Q_INVOKABLE void reloadDraft();

    /// Loads the built-in defaults into the draft (uncommitted; a following
    /// saveDraft() makes them persistent).
    Q_INVOKABLE void loadDefaultDraft();

    /// Applies a free-text edit to one draft row. Returns false when the text
    /// does not parse for that parameter's value kind (row left untouched).
    Q_INVOKABLE bool setDraftValue(int row, const QString &text);

    /// Commits the draft: writes the user profile, or removes the file when
    /// the draft equals the built-in defaults (a user profile exists only for
    /// changed settings). Emits profileChanged.
    Q_INVOKABLE void saveDraft();

    /// Restores defaults immediately (file removal + draft reset) and emits
    /// profileChanged.
    Q_INVOKABLE void resetToDefaults();

signals:
    /// Emitted when the persisted (active) profile changes: after a save or a
    /// reset. Consumers read activeProfile() again at their next use.
    void profileChanged();

private:
    QString userPath() const;
    void reloadActive();

    SettingsStore &m_settings;
    RequestProfile m_defaults;
    RequestProfile m_active;
    RequestParametersModel *m_model;
};

}  // namespace llocr
