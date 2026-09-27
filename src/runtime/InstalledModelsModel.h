#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include "runtime/ModelRegistry.h"

namespace llocr {

class SettingsStore;

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

    InstalledModelsModel(SettingsStore &settings, bool forCheck, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE int sourceIndex(int row) const;
    Q_INVOKABLE int rowForSourceIndex(int sourceIndex) const;

    void setEntries(const QList<ModelEntry> &entries);

    static bool matchesRole(const ModelEntry &entry, const SettingsStore &settings, bool forCheck);

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
