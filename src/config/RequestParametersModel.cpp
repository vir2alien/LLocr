#include "config/RequestParametersModel.h"

namespace llocr {

RequestParametersModel::RequestParametersModel(QObject *parent) : QAbstractListModel(parent) {}

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
    case ChoicesRole: {
        QVariantList choices;
        for (const RequestParameterChoice &choice : p.choices) {
            choices.append(QVariantMap{{QStringLiteral("label"), choice.label}, {QStringLiteral("value"), RequestProfile::valueToText(choice.value)}});
        }
        return choices;
    }
    case ChoiceIndexRole: {
        const QString value = RequestProfile::valueToText(p.value);
        for (int i = 0; i < p.choices.size(); ++i) {
            if (RequestProfile::valueToText(p.choices.at(i).value) == value)
                return i;
        }
        return -1;
    }
    }
    return QVariant();
}

QHash<int, QByteArray> RequestParametersModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        {NameRole, "name"},
        {ValueTextRole, "valueText"},
        {KindRole, "kind"},
        {OrderRole, "order"},
        {DescriptionRole, "description"},
        {ChoicesRole, "choices"},
        {ChoiceIndexRole, "choiceIndex"},
    };
    return roles;
}

void RequestParametersModel::resetFrom(const QList<RequestParameter> &parameters)
{
    RequestProfile profile;
    profile.parameters = parameters;
    profile.sortByOrder();
    const int previousCount = m_parameters.size();
    beginResetModel();
    m_parameters = profile.parameters;
    endResetModel();
    if (m_parameters.size() != previousCount)
        emit countChanged();
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
    emit dataChanged(idx, idx, {ValueTextRole});
    return true;
}

}  // namespace llocr
