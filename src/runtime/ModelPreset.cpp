#include "runtime/ModelPreset.h"

#include "core/ModelProfiles.h"

namespace llocr {

QString ModelPreset::parserFor(bool forCheck) const
{
    const ModelProfiles::Role *role = ModelProfiles::roleFor(profileId, forCheck ? QStringLiteral("blockRecognition") : QStringLiteral("ocr"));
    return role ? role->parser : QString();
}

ModelPreset ModelPreset::fromJson(const QJsonObject &o)
{
    ModelPreset p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.title = o.value(QStringLiteral("title")).toString();
    p.profileId = o.value(QStringLiteral("profileId")).toString();
    p.repo = o.value(QStringLiteral("repo")).toString();
    p.revision = o.value(QStringLiteral("revision")).toString();
    p.model = o.value(QStringLiteral("model")).toString();
    p.mmproj = o.value(QStringLiteral("mmproj")).toString();
    p.mtp = o.value(QStringLiteral("mtp")).toString();
    p.mtpRepo = o.value(QStringLiteral("mtpRepo")).toString();
    p.mtpRevision = o.value(QStringLiteral("mtpRevision")).toString();
    p.ctxSize = o.value(QStringLiteral("ctxSize")).toInt(8192);
    p.minBuild = o.value(QStringLiteral("minBuild")).toString();
    p.license = o.value(QStringLiteral("license")).toString();

    const QJsonObject sha = o.value(QStringLiteral("sha256")).toObject();
    for (auto it = sha.constBegin(); it != sha.constEnd(); ++it)
        p.sha256.insert(it.key().toLower(), it.value().toString().toLower());
    return p;
}

QJsonObject ModelPreset::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    if (!title.isEmpty())
        o.insert(QStringLiteral("title"), title);
    if (!profileId.isEmpty())
        o.insert(QStringLiteral("profileId"), profileId);
    if (!repo.isEmpty())
        o.insert(QStringLiteral("repo"), repo);
    if (!revision.isEmpty())
        o.insert(QStringLiteral("revision"), revision);
    if (!model.isEmpty())
        o.insert(QStringLiteral("model"), model);
    if (!mmproj.isEmpty())
        o.insert(QStringLiteral("mmproj"), mmproj);
    if (!mtp.isEmpty())
        o.insert(QStringLiteral("mtp"), mtp);
    if (!mtpRepo.isEmpty())
        o.insert(QStringLiteral("mtpRepo"), mtpRepo);
    if (!mtpRevision.isEmpty())
        o.insert(QStringLiteral("mtpRevision"), mtpRevision);
    if (ctxSize != 8192)
        o.insert(QStringLiteral("ctxSize"), ctxSize);
    if (!minBuild.isEmpty())
        o.insert(QStringLiteral("minBuild"), minBuild);
    if (!license.isEmpty())
        o.insert(QStringLiteral("license"), license);
    QJsonObject sha;
    for (auto it = sha256.constBegin(); it != sha256.constEnd(); ++it)
        sha.insert(it.key(), it.value());
    if (!sha.isEmpty())
        o.insert(QStringLiteral("sha256"), sha);
    return o;
}

}  // namespace llocr