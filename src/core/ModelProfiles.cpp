#include "core/ModelProfiles.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QSet>

#include <algorithm>

#include "core/LaunchProfile.h"
#include "core/RequestProfile.h"

namespace llocr {

namespace {

ModelProfiles::Module readModule(const QJsonObject &object, QString &error)
{
    ModelProfiles::Module module;
    if (object.isEmpty())
        return module;
    module.id = object.value(QStringLiteral("id")).toString();
    module.file = object.value(QStringLiteral("file")).toString();
    module.sha256 = object.value(QStringLiteral("sha256")).toString().toLower();
    module.repo = object.value(QStringLiteral("repo")).toString();
    module.revision = object.value(QStringLiteral("revision")).toString();
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
        quant.sizeBytes = static_cast<qint64>(entry.value(QStringLiteral("sizeBytes")).toDouble(0));
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

namespace {

// The prompt half of a role: the system prompt, the per-block prompts, the
// decision questions and the alias map. A model profile keeps them inline; a
// request profile ships them in its own file (ADR 182).
void readPromptFields(const QJsonObject &object, ModelProfiles::Role &role, QString &error, const QString &id, bool requestProfile)
{
    role.systemPrompt = object.value(QStringLiteral("systemPrompt")).toString();
    role.statePrompt = object.value(QStringLiteral("statePrompt")).toString();
    role.question = object.value(QStringLiteral("question")).toString();
    role.questions = object.value(QStringLiteral("questions")).toObject();
    const QJsonObject criteria = object.value(QStringLiteral("criteria")).toObject();
    role.criteriaTrue = criteria.value(QStringLiteral("true")).toString();
    role.criteriaFalse = criteria.value(QStringLiteral("false")).toString();

    const QJsonObject blockPrompts = object.value(QStringLiteral("blockPrompts")).toObject();
    for (auto prompt = blockPrompts.constBegin(); prompt != blockPrompts.constEnd(); ++prompt) {
        // A v6 decision entry ships as an object (statePrompt/question/
        // questions); the string shape stays the fix/teleocr vocabulary.
        if (prompt.value().isObject()) {
            const QJsonObject spec = prompt.value().toObject();
            ModelProfiles::DecisionBlock block;
            block.statePrompt = spec.value(QStringLiteral("statePrompt")).toString();
            block.question = spec.value(QStringLiteral("question")).toString();
            block.questions = spec.value(QStringLiteral("questions")).toObject();
            const bool empty = prompt.key().isEmpty() || (block.statePrompt.isEmpty() && block.question.isEmpty() && block.questions.isEmpty());
            if (empty) {
                error = requestProfile ? QObject::tr("Request profile %1 has a block prompt without a block type or text").arg(id)
                                       : QObject::tr("Model profile %1 has a block prompt without a block type or text").arg(id);
                return;
            }
            role.decisionBlocks.insert(prompt.key(), block);
            continue;
        }
        const QString text = prompt.value().toString();
        if (prompt.key().isEmpty() || text.isEmpty()) {
            error = requestProfile ? QObject::tr("Request profile %1 has a block prompt without a block type or text").arg(id)
                                   : QObject::tr("Model profile %1 has a block prompt without a block type or text").arg(id);
            return;
        }
        role.blockPrompts.insert(prompt.key(), text);
    }

    const QJsonObject blockAliases = object.value(QStringLiteral("blockAliases")).toObject();
    for (auto alias = blockAliases.constBegin(); alias != blockAliases.constEnd(); ++alias) {
        const QString target = alias.value().toString();
        if (alias.key().isEmpty() || target.isEmpty()) {
            error = requestProfile ? QObject::tr("Request profile %1 has a block alias without a block type or target").arg(id)
                                   : QObject::tr("Model profile %1 has a block alias without a block type or target").arg(id);
            return;
        }
        role.blockAliases.insert(alias.key(), target);
    }
}

// The shared request texts of a task replace the role's own ones.
void applyPromptProfile(ModelProfiles::Role &role, const ModelProfiles::Role &prompts)
{
    role.systemPrompt = prompts.systemPrompt;
    role.statePrompt = prompts.statePrompt;
    role.question = prompts.question;
    role.questions = prompts.questions;
    role.criteriaTrue = prompts.criteriaTrue;
    role.criteriaFalse = prompts.criteriaFalse;
    role.blockPrompts = prompts.blockPrompts;
    role.decisionBlocks = prompts.decisionBlocks;
    role.blockAliases = prompts.blockAliases;
}

bool findPromptProfile(const QList<ModelProfiles::PromptProfile> &profiles, const QString &id)
{
    for (const ModelProfiles::PromptProfile &profile : profiles) {
        if (profile.id == id)
            return true;
    }
    return false;
}

// A prompt profile: the id, the title and the prompt half of a role.
ModelProfiles::PromptProfile readPromptProfile(const QJsonObject &root, QString &error)
{
    ModelProfiles::PromptProfile profile;
    profile.id = root.value(QStringLiteral("id")).toString();
    if (profile.id.isEmpty()) {
        error = QObject::tr("A request profile has an empty id");
        return profile;
    }
    profile.title = root.value(QStringLiteral("title")).toString();
    if (profile.title.isEmpty())
        profile.title = profile.id;
    readPromptFields(root, profile.prompts, error, profile.id, true);
    return profile;
}

}  // namespace

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
    profile.runtimeNote = root.value(QStringLiteral("runtimeNote")).toString();
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
        role.protocol = roleObject.value(QStringLiteral("protocol")).toString();
        role.maxOutput = roleObject.value(QStringLiteral("maxOutput")).toInt(0);
        const QJsonObject maxOutputs = roleObject.value(QStringLiteral("maxOutputByBlock")).toObject();
        for (auto maxOutput = maxOutputs.constBegin(); maxOutput != maxOutputs.constEnd(); ++maxOutput)
            role.blockMaxOutput.insert(maxOutput.key(), maxOutput.value().toInt(0));
        role.isDefault = roleObject.value(QStringLiteral("default")).toBool(false);
        role.promptBeforeImage = roleObject.value(QStringLiteral("promptBeforeImage")).toBool(false);
        role.imageLongestEdge = roleObject.value(QStringLiteral("imageLongestEdge")).toInt(0);
        role.matchThreshold = roleObject.value(QStringLiteral("matchThreshold")).toDouble(-1.0);
        const QJsonObject thresholds = roleObject.value(QStringLiteral("matchThresholdByBlock")).toObject();
        for (auto threshold = thresholds.constBegin(); threshold != thresholds.constEnd(); ++threshold)
            role.blockMatchThresholds.insert(threshold.key(), threshold.value().toDouble(-1.0));
        role.emptyThreshold = roleObject.value(QStringLiteral("emptyThreshold")).toDouble(-1.0);
        role.readableThreshold = roleObject.value(QStringLiteral("readableThreshold")).toDouble(-1.0);
        role.promptProfile = roleObject.value(QStringLiteral("promptProfile")).toString();
        role.launch = LaunchProfile::parseParameters(roleObject.value(QStringLiteral("launch")).toArray(), error, QObject::tr("Model profile"));
        if (!error.isEmpty())
            return profile;
        role.request = RequestProfile::parseParameters(roleObject.value(QStringLiteral("request")).toArray(), error, QObject::tr("Model profile request"));
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

        readPromptFields(roleObject, role, error, profile.id, false);
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
        Profile profile = readProfile(doc.object(), profileError);
        if (!profileError.isEmpty()) {
            error = QObject::tr("%1: %2").arg(path, profileError);
            return {};
        }
        // A role that names a prompt profile takes its texts from there: the
        // fix and decision tasks speak the same requests whatever model serves
        // them (ADR 182).
        for (auto role = profile.roles.begin(); role != profile.roles.end(); ++role) {
            if (role->promptProfile.isEmpty())
                continue;
            const PromptProfile *prompts = promptProfile(role->promptProfile);
            if (!prompts) {
                error = QObject::tr("Model profile %1 references an unknown request profile: %2").arg(profile.id, role->promptProfile);
                return {};
            }
            applyPromptProfile(*role, prompts->prompts);
        }
        if (find(out, profile.id)) {
            error = QObject::tr("Duplicate model profile id: %1").arg(profile.id);
            return {};
        }
        out.append(profile);
    }
    return out;
}

QList<ModelProfiles::PromptProfile> ModelProfiles::loadPromptProfilesFrom(const QString &directory, QString &error)
{
    QList<PromptProfile> out;
    QDir dir(directory);
    if (!dir.exists()) {
        error = QObject::tr("Request profile directory not found: %1").arg(directory);
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
        const PromptProfile profile = readPromptProfile(doc.object(), profileError);
        if (!profileError.isEmpty()) {
            error = QObject::tr("%1: %2").arg(path, profileError);
            return {};
        }
        if (findPromptProfile(out, profile.id)) {
            error = QObject::tr("Duplicate request profile id: %1").arg(profile.id);
            return {};
        }
        out.append(profile);
    }
    return out;
}

const QList<ModelProfiles::PromptProfile> &ModelProfiles::promptProfiles()
{
    static const QList<PromptProfile> loaded = [] {
        QList<PromptProfile> profiles;
        QString error;
        profiles = loadPromptProfilesFrom(QString::fromUtf8(kPromptResourceDir), error);
        if (!error.isEmpty())
            qWarning("ModelProfiles: %s", qUtf8Printable(error));
        return profiles;
    }();
    return loaded;
}

const ModelProfiles::PromptProfile *ModelProfiles::promptProfile(const QString &id)
{
    for (const PromptProfile &profile : promptProfiles()) {
        if (profile.id == id)
            return &profile;
    }
    return nullptr;
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
        const Role *modelRole = roleFor(profile, role);
        if (modelRole && modelRole->isDefault)
            return profile.id;
    }
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

QList<ModelProfiles::Profile> ModelProfiles::forRole(const QList<Profile> &profiles, const QString &role)
{
    QList<Profile> out;
    for (const Profile &p : profiles) {
        if (roleFor(p, role))
            out.append(p);
    }
    const auto recommended = [&role](const Profile &profile) {
        const Role *modelRole = roleFor(profile, role);
        return profile.isDefault || (modelRole && modelRole->isDefault);
    };
    std::stable_sort(out.begin(), out.end(), [&recommended](const Profile &a, const Profile &b) {
        if (recommended(a) != recommended(b))
            return recommended(a);
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    return out;
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

QString ModelProfiles::blockPromptFor(const QList<Profile> &profiles, const QString &modelId, const QString &role, const QString &type)
{
    const Profile *profile = find(profiles, modelId);
    const Role *found = profile ? roleFor(*profile, role) : nullptr;
    if (!found)
        return QString();
    const QString direct = found->blockPrompts.value(type);
    if (!direct.isEmpty())
        return direct;
    return found->blockPrompts.value(found->blockAliases.value(type));
}

ModelProfiles::DecisionBlock ModelProfiles::decisionBlockFor(const QList<Profile> &profiles, const QString &modelId, const QString &role, const QString &type)
{
    const Profile *profile = find(profiles, modelId);
    const Role *found = profile ? roleFor(*profile, role) : nullptr;
    if (!found)
        return {};
    const auto direct = found->decisionBlocks.constFind(type);
    if (direct != found->decisionBlocks.constEnd())
        return direct.value();
    const auto aliased = found->decisionBlocks.constFind(found->blockAliases.value(type));
    if (aliased != found->decisionBlocks.constEnd())
        return aliased.value();
    return {};
}

QString ModelProfiles::systemPromptFor(const QList<Profile> &profiles, const QString &modelId, const QString &role)
{
    const Profile *profile = find(profiles, modelId);
    const Role *found = profile ? roleFor(*profile, role) : nullptr;
    return found ? found->systemPrompt : QString();
}

QString ModelProfiles::statePromptFor(const QList<Profile> &profiles, const QString &modelId, const QString &role)
{
    const Profile *profile = find(profiles, modelId);
    const Role *found = profile ? roleFor(*profile, role) : nullptr;
    return found ? found->statePrompt : QString();
}

QString ModelProfiles::questionFor(const QList<Profile> &profiles, const QString &modelId, const QString &role)
{
    const Profile *profile = find(profiles, modelId);
    const Role *found = profile ? roleFor(*profile, role) : nullptr;
    return found ? found->question : QString();
}

QString ModelProfiles::criteriaTrueFor(const QList<Profile> &profiles, const QString &modelId, const QString &role)
{
    const Profile *profile = find(profiles, modelId);
    const Role *found = profile ? roleFor(*profile, role) : nullptr;
    return found ? found->criteriaTrue : QString();
}

QString ModelProfiles::criteriaFalseFor(const QList<Profile> &profiles, const QString &modelId, const QString &role)
{
    const Profile *profile = find(profiles, modelId);
    const Role *found = profile ? roleFor(*profile, role) : nullptr;
    return found ? found->criteriaFalse : QString();
}

QString ModelProfiles::runtimeNoteFor(const QList<Profile> &profiles, const QString &modelId)
{
    const Profile *profile = find(profiles, modelId);
    return profile ? profile->runtimeNote : QString();
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
    maxTokens.description = QObject::tr("Maximum tokens the model may generate for one page.");
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

int ModelProfiles::maxOutputForBlock(const QList<Profile> &profiles, const QString &modelId, const QString &role, const QString &blockType)
{
    const Profile *profile = find(profiles, modelId);
    const Role *found = profile ? roleFor(*profile, role) : nullptr;
    if (!found)
        return -1;
    const QString aliasKey = found->blockAliases.value(blockType, blockType);
    const int byBlock = found->blockMaxOutput.value(aliasKey, -1);
    return byBlock;
}

}  // namespace llocr
