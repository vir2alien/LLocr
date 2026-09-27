#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include "runtime/InstallTransaction.h"

namespace llocr {

class SettingsStore;

// The installed llama.cpp builds as a real list model with named roles.
//
// The UI used to ask `installedBuildInfo(row)` for a QVariantMap and read it
// with string keys from QML — a typo in a key produced an empty cell rather
// than a compile error, and the delegate had to re-fetch its row by hand on
// every change signal (ADR 115).
class InstalledBuildsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        TagRole = Qt::UserRole + 1,
        BuildRole,
        BackendRole,
        BackendDisplayRole,
        ServerPathRole,
        BinaryFoundRole,
        ActiveRole,
    };
    Q_ENUM(Roles)

    explicit InstalledBuildsModel(SettingsStore &settings, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Replaces the whole list; emits a reset only when something changed.
    void setBuilds(const QList<InstalledBuildInfo> &builds);
    /// The active build is derived from the settings, so a `serverPath` change
    /// re-evaluates it instead of leaving a stale highlight.
    void settingsChanged();

    const QList<InstalledBuildInfo> &builds() const { return m_builds; }

signals:
    void countChanged();

private:
    bool isActive(const InstalledBuildInfo &build) const;
    static QString normalizedPath(const QString &path);

    SettingsStore &m_settings;
    QList<InstalledBuildInfo> m_builds;
};

}  // namespace llocr
