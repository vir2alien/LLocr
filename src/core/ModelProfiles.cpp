#include "core/ModelProfiles.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QSet>

namespace llocr {

namespace {

QList<LaunchParameter> readLaunchParameters(const QJsonArray &array, QString &error)
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

QList<RequestParameter> readRequestParameters(const QJsonArray &array, QString &error)
{
    QList<RequestParameter> out;
    QSet<QString> names;
    int fallbackOrder = 1;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            error = QObject::tr("Model profile request parameter is not an object");
            return {};
        }
        const QJsonObject obj = value.toObject();
        RequestParameter parameter;
        parameter.name = obj.value(QStringLiteral("name")).toString();
        if (parameter.name.isEmpty()) {
            error = QObject::tr("Model profile request parameter has an empty name");
            return {};
        }
        parameter.order = obj.contains(QStringLiteral("order")) ? obj.value(QStringLiteral("order")).toInt(fallbackOrder) : fallbackOrder;
        if (parameter.order <= 0) {
            error = QObject::tr("Model profile request parameter %1 has an invalid order").arg(parameter.name);
            return {};
        }
        fallbackOrder = parameter.order + 1;

        if (!RequestProfile::valueFromJson(obj.value(QStringLiteral("value")), parameter.kind, parameter.value)) {
            error = QObject::tr("Model profile request parameter %1 has an unsupported value").arg(parameter.name);
            return {};
        }
        parameter.description = obj.value(QStringLiteral("description")).toString();

        if (names.contains(parameter.name)) {
            error = QObject::tr("Model profile has a duplicate request parameter: %1").arg(parameter.name);
            return {};
        }
        names.insert(parameter.name);
        out.append(parameter);
    }
    return out;
}

ModelProfiles::Module readModule(const QJsonObject &object, QString &error)
{
    ModelProfiles::Module module;
    if (object.isEmpty())
        return module;
    module.id = object.value(QStringLiteral("id")).toString();
    module.file = object.value(QStringLiteral("file")).toString();
    module.sha256 = object.value(QStringLiteral("sha256")).toString().toLower();
    if (module.file.isEmpty()) {
        error = QObject::tr("Model profile %1 has a module without a file name").arg(module.id);
        return module;
    }
    return module;
}

