#include "core/RequestProfile.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace llocr {

namespace {

constexpr int kSchemaVersion = 1;
constexpr const char *kSchemaKey = "schemaVersion";
constexpr const char *kParametersKey = "parameters";
constexpr const char *kOrderKey = "order";
constexpr const char *kNameKey = "name";
constexpr const char *kValueKey = "value";

}  // namespace

bool RequestParameter::operator==(const RequestParameter &other) const
{
    return name == other.name && kind == other.kind
        && value.typeId() == other.value.typeId() && value == other.value;
}

bool RequestProfile::valueFromJson(const QJsonValue &value,
                                   RequestValueKind &kind, QVariant &out)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        kind = RequestValueKind::Boolean;
        out = QVariant(value.toBool());
        return true;
    case QJsonValue::Double: {
        kind = RequestValueKind::Number;
        out = QVariant(value.toDouble());
        return true;
    }
    case QJsonValue::String:
        kind = RequestValueKind::String;
        out = QVariant(value.toString());
        return true;
    case QJsonValue::Array: {
        QStringList items;
        for (const QJsonValue &item : value.toArray()) {
            if (!item.isString())
                return false;  // mixed arrays are not supported
            items.append(item.toString());
        }
        kind = RequestValueKind::StringList;
        out = QVariant(items);
        return true;
    }
    default:
        return false;  // object / null / undefined
    }
}

QJsonValue RequestProfile::valueToJson(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return QJsonValue(value.toBool());
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return QJsonValue(value.toDouble());
    case QMetaType::QStringList: {
        QJsonArray arr;
        for (const QString &item : value.toStringList())
            arr.append(QJsonValue(item));
        return QJsonValue(arr);
    }
    case QMetaType::QString:
        return QJsonValue(value.toString());
    default:
        // Unknown types should not appear in a profile; serialize as a string
        // rather than dropping the parameter silently.
        return QJsonValue(value.toString());
    }
}

QString RequestProfile::valueToText(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::Double:
    case QMetaType::Float:
        return QString::number(value.toDouble());
    case QMetaType::QStringList:
        return value.toStringList().join(QStringLiteral(", "));
    case QMetaType::QString:
        return value.toString();
    default:
        return value.toString();
    }
}

bool RequestProfile::textToValue(const QString &text, RequestValueKind kind,
                                 QVariant &out)
{
    switch (kind) {
    case RequestValueKind::Number: {
        bool ok = false;
        const double number = text.trimmed().toDouble(&ok);
        if (!ok || !qIsFinite(number))
            return false;
        out = QVariant(number);
        return true;
    }
    case RequestValueKind::Boolean: {
        const QString normalized = text.trimmed().toLower();
        if (normalized == QStringLiteral("true")) {
            out = QVariant(true);
            return true;
        }
        if (normalized == QStringLiteral("false")) {
            out = QVariant(false);
            return true;
        }
        return false;
    }
    case RequestValueKind::String:
        out = QVariant(text);
        return true;
    case RequestValueKind::StringList: {
        QStringList items;
        const QStringList raw = text.split(QLatin1Char(','));
        for (QString item : raw) {
            item = item.trimmed();
            if (!item.isEmpty())
                items.append(item);
        }
        out = QVariant(items);
        return true;
    }
    }
    return false;
}

RequestProfile RequestProfile::fromJson(const QJsonObject &root, QString &error)
{
    if (!root.contains(QLatin1String(kParametersKey))) {
        error = QObject::tr("Request profile has no parameters array");
        return RequestProfile();
    }
    const QJsonArray params = root.value(QLatin1String(kParametersKey)).toArray();

    RequestProfile profile;
    QSet<QString> seen;
    int fallbackOrder = 1;
    for (const QJsonValue &entry : params) {
        if (!entry.isObject()) {
            error = QObject::tr("Request profile parameter is not an object");
            return RequestProfile();
        }
        const QJsonObject obj = entry.toObject();
        const QString name = obj.value(QLatin1String(kNameKey)).toString();
        if (name.isEmpty()) {
            error = QObject::tr("Request profile parameter has an empty name");
            return RequestProfile();
        }
        if (seen.contains(name)) {
            error = QObject::tr("Request profile has a duplicate parameter: %1")
                        .arg(name);
            return RequestProfile();
        }

        RequestParameter parameter;
        parameter.name = name;
        if (obj.contains(QLatin1String(kOrderKey))) {
            parameter.order = obj.value(QLatin1String(kOrderKey)).toInt();
            if (parameter.order <= 0) {
                error = QObject::tr("Request profile parameter %1 has an invalid order")
                            .arg(name);
                return RequestProfile();
            }
        } else {
            parameter.order = fallbackOrder;
        }
        fallbackOrder = parameter.order + 1;

        RequestValueKind kind;
        QVariant value;
        if (!valueFromJson(obj.value(QLatin1String(kValueKey)), kind, value)) {
            error = QObject::tr("Request profile parameter %1 has an unsupported value")
                        .arg(name);
            return RequestProfile();
        }
        parameter.kind = kind;
        parameter.value = value;

        seen.insert(name);
        profile.parameters.append(parameter);
    }
    profile.sortByOrder();
    return profile;
}

QJsonObject RequestProfile::toJson() const
{
    QJsonArray params;
    for (const RequestParameter &p : parameters) {
        QJsonObject obj;
        obj.insert(QLatin1String(kOrderKey), p.order);
        obj.insert(QLatin1String(kNameKey), p.name);
        obj.insert(QLatin1String(kValueKey), valueToJson(p.value));
        params.append(obj);
    }
    QJsonObject root;
    root.insert(QLatin1String(kSchemaKey), kSchemaVersion);
    root.insert(QLatin1String(kParametersKey), params);
    return root;
}

RequestProfile RequestProfile::merge(const RequestProfile &defaults,
                                     const RequestProfile &user)
{
    QHash<QString, RequestParameter> userByName;
    for (const RequestParameter &p : user.parameters)
        userByName.insert(p.name, p);

    RequestProfile out;
    for (const RequestParameter &d : defaults.parameters) {
        const auto it = userByName.constFind(d.name);
        if (it != userByName.constEnd()) {
            RequestParameter p = it.value();
            p.order = d.order;  // the built-in profile owns the position
            out.parameters.append(p);
            userByName.remove(d.name);
        } else {
            out.parameters.append(d);
        }
    }

    // User-only parameters (e.g. from an older built-in profile) are appended
    // after the built-in ones, sorted by their own order.
    QList<RequestParameter> extra = userByName.values();
    std::stable_sort(extra.begin(), extra.end(),
                     [](const RequestParameter &a, const RequestParameter &b) {
                         return a.order < b.order;
                     });
    for (const RequestParameter &p : extra)
        out.parameters.append(p);

    return out;
}

bool RequestProfile::operator==(const RequestProfile &other) const
{
    RequestProfile a = *this;
    RequestProfile b = other;
    a.sortByOrder();
    b.sortByOrder();
    return a.parameters == b.parameters;
}

void RequestProfile::sortByOrder()
{
    std::stable_sort(parameters.begin(), parameters.end(),
                     [](const RequestParameter &a, const RequestParameter &b) {
                         return a.order < b.order;
                     });
}

}  // namespace llocr
