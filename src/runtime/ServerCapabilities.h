#pragma once

#include <QJsonObject>
#include <QString>

namespace llocr {

struct ServerCapabilities {
    QString versionText;  // raw `--version` stdout, trimmed, if the probe ran it
    QString build;         // e.g. "b10594"; empty when undetermined
    bool ok = false;       // the binary answered --version/--help (responds at all)
    bool belowMinimum = false;  // build is known and < kMinimumSupportedBuild
    bool supportsFlashAttn = true;
    bool supportsFlashAttnValue = false;
    bool supportsAlias = true;
    bool supportsJinja = false;
    bool supportsCacheTypeK = false;
    bool supportsCacheTypeV = false;

    static constexpr const char *kMinimumSupportedBuild = "b4000";
    static constexpr int kMinimumBuildNumber = 4000;

    static ServerCapabilities detect(const QString &versionOutput,
                                     const QString &helpOutput = QString());

    static int extractBuildNumber(const QString &versionOutput);

    QJsonObject toJson() const;
    static ServerCapabilities fromJson(const QJsonObject &o);
    static QString cacheFileName(const QString &cacheDir, const QString &binaryPath);
};

}  // namespace llocr