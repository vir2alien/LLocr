#include "config/RequestProfileListModel.h"

namespace llocr {

RequestProfileListModel::RequestProfileListModel(QObject *parent) : QAbstractListModel(parent) {}

int RequestProfileListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_ids.size());
}

QVariant RequestProfileListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_ids.size())
        return {};
    switch (role) {
    case IdRole:
        return m_ids.at(index.row());
    case DisplayRole:
        return m_names.at(index.row());
    default:
        return {};
    }
}

QHash<int, QByteArray> RequestProfileListModel::roleNames() const
{
    return {{IdRole, "modelId"}, {DisplayRole, "displayName"}};
}

void RequestProfileListModel::resetFrom(const QList<ModelProfiles::Profile> &profiles)
{
    beginResetModel();
    m_ids.clear();
    m_names.clear();
    for (const ModelProfiles::Profile &profile : profiles) {
        m_ids.append(profile.id);
        m_names.append(profile.title.isEmpty() ? profile.id : profile.title);
    }
    endResetModel();
}

int RequestProfileListModel::rowOfId(const QString &id) const
{
    return static_cast<int>(m_ids.indexOf(id));
}

}  // namespace llocr
