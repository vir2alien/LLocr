#include "app/LaunchParametersModel.h"

#include <cmath>

namespace llocr {

namespace {

bool isNumericText(const QString &text)
{
    bool ok = false;
    const double number = text.trimmed().toDouble(&ok);
    return ok && qIsFinite(number);
}

}  // namespace

LaunchParametersModel::LaunchParametersModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LaunchParametersModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_parameters.size();
}

QVariant LaunchParametersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_parameters.size())
        return QVariant();

    const LaunchParameter &p = m_parameters.at(index.row());
    switch (role) {
    case NameRole:
        return p.name;
    case ValueTextRole:
        if (p.kind == LaunchValueKind::Number)
            return QString::number(p.value.toDouble());
        if (p.kind == LaunchValueKind::Text)
            return p.value.toString();
        return QString();  // Flag
    case KindRole:
        return int(p.kind);
    case DescriptionRole:
        return p.description;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> LaunchParametersModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { ValueTextRole, "valueText" },
        { KindRole, "kind" },
        { DescriptionRole, "description" },
    };
}

void LaunchParametersModel::resetFrom(const QList<LaunchParameter> &parameters)
{
    LaunchProfile profile;
    profile.parameters = parameters;
    profile.sortByOrder();
    beginResetModel();
    m_parameters = profile.parameters;
    endResetModel();
}

bool LaunchParametersModel::setValue(int row, const QString &text)
{
    if (row < 0 || row >= m_parameters.size())
        return false;

    LaunchParameter &p = m_parameters[row];
    if (p.kind == LaunchValueKind::Number && !text.trimmed().isEmpty()
        && !isNumericText(text))
        return false;  // a Number row only accepts numeric text

    LaunchValueKind nextKind = p.kind;
    QVariant nextValue;
    if (text.trimmed().isEmpty()) {
        nextKind = LaunchValueKind::Flag;  // emptied value → bare flag
    } else if (p.kind == LaunchValueKind::Flag) {
        // A filled flag row becomes a value row.
        nextKind = isNumericText(text) ? LaunchValueKind::Number
                                       : LaunchValueKind::Text;
        nextValue = nextKind == LaunchValueKind::Number
                        ? QVariant(text.trimmed().toDouble())
                        : QVariant(text);
    } else {
        nextValue = p.kind == LaunchValueKind::Number
                        ? QVariant(text.trimmed().toDouble())
                        : QVariant(text);
    }

    const bool valueChanged = p.kind != nextKind
        || (nextKind != LaunchValueKind::Flag && p.value != nextValue);
    if (!valueChanged)
        return true;

    p.kind = nextKind;
    p.value = nextValue;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { ValueTextRole, KindRole });
    return true;
}

bool LaunchParametersModel::appendRow(const QString &name, const QString &text)
{
    QString clean = name.trimmed();
    while (clean.startsWith(u'-'))
        clean.remove(0, 1);
    if (clean.isEmpty())
        return false;
    if (LaunchProfile::reservedArgNames().contains(clean))
        return false;
    for (const LaunchParameter &p : m_parameters)
        if (p.name == clean)
            return false;

    LaunchParameter parameter;
    parameter.name = clean;
    parameter.order = m_parameters.isEmpty() ? 1 : m_parameters.last().order + 1;
    if (text.trimmed().isEmpty()) {
        parameter.kind = LaunchValueKind::Flag;
    } else if (isNumericText(text)) {
        parameter.kind = LaunchValueKind::Number;
        parameter.value = QVariant(text.trimmed().toDouble());
    } else {
        parameter.kind = LaunchValueKind::Text;
        parameter.value = QVariant(text);
    }

    beginInsertRows(QModelIndex(), m_parameters.size(), m_parameters.size());
    m_parameters.append(parameter);
    endInsertRows();
    return true;
}

void LaunchParametersModel::removeRow(int row)
{
    if (row < 0 || row >= m_parameters.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_parameters.removeAt(row);
    endRemoveRows();
}

}  // namespace llocr
