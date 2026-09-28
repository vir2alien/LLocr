#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include "core/LaunchProfile.h"
#include "core/RequestProfile.h"

namespace llocr {

// One profile per model family, loaded from ":/profiles/models/*.json". A
// profile owns everything that is a property of the weights: the prompts, the
// request and launch parameters, the label vocabulary. A model that does not
// name a parameter simply does not pass it, which is why the platform layer in
// serverLaunch.json must not name one either (docs/10-model-profiles.md).
class ModelProfiles
{
public:
    struct Prompt {
        QString id;
        QString title;
        QString text;
    };

    struct Role {
        QString alias;
        QString parser;
        int maxOutput = 0;
        QList<Prompt> prompts;
        QList<LaunchParameter> launch;
        QList<RequestParameter> request;
        QHash<QString, QString> blockStyles;
    };

    struct Profile {
        QString id;
        QString title;
        QString minBuild;
        bool isDefault = false;
        QHash<QString, Role> roles;
        QList<LaunchParameter> fallbackLaunch;
        QList<RequestParameter> fallbackRequest;
    };

    static constexpr const char *kResourceDir = ":/profiles/models";

    static QList<Profile> loadFrom(const QString &directory, QString &error);
    static QList<Profile> loadBuiltIn(QString &error);

    // The catalog the whole process reads. Loaded once from the resource, like
    // BlockStyleMap; the models, the parsers and the label map all need it and
    // none of them can reach a store. Tests replace it with setInstance().
    static const QList<Profile> &instance();
    static void setInstance(const QList<Profile> &profiles);

    static const Profile *find(const QList<Profile> &profiles, const QString &id);
    static const Role *roleFor(const Profile &profile, const QString &role);
    static const Role *roleFor(const QString &modelId, const QString &role);

    // The role a model profile answers, or nullptr. A model that only serves one
    // role is not offered for the other one.
    static QStringList idsForRole(const QList<Profile> &profiles, const QString &role);

    static QList<LaunchParameter> launchFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);
    static QList<RequestParameter> requestFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);
    static QList<Prompt> promptsFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);

    // max_tokens is not written in the profile: it is the request-side half of
    // maxOutput, which the launch side already passes as n-predict. Two numbers
    // for one limit could disagree, and max_tokens > n-predict truncates the
    // reply silently — the parser sees a short page, not an error.
    static QList<RequestParameter> requestWithMaxOutput(const QList<Profile> &profiles, const QString &modelId, const QString &role);
};

}  // namespace llocr
