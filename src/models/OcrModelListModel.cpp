#include "models/OcrModelListModel.h"

#include "models/OcrModelFactory.h"

namespace llocr {

OcrModelListModel::OcrModelListModel(QObject *parent) : QAbstractListModel(parent), m_ids(OcrModelFactory::registeredIds())
{
    for (const QString &id : std::as_const(m_ids))
        m_names.append(OcrModelFactory::displayNameForId(id));
}

int OcrModelListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_ids.size());
}

QVariant OcrModelListModel::data(const QModelIndex &index, int role) const
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

QHash<int, QByteArray> OcrModelListModel::roleNames() const
{
    return {{IdRole, "modelId"}, {DisplayRole, "displayName"}};
}

int OcrModelListModel::rowOfId(const QString &id) const
{
    return static_cast<int>(m_ids.indexOf(id));
}

QString OcrModelListModel::displayName(const QString &id) const
{
    const int row = rowOfId(id);
    return row >= 0 ? m_names.at(row) : QString();
}

}  // namespace llocr
