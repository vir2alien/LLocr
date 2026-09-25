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
    case DescriptionRole:
        return p.description;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> RequestParametersModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        { NameRole, "name" },
        { ValueTextRole, "valueText" },
        { KindRole, "kind" },
        { OrderRole, "order" },
        { DescriptionRole, "description" },
    };
    return roles;
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

bool RequestParametersModel::appendRow(const QString &name, const QString &text)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty())
        return false;
    for (const RequestParameter &p : m_parameters) {
        if (p.name == trimmedName)
            return false;
    }

    RequestValueKind kind = RequestValueKind::String;
    QVariant parsed;
    if (RequestProfile::textToValue(text, RequestValueKind::Number, parsed))
        kind = RequestValueKind::Number;
    else if (RequestProfile::textToValue(text, RequestValueKind::Boolean, parsed))
        kind = RequestValueKind::Boolean;
    else if (!RequestProfile::textToValue(text, kind, parsed))
        return false;

    int maxOrder = 0;
    for (const RequestParameter &p : m_parameters)
        maxOrder = qMax(maxOrder, p.order);

    RequestParameter p;
    p.name = trimmedName;
    p.kind = kind;
    p.value = parsed;
    p.order = maxOrder + 1;

    beginInsertRows(QModelIndex(), m_parameters.size(), m_parameters.size());
    m_parameters.append(p);
    endInsertRows();
    return true;
}

}  // namespace llocr
