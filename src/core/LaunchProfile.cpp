#include "core/LaunchProfile.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace llocr {

namespace {

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

bool LaunchProfile::operator==(const LaunchProfile &other) const
{
    return id == other.id && name == other.name && os == other.os
           && backend == other.backend && description == other.description
           && parametersEqual(other);
}

LaunchProfile LaunchProfile::merge(const LaunchProfile &defaults,
                                   const LaunchProfile &user)
{
    // The launch store's user copy *replaces* the parameter set — that is what
    // makes «remove this row» work — while the descriptive fields stay the
    // built-in's: they describe the preset, and an override file has no
    // business changing them. (The request store merges parameter-wise; the two
    // stores genuinely differ here, which is why the policy lives on the type.)
    LaunchProfile out = user;
    out.id = defaults.id.isEmpty() ? user.id : defaults.id;
    out.name = defaults.name.isEmpty() ? user.name : defaults.name;
    out.os = defaults.os;
    out.backend = defaults.backend;
    out.description = defaults.description;
    out.sortByOrder();
    return out;
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
        if (!profileFromJson(obj, profile, error))
            return QList<LaunchProfile>();
        out.append(profile);
    }
    return out;
}

bool LaunchProfile::profileFromJson(const QJsonObject &obj, LaunchProfile &profile,
                                    QString &error)
{
    profile = LaunchProfile();
    profile.id = obj.value(QLatin1String(kIdKey)).toString();
    profile.name = obj.value(QLatin1String(kNameKey)).toString();
    if (profile.name.isEmpty())
        profile.name = profile.id;
    profile.os = obj.value(QLatin1String(kOsKey)).toString();
    profile.backend = obj.value(QLatin1String(kBackendKey)).toString();
    profile.description = obj.value(QLatin1String(kDescriptionKey)).toString();

    int fallbackOrder = 1;
    QSet<QString> paramNames;
    for (const QJsonValue &pv : obj.value(QLatin1String("parameters")).toArray()) {
        if (!pv.isObject()) {
            error = QObject::tr("Launch profile parameter is not an object");
            return false;
        }
        LaunchParameter parameter;
        if (!parseParameter(pv.toObject(), fallbackOrder, parameter, error))
            return false;
        fallbackOrder = parameter.order + 1;
        if (paramNames.contains(parameter.name)) {
            error = QObject::tr("Launch profile %1 has a duplicate parameter: %2")
                        .arg(profile.id, parameter.name);
            return false;
        }
        paramNames.insert(parameter.name);
        profile.parameters.append(parameter);
    }
    profile.sortByOrder();
    return true;
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