ModelProfiles::Files readFiles(const QJsonObject &root, const QString &profileId, QString &error)
{
    ModelProfiles::Files files;
    const QJsonObject object = root.value(QStringLiteral("files")).toObject();
    if (object.isEmpty())
        return files;  // a model that is not downloaded from the catalog

    files.repo = object.value(QStringLiteral("repo")).toString();
    files.revision = object.value(QStringLiteral("revision")).toString();
    if (files.repo.isEmpty()) {
        error = QObject::tr("Model profile %1 has files without a repo").arg(profileId);
        return files;
    }

    for (const QJsonValue &value : object.value(QStringLiteral("quants")).toArray()) {
        const QJsonObject entry = value.toObject();
        ModelProfiles::Quant quant;
        quant.id = entry.value(QStringLiteral("id")).toString();
        quant.file = entry.value(QStringLiteral("file")).toString();
        quant.sha256 = entry.value(QStringLiteral("sha256")).toString().toLower();
        if (quant.id.isEmpty() || quant.file.isEmpty()) {
            error = QObject::tr("Model profile %1 has a quantization without an id or a file name").arg(profileId);
            return files;
        }
        files.quants.append(quant);
    }
    if (files.quants.isEmpty()) {
        error = QObject::tr("Model profile %1 has no quantizations").arg(profileId);
        return files;
    }

    files.mmproj = readModule(object.value(QStringLiteral("mmproj")).toObject(), error);
    if (!error.isEmpty())
        return files;
    files.mtp = readModule(object.value(QStringLiteral("mtp")).toObject(), error);
    return files;
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
    profile.license = root.value(QStringLiteral("license")).toString();
    profile.isDefault = root.value(QStringLiteral("default")).toBool(false);
    profile.files = readFiles(root, profile.id, error);
    if (!error.isEmpty())
        return profile;

    const QJsonObject roles = root.value(QStringLiteral("roles")).toObject();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        const QJsonObject roleObject = it.value().toObject();
        ModelProfiles::Role role;
        role.alias = roleObject.value(QStringLiteral("alias")).toString();
        role.parser = roleObject.value(QStringLiteral("parser")).toString();
        role.maxOutput = roleObject.value(QStringLiteral("maxOutput")).toInt(0);
        role.launch = readLaunchParameters(roleObject.value(QStringLiteral("launch")).toArray(), error);
        if (!error.isEmpty())
            return profile;
        role.request = readRequestParameters(roleObject.value(QStringLiteral("request")).toArray(), error);
        if (!error.isEmpty())
            return profile;

        for (const QJsonValue &value : roleObject.value(QStringLiteral("prompts")).toArray()) {
            const QJsonObject entry = value.toObject();
            ModelProfiles::Prompt prompt;
            prompt.id = entry.value(QStringLiteral("id")).toString();
            prompt.title = entry.value(QStringLiteral("title")).toString();
            if (prompt.title.isEmpty())
                prompt.title = prompt.id;
            prompt.text = entry.value(QStringLiteral("text")).toString();
            if (prompt.id.isEmpty() || prompt.text.isEmpty()) {
                error = QObject::tr("Model profile %1 has a prompt without an id or text").arg(profile.id);
                return profile;
            }
            role.prompts.append(prompt);
        }

        const QJsonObject blocks = roleObject.value(QStringLiteral("blocks")).toObject();
        const QJsonObject styles = blocks.contains(QStringLiteral("styles")) ? blocks.value(QStringLiteral("styles")).toObject() : blocks;
        for (auto style = styles.constBegin(); style != styles.constEnd(); ++style) {
            role.blockStyles.insert(style.key(), style.value().toVariant().toString());
        }

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

    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &name : files) {
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

const QList<ModelProfiles::Profile> &ModelProfiles::instance()
{
    static const QList<Profile> loaded = [] {
        QList<Profile> profiles;
        QString error;
        profiles = loadBuiltIn(error);
        if (!error.isEmpty())
            qWarning("ModelProfiles: %s", qUtf8Printable(error));
        return profiles;
    }();
    return loaded;
}

void ModelProfiles::setInstance(const QList<Profile> &profiles)
{
    const_cast<QList<Profile> &>(instance()) = profiles;
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

const ModelProfiles::Role *ModelProfiles::roleFor(const QString &modelId, const QString &role)
{
    const Profile *profile = find(instance(), modelId);
    return profile ? roleFor(*profile, role) : nullptr;
}

QString ModelProfiles::idForRepo(const QList<Profile> &profiles, const QString &repo)
{
    if (repo.isEmpty())
        return QString();
    for (const Profile &profile : profiles) {
        if (profile.files.repo == repo)
            return profile.id;
    }
    return QString();
}

QString ModelProfiles::defaultIdForRole(const QList<Profile> &profiles, const QString &role)
{
    for (const Profile &profile : profiles) {
        if (profile.isDefault && roleFor(profile, role))
            return profile.id;
    }
    const QStringList ids = idsForRole(profiles, role);
    return ids.isEmpty() ? QString() : ids.constFirst();
}

QStringList ModelProfiles::idsForRole(const QList<Profile> &profiles, const QString &role)
{
    QStringList ids;
    for (const Profile &p : profiles) {
        if (roleFor(p, role))
            ids.append(p.id);
    }
    return ids;
}

QList<LaunchParameter> ModelProfiles::launchFor(const QList<Profile> &profiles, const QString &modelId, const QString &role)
{
    const Profile *profile = find(profiles, modelId);
    if (!profile)
        return {};
    if (const Role *found = roleFor(*profile, role))
        return found->launch;
    return {};
}

QList<RequestParameter> ModelProfiles::requestFor(const QList<Profile> &profiles, const QString &modelId, const QString &role)
{
    const Profile *profile = find(profiles, modelId);
    if (!profile)
        return {};
    if (const Role *found = roleFor(*profile, role))
        return found->request;
    return {};
}

QList<ModelProfiles::Prompt> ModelProfiles::promptsFor(const QList<Profile> &profiles, const QString &modelId, const QString &role)
{
    if (const Role *found = roleFor(modelId, role))
        return found->prompts;
    return {};
}

QList<RequestParameter> ModelProfiles::requestWithMaxOutput(const QList<Profile> &profiles, const QString &modelId, const QString &role)
{
    QList<RequestParameter> out = requestFor(profiles, modelId, role);
    const Profile *profile = find(profiles, modelId);
    const Role *found = profile ? roleFor(*profile, role) : nullptr;
    const int maxOutput = found ? found->maxOutput : 0;
    if (maxOutput <= 0)
        return out;

    RequestParameter maxTokens;
    maxTokens.name = QStringLiteral("max_tokens");
    maxTokens.kind = RequestValueKind::Number;
    maxTokens.value = QVariant(maxOutput);
    maxTokens.description = QObject::tr("Maximum tokens the model may generate for one page. Shares its value with the server's n-predict.");
    for (RequestParameter &parameter : out) {
        if (parameter.name == maxTokens.name) {
            parameter = maxTokens;
            return out;
        }
    }
    maxTokens.order = out.isEmpty() ? 1 : out.last().order + 1;
    out.append(maxTokens);
    return out;
}

}  // namespace llocr
