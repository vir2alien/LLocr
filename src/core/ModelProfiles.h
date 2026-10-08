#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include "core/LaunchProfile.h"
#include "core/RequestProfile.h"

namespace llocr {

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
        QString systemPrompt;  // check-role override of the shared verifier system prompt
        QList<Prompt> prompts;
        QList<LaunchParameter> launch;
        QList<RequestParameter> request;
        QHash<QString, QString> blockStyles;
        QHash<QString, QString> blockPrompts;
        bool promptBeforeImage = false;  // wire order: prompt text first, then the image (LFM2.5 trains text-then-image)
    };

    struct Quant {
        QString id;
        QString file;
        QString sha256;
    };

    struct Module {
        QString id;
        QString file;
        QString sha256;
        QString repo;      // empty = files.repo; the DSpark/MTP draft lives in its own repo
        QString revision;  // empty = pin the repo head at install time
    };

    struct Files {
        QString repo;
        QString revision;  // may be empty; the install pins the repo head
        QList<Quant> quants;
        Module mmproj;
        Module mtp;  // optional speculative-decoding draft module (DSpark / MTP)
    };

    struct Profile {
        QString id;
        QString title;
        QString minBuild;
        QString license;
        QString runtimeNote;
        bool isDefault = false;
        Files files;
        QHash<QString, Role> roles;
    };

    static constexpr const char *kResourceDir = ":/profiles/models";

    static QList<Profile> loadFrom(const QString &directory, QString &error);
    static QList<Profile> loadBuiltIn(QString &error);

    static const QList<Profile> &instance();

    static const Profile *find(const QList<Profile> &profiles, const QString &id);
    static const Role *roleFor(const Profile &profile, const QString &role);
    static const Role *roleFor(const QString &modelId, const QString &role);

    static QString idForRepo(const QList<Profile> &profiles, const QString &repo);

    static QString defaultIdForRole(const QList<Profile> &profiles, const QString &role);

    static QStringList idsForRole(const QList<Profile> &profiles, const QString &role);

    static QList<Profile> forRole(const QList<Profile> &profiles, const QString &role);

    static QList<LaunchParameter> launchFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);
    static QList<RequestParameter> requestFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);
    static QList<Prompt> promptsFor(const QList<Profile> &profiles, const QString &modelId, const QString &role);

    static QString blockPromptFor(const QList<Profile> &profiles, const QString &modelId, const QString &role, const QString &type, const QString &fallback);

    static QString systemPromptFor(const QList<Profile> &profiles, const QString &modelId, const QString &role, const QString &fallback);

    static QString runtimeNoteFor(const QList<Profile> &profiles, const QString &modelId);

    static QList<RequestParameter> requestWithMaxOutput(const QList<Profile> &profiles, const QString &modelId, const QString &role);
};

}  // namespace llocr
