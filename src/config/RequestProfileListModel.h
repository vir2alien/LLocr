#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QStringList>

#include "core/ModelProfiles.h"

namespace llocr {

class RequestProfileListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        DisplayRole,
    };

    explicit RequestProfileListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void resetFrom(const QList<ModelProfiles::Profile> &profiles);

    Q_INVOKABLE int rowOfId(const QString &id) const;

private:
    QStringList m_ids;
    QStringList m_names;
};

}  // namespace llocr
