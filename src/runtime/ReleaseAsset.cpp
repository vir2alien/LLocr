#include <QList>

#include "runtime/ReleaseAsset.h"

namespace llocr {

QJsonObject ReleaseAsset::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("fileName"), fileName);
    o.insert(QStringLiteral("downloadUrl"), downloadUrl);
    o.insert(QStringLiteral("os"), os);
    o.insert(QStringLiteral("backend"), backend);
    o.insert(QStringLiteral("arch"), arch);
    o.insert(QStringLiteral("build"), build);
    o.insert(QStringLiteral("size"), static_cast<double>(size));
    o.insert(QStringLiteral("sha256"), sha256);
    o.insert(QStringLiteral("cudart"), cudart);
    return o;
}

ReleaseAsset ReleaseAsset::fromJson(const QJsonObject &o)
{
    ReleaseAsset a;
    a.fileName = o.value(QStringLiteral("fileName")).toString();
    a.downloadUrl = o.value(QStringLiteral("downloadUrl")).toString();
    a.os = o.value(QStringLiteral("os")).toString();
    a.backend = o.value(QStringLiteral("backend")).toString();
    a.arch = o.value(QStringLiteral("arch")).toString();
    a.build = o.value(QStringLiteral("build")).toString();
    a.size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble(-1));
    a.sha256 = o.value(QStringLiteral("sha256")).toString();
    a.cudart = o.value(QStringLiteral("cudart")).toBool(false);
    return a;
}

ReleaseAsset ReleaseInfo::pickAsset(QString os, QString arch, QString backend,
                                    bool wantCudart) const
{
    for (const ReleaseAsset &a : assets) {
        if (a.cudart != wantCudart)
            continue;
        if (wantCudart)
            return a;  // only one cudart asset is expected
        // An asset with an empty backend token (e.g. `...-bin-macos-arm64` or
        // `...-bin-ubuntu-x64`) is a universal build that serves any backend.
        if (a.os == os && a.arch == arch
            && (a.backend.isEmpty() || a.backend == backend))
            return a;
    }
    return ReleaseAsset();
}

}  // namespace llocr