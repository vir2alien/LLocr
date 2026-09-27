#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include "runtime/ModelRegistry.h"

namespace llocr {

class SettingsStore;

// The installed models of one role (recognition or check) as a real list model
// with named roles — the same reason as InstalledBuildsModel: the QML used to
// index a QVariantMap with string keys and re-fetch the row on every change
// signal (ADR 115).
class InstalledModelsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        PathRole,
        MmprojPathRole,
        SizeRole,
        QuantizationRole,
        OriginRole,
        LicenseRole,
        RepoRole,
        ActiveRole,
        PartsRole,
    };
    Q_ENUM(Roles)

    InstalledModelsModel(SettingsStore &settings, bool forCheck,
                         QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// The index into the installer's own list, for the mutating calls
    /// (activate / remove / open folder) that the UI drives by row.
    Q_INVOKABLE int sourceIndex(int row) const;
    Q_INVOKABLE int rowForSourceIndex(int sourceIndex) const;

    /// The active model is derived from the settings, so re-publishing the
    /// entries (what the owner does on a launch-path change) re-evaluates both
    /// the highlight and the role split, which depends on it.
    void setEntries(const QList<ModelEntry> &entries);

    /// Whether a model belongs to a role. Shared with the installer so the list
    /// and the installer's own filtering can never disagree.
    static bool matchesRole(const ModelEntry &entry, const SettingsStore &settings,
                            bool forCheck);

signals:
    void countChanged();

private:
    bool matches(const ModelEntry &entry) const;
    bool isActive(const ModelEntry &entry) const;
    static QString displayTitle(const ModelEntry &entry);

    SettingsStore &m_settings;
    bool m_forCheck = false;
    QList<ModelEntry> m_entries;
    QList<int> m_rows;  ///< indices into m_entries, already filtered by role
};

}  // namespace llocr
