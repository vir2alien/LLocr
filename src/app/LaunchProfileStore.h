#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "app/LaunchParametersModel.h"

class QAbstractListModel;

namespace llocr {

class SettingsStore;

/// Owns the llama-server launch-parameter profiles:
///  - built-in presets (read-only, :/profiles/serverLaunch.json, tagged with
///    os/backend) that ship with the app and may be updated by releases;
///  - per-preset user copies (<AppData>/LLocr/profiles/serverLaunch.json) —
///    full copies keyed by the preset id, written only when the user changed
///    that preset (add/remove/edit of parameters);
///  - the selected preset id, persisted in `launch/profileId` and
///    auto-switched to a preset matching the installed runtime backend.
///
/// Same draft discipline as RequestProfileStore: the settings table edits the
/// draft; saveDraft() commits the draft for the currently selected preset
/// (writing the user copy, or dropping it when the draft equals the built-in
/// preset), reloadDraft()/Cancel discards.
class LaunchProfileStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel *draftModel READ draftModel CONSTANT)
    Q_PROPERTY(QString draftProfileId READ draftProfileId NOTIFY draftProfileChanged)
    /// Resolved, persisted selection (auto-switched to the current backend).
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY activeProfileChanged)
    /// Parallel to presetIds(): display names for the settings combobox.
    Q_PROPERTY(QStringList presetIds READ presetIds CONSTANT)
    Q_PROPERTY(QStringList presetNames READ presetNames CONSTANT)

public:
    /// `builtInPath` is overridable for unit tests; production code uses the
    /// default resource path.
    explicit LaunchProfileStore(SettingsStore &settings,
                                const QString &builtInPath =
                                    QString::fromUtf8(LaunchProfile::kBuiltInPath),
                                QObject *parent = nullptr);

    QAbstractListModel *draftModel() const { return m_model; }
    QString draftProfileId() const { return m_draftProfileId; }
    QStringList presetIds() const;
    QStringList presetNames() const;

    /// Resolved selection: the persisted id when its preset matches the
    /// installed runtime backend, otherwise the best-matching preset.
    QString activeProfileId() const;

    /// The effective profile for argv building: the user copy of the active
    /// preset when one exists, else the built-in preset.
    LaunchProfile activeProfile() const;

    /// True when the user profile file exists on disk.
    Q_INVOKABLE bool hasUserProfile() const;

    /// Discards unsaved draft edits (settings dialog Cancel/reopen).
    Q_INVOKABLE void reloadDraft();

    /// Combobox: switches the draft to another preset (uncommitted until
    /// saveDraft()). Unknown ids are ignored.
    Q_INVOKABLE void selectDraftProfile(const QString &id);

    /// Applies a free-text edit to one draft row; false = rejected, row kept.
    Q_INVOKABLE bool setDraftValue(int row, const QString &text);

    /// Adds a parameter to the draft; false when the name is empty, reserved
    /// (model/mmproj/alias/host/port) or duplicated.
    Q_INVOKABLE bool appendDraftParameter(const QString &name, const QString &text);

    Q_INVOKABLE void removeDraftRow(int row);

    /// Commits the draft for the currently edited preset: persists the
    /// selection and stores/drops the user copy for that preset. Emits
    /// profileChanged.
    Q_INVOKABLE void saveDraft();

    /// Loads the built-in rows of the edited preset into the draft
    /// (uncommitted; a following saveDraft() drops the user copy).
    Q_INVOKABLE void loadDefaultDraft();

    /// Drops the user copy of the edited preset immediately, resets the
    /// persisted selection to auto-resolve, and reloads the draft. Emits
    /// profileChanged.
    Q_INVOKABLE void resetToDefaults();

    /// Service API (model installer): updates one Number row of the ACTIVE
    /// profile (e.g. ctx-size of the model just installed) and persists the
    /// user copy. A missing row is left alone — the profile, not the model,
    /// owns the parameter set. Emits profileChanged on a real change.
    void setActiveProfileNumber(const QString &name, double value);

signals:
    /// The effective launch parameters changed (save/reset/auto-switch) — the
    /// running server would need a restart to pick them up.
    void profileChanged();
    void draftProfileChanged();
    void activeProfileChanged();

private:
    QString userPath() const;
    void reloadUserProfiles();
    void persistUserProfiles();
    const LaunchProfile *findPreset(const QString &id) const;
    /// Auto-switch: re-resolves the persisted id against the installed
    /// backend; persists and announces the change when it differs.
    void ensureProfileResolved();
    bool presetMatches(const LaunchProfile &preset, const QString &backend,
                       const QString &osTag) const;

    SettingsStore &m_settings;
    QList<LaunchProfile> m_presets;
    QHash<QString, LaunchProfile> m_userProfiles;  // by preset id
    QString m_draftProfileId;
    LaunchParametersModel *m_model;
};

}  // namespace llocr
