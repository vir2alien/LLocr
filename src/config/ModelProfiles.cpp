#include "config/ModelProfiles.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>

namespace llocr {

namespace {

QList<LaunchParameter> readParameters(const QJsonArray &array, QString &error)
{
    QList<LaunchParameter> out;
    QSet<QString> names;
    int fallbackOrder = 1;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            error = QObject::tr("Model profile parameter is not an object");
            return {};
        }
        const QJsonObject obj = value.toObject();
        LaunchParameter parameter;
        parameter.name = obj.value(QStringLiteral("name")).toString();
        if (parameter.name.isEmpty()) {
            error = QObject::tr("Model profile parameter has an empty name");
            return {};
        }
        parameter.order = obj.contains(QStringLiteral("order")) ? obj.value(QStringLiteral("order")).toInt(fallbackOrder) : fallbackOrder;
        if (parameter.order <= 0) {
            error = QObject::tr("Model profile parameter %1 has an invalid order").arg(parameter.name);
            return {};
        }
        fallbackOrder = parameter.order + 1;

        const QJsonValue v = obj.value(QStringLiteral("value"));
        switch (v.type()) {
        case QJsonValue::Undefined:
        case QJsonValue::Null:
            parameter.kind = LaunchValueKind::Flag;
            break;
        case QJsonValue::Double:
            parameter.kind = LaunchValueKind::Number;
            parameter.value = QVariant(v.toDouble());
            break;
        case QJsonValue::String:
            parameter.kind = LaunchValueKind::Text;
            parameter.value = QVariant(v.toString());
            break;
        default:
            error = QObject::tr("Model profile parameter %1 has an unsupported value").arg(parameter.name);
            return {};
        }
        parameter.description = obj.value(QStringLiteral("description")).toString();

        if (names.contains(parameter.name)) {
            error = QObject::tr("Model profile has a duplicate parameter: %1").arg(parameter.name);
            return {};
        }
        names.insert(parameter.name);
        out.append(parameter);
    }
    return out;
}

ModelProfiles::Profile readProfile(const QJsonObject &root, QString &error)
{
    ModelProfiles::Profile profile;
    profile.id = root.value(QStringLiteral("id")).toString();
    if (profile.id.isEmpty()) {
        error = QObject::tr("A model profile has an empty id");
        return profile;
    }
    profile.title = root.value(QStringLiteral("title")).toString();
    if (profile.title.isEmpty())
        profile.title = profile.id;
    profile.minBuild = root.value(QStringLiteral("minBuild")).toString();

    const QJsonObject fallback = root.value(QStringLiteral("fallback")).toObject();
    profile.fallback = readParameters(fallback.value(QStringLiteral("launch")).toArray(), error);
    if (!error.isEmpty())
        return profile;

    const QJsonObject roles = root.value(QStringLiteral("roles")).toObject();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        const QJsonObject roleObject = it.value().toObject();
        ModelProfiles::Role role;
        role.alias = roleObject.value(QStringLiteral("alias")).toString();
        role.parser = roleObject.value(QStringLiteral("parser")).toString();
        role.maxOutput = roleObject.value(QStringLiteral("maxOutput")).toInt(0);
        role.launch = readParameters(roleObject.value(QStringLiteral("launch")).toArray(), error);
        if (!error.isEmpty())
            return profile;
        profile.roles.insert(it.key(), role);
    }
    return profile;
}

}  // namespace

QList<ModelProfiles::Profile> ModelProfiles::loadFrom(const QString &directory, QString &error)
{
    QList<Profile> out;
    QDir dir(directory);
    if (!dir.exists()) {
        error = QObject::tr("Model profile directory not found: %1").arg(directory);
        return out;
    }

    QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &name : std::as_const(files)) {
        const QString path = dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            error = QObject::tr("Unable to read %1: %2").arg(path, file.errorString());
            return {};
        }
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            error = QObject::tr("%1 is malformed: %2").arg(path, parseError.errorString());
            return {};
        }
        QString profileError;
        const Profile profile = readProfile(doc.object(), profileError);
        if (!profileError.isEmpty()) {
            error = QObject::tr("%1: %2").arg(path, profileError);
            return {};
        }
        if (find(out, profile.id)) {
            error = QObject::tr("Duplicate model profile id: %1").arg(profile.id);
            return {};
        }
        out.append(profile);
    }
    return out;
}

QList<ModelProfiles::Profile> ModelProfiles::loadBuiltIn(QString &error)
{
    return loadFrom(QString::fromUtf8(kResourceDir), error);
}

const ModelProfiles::Profile *ModelProfiles::find(const QList<Profile> &profiles, const QString &id)
{
    for (const Profile &p : profiles) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

const ModelProfiles::Role *ModelProfiles::roleFor(const Profile &profile, const QString &role)
{
    const auto it = profile.roles.constFind(role);
    return it == profile.roles.constEnd() ? nullptr : &it.value();
}

QList<LaunchParameter> ModelProfiles::launchFor(const QList<Profile> &profiles, const QString &modelId, const QString &role)
{
    const Profile *profile = find(profiles, modelId);
    if (!profile)
        return {};
    if (const Role *found = roleFor(*profile, role))
        return found->launch;
    return profile->fallback;
}

}  // namespace llocr
