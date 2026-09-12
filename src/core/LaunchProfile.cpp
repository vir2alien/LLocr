#include "core/LaunchProfile.h"

#include <QHash>
#include <QJsonArray>
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
constexpr const char *kProfilesKey = "profiles";
constexpr const char *kIdKey = "id";
constexpr const char *kNameKey = "name";
constexpr const char *kOsKey = "os";
constexpr const char *kBackendKey = "backend";
constexpr const char *kDescriptionKey = "description";
constexpr const char *kOrderKey = "order";
constexpr const char *kValueKey = "value";

}  // namespace

const QStringList &LaunchProfile::reservedArgNames()
{
    static const QStringList names = {
        QStringLiteral("model"), QStringLiteral("mmproj"),
        QStringLiteral("alias"), QStringLiteral("host"),
        QStringLiteral("port"),
    };
    return names;
}

bool LaunchParameter::operator==(const LaunchParameter &other) const
{
    return name == other.name && kind == other.kind
        && value.typeId() == other.value.typeId() && value == other.value;
}

bool LaunchProfile::parametersEqual(const LaunchProfile &other) const
{
    LaunchProfile a = *this;
    LaunchProfile b = other;
    a.sortByOrder();
    b.sortByOrder();
    return a.parameters == b.parameters;
}

void LaunchProfile::sortByOrder()
{
    std::stable_sort(parameters.begin(), parameters.end(),
                     [](const LaunchParameter &a, const LaunchParameter &b) {
                         return a.order < b.order;
                     });
}

const LaunchParameter *LaunchProfile::find(const QString &name) const
{
    for (const LaunchParameter &p : parameters)
        if (p.name == name)
            return &p;
    return nullptr;
}

namespace {

/// Parses one `{ order, name, value?, description? }` parameter object.
/// Returns false (with `error` set) on a malformed entry.
bool parseParameter(const QJsonObject &obj, int fallbackOrder,
                    LaunchParameter &out, QString &error)
{
    const QString name = obj.value(QLatin1String("name")).toString();
    if (name.isEmpty()) {
        error = QObject::tr("Launch profile parameter has an empty name");
        return false;
    }
    out.name = name;

    if (obj.contains(QLatin1String(kOrderKey))) {
        out.order = obj.value(QLatin1String(kOrderKey)).toInt();
        if (out.order <= 0) {
            error = QObject::tr("Launch profile parameter %1 has an invalid order")
                        .arg(name);
            return false;
        }
    } else {
        out.order = fallbackOrder;
    }

    const QJsonValue value = obj.value(QLatin1String(kValueKey));
    switch (value.type()) {
    case QJsonValue::Undefined:
    case QJsonValue::Null:
        out.kind = LaunchValueKind::Flag;
        break;
    case QJsonValue::Double:
        out.kind = LaunchValueKind::Number;
        out.value = QVariant(value.toDouble());
        break;
    case QJsonValue::String:
        out.kind = LaunchValueKind::Text;
        out.value = QVariant(value.toString());
        break;
    default:
        // bool / array / object values make no sense on a command line.
        error = QObject::tr("Launch profile parameter %1 has an unsupported value")
                    .arg(name);
        return false;
    }

    out.description = obj.value(QLatin1String(kDescriptionKey)).toString();
    return true;
}

}  // namespace

QList<LaunchProfile> LaunchProfile::parseFile(const QJsonObject &root,
                                              QString &error)
{
    const QJsonArray profiles = root.value(QLatin1String(kProfilesKey)).toArray();
    // A missing/empty `profiles` array is a valid (empty) catalog.

    QList<LaunchProfile> out;
    QSet<QString> seen;
    for (const QJsonValue &entry : profiles) {
        if (!entry.isObject()) {
            error = QObject::tr("Launch profile is not an object");
            return QList<LaunchProfile>();
        }
        const QJsonObject obj = entry.toObject();
        const QString id = obj.value(QLatin1String(kIdKey)).toString();
        if (id.isEmpty()) {
            error = QObject::tr("Launch profile has an empty id");
            return QList<LaunchProfile>();
        }
        if (seen.contains(id)) {
            error = QObject::tr("Launch profile file has a duplicate profile: %1")
                        .arg(id);
            return QList<LaunchProfile>();
        }
        seen.insert(id);

        LaunchProfile profile;
        profile.id = id;
        profile.name = obj.value(QLatin1String(kNameKey)).toString();
        if (profile.name.isEmpty())
            profile.name = id;
        profile.os = obj.value(QLatin1String(kOsKey)).toString();
        profile.backend = obj.value(QLatin1String(kBackendKey)).toString();
        profile.description = obj.value(QLatin1String(kDescriptionKey)).toString();

        int fallbackOrder = 1;
        QSet<QString> paramNames;
        for (const QJsonValue &pv : obj.value(QLatin1String("parameters")).toArray()) {
            if (!pv.isObject()) {
                error = QObject::tr("Launch profile parameter is not an object");
                return QList<LaunchProfile>();
            }
            LaunchParameter parameter;
            if (!parseParameter(pv.toObject(), fallbackOrder, parameter, error))
                return QList<LaunchProfile>();
            fallbackOrder = parameter.order + 1;
            if (paramNames.contains(parameter.name)) {
                error = QObject::tr("Launch profile %1 has a duplicate parameter: %2")
                            .arg(id, parameter.name);
                return QList<LaunchProfile>();
            }
            paramNames.insert(parameter.name);
            profile.parameters.append(parameter);
        }
        profile.sortByOrder();
        out.append(profile);
    }
    return out;
}

QJsonObject LaunchProfile::toJson() const
{
    QJsonArray params;
    for (const LaunchParameter &p : parameters) {
        QJsonObject obj;
        obj.insert(QLatin1String(kOrderKey), p.order);
        obj.insert(QLatin1String(kNameKey), p.name);
        if (p.kind == LaunchValueKind::Number)
            obj.insert(QLatin1String(kValueKey), p.value.toDouble());
        else if (p.kind == LaunchValueKind::Text)
            obj.insert(QLatin1String(kValueKey), p.value.toString());
        // Flag: no value key.
        if (!p.description.isEmpty())
            obj.insert(QLatin1String(kDescriptionKey), p.description);
        params.append(obj);
    }

    QJsonObject obj;
    obj.insert(QLatin1String(kIdKey), id);
    if (!name.isEmpty() && name != id)
        obj.insert(QLatin1String(kNameKey), name);
    if (!os.isEmpty())
        obj.insert(QLatin1String(kOsKey), os);
    if (!backend.isEmpty())
        obj.insert(QLatin1String(kBackendKey), backend);
    if (!description.isEmpty())
        obj.insert(QLatin1String(kDescriptionKey), description);
    obj.insert(QLatin1String("parameters"), params);
    return obj;
}

}  // namespace llocr
