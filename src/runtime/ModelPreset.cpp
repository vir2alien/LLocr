#include "runtime/ModelPreset.h"

namespace llocr {

ModelPreset ModelPreset::fromJson(const QJsonObject &o)
{
    ModelPreset p;
    p.id = o.value(QStringLiteral("id")).toString();
    p.title = o.value(QStringLiteral("title")).toString();
    p.repo = o.value(QStringLiteral("repo")).toString();
    p.revision = o.value(QStringLiteral("revision")).toString();
    p.model = o.value(QStringLiteral("model")).toString();
    p.mmproj = o.value(QStringLiteral("mmproj")).toString();
    p.parser = o.value(QStringLiteral("parser")).toString();
    p.prompt = o.value(QStringLiteral("prompt")).toString();
    p.ctxSize = o.value(QStringLiteral("ctxSize")).toInt(8192);
    p.minBuild = o.value(QStringLiteral("minBuild")).toString();
    p.approxVramGb = o.value(QStringLiteral("approxVramGb")).toDouble(0.0);
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
    if (!repo.isEmpty())
        o.insert(QStringLiteral("repo"), repo);
    if (!revision.isEmpty())
        o.insert(QStringLiteral("revision"), revision);
    if (!model.isEmpty())
        o.insert(QStringLiteral("model"), model);
    if (!mmproj.isEmpty())
        o.insert(QStringLiteral("mmproj"), mmproj);
    if (!parser.isEmpty())
        o.insert(QStringLiteral("parser"), parser);
    if (!prompt.isEmpty())
        o.insert(QStringLiteral("prompt"), prompt);
    if (ctxSize != 8192)
        o.insert(QStringLiteral("ctxSize"), ctxSize);
    if (!minBuild.isEmpty())
        o.insert(QStringLiteral("minBuild"), minBuild);
    if (approxVramGb > 0.0)
        o.insert(QStringLiteral("approxVramGb"), approxVramGb);
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