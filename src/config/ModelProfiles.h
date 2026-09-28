#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include "core/LaunchProfile.h"

namespace llocr {

// One profile per model family, loaded from ":/profiles/models/*.json". A
// profile owns the parameters that are a property of the weights: a model that
// does not name a parameter simply does not pass it, which is why the launch
// layer in serverLaunch.json must not name one either (docs/10-model-profiles.md).
class ModelProfiles
{
public:
    struct Role {
        QString alias;
        QString parser;
        int maxOutput = 0;
        QList<LaunchParameter> launch;
    };

    struct Profile {
        QString id;
        QString title;
        QString minBuild;
        QHash<QString, Role> roles;
        QList<LaunchParameter> fallback;
    };

    static constexpr const char *kResourceDir = ":/profiles/models";

    static QList<Profile> loadFrom(const QString &directory, QString &error);
    static QList<Profile> loadBuiltIn(QString &error);

    static const Profile *find(const QList<Profile> &profiles, const QString &id);
    static const Role *roleFor(const Profile &profile, const QString &role);

    // The parameters for a model, falling back to the profile's own fallback
    // block when the model is not in the catalog (a hand-picked GGUF, an
    // external server): a missing ctx-size would otherwise fall through to
    // llama.cpp's own 4096 default, which truncates a table page.
    static QList<LaunchParameter> launchFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);
};

}  // namespace llocr
