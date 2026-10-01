#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QSet>

#include "core/LaunchProfile.h"

namespace llocr {

class LaunchParametersModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        ValueTextRole,
        KindRole,
        DescriptionRole,
        EditableRole,
    };

    explicit LaunchParametersModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QList<LaunchParameter> &parameters() const { return m_parameters; }
    void setLockedPrefix(int rows);
    void setLockedNames(const QSet<QString> &names);
    void resetFrom(const QList<LaunchParameter> &parameters);
    bool setValue(int row, const QString &text);
    bool appendRow(const QString &name, const QString &text);
    void removeRow(int row);

private:
    bool isEditable(int row) const;

    QList<LaunchParameter> m_parameters;
    int m_lockedPrefix = 0;
    QSet<QString> m_lockedNames;
};

}  // namespace llocr
