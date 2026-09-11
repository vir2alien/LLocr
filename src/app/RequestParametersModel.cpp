#include "app/RequestParametersModel.h"

namespace llocr {

RequestParametersModel::RequestParametersModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int RequestParametersModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_parameters.size();
}

QVariant RequestParametersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_parameters.size())
        return QVariant();

    const RequestParameter &p = m_parameters.at(index.row());
    switch (role) {
    case NameRole:
        return p.name;
    case ValueTextRole:
        return RequestProfile::valueToText(p.value);
    case KindRole:
        return int(p.kind);
    case OrderRole:
        return p.order;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> RequestParametersModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { ValueTextRole, "valueText" },
        { KindRole, "kind" },
        { OrderRole, "order" },
    };
}

void RequestParametersModel::resetFrom(const QList<RequestParameter> &parameters)
{
    RequestProfile profile;
    profile.parameters = parameters;
    profile.sortByOrder();
    beginResetModel();
    m_parameters = profile.parameters;
    endResetModel();
}

bool RequestParametersModel::setValue(int row, const QString &text)
{
    if (row < 0 || row >= m_parameters.size())
        return false;

    RequestParameter &p = m_parameters[row];
    QVariant parsed;
    if (!RequestProfile::textToValue(text, p.kind, parsed))
        return false;
    if (parsed == p.value)
        return true;

    p.value = parsed;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { ValueTextRole });
    return true;
}

}  // namespace llocr
