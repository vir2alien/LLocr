#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

namespace llocr {

struct ReleaseAsset {
    QString fileName;     // e.g. "llama-b10594-bin-win-cuda-cu12-x64.zip"
    QString downloadUrl;  // browser_download_url
    QString os;           // "win" | "linux" | "macos"
    QString backend;      // "cpu" | "cuda" | "metal" | "vulkan" | "hip" | "sycl" | "" when unknown
    QString arch;         // "x64" | "arm64" | "" when unknown
    QString build;        // "b10594" — from the enclosing release; used for the install tag
    qint64 size = -1;     // -1 = size not reported
    QString sha256;       // lowercase hex digest from the release body; empty = not published
    bool cudart = false;  // true for the CUDA runtime zip (not the llama-server archive)

    QJsonObject toJson() const;
    static ReleaseAsset fromJson(const QJsonObject &o);
};

struct ReleaseInfo {
    QString tagName;       // e.g. "b10594"
    qint64 build = -1;     // numeric build extracted from tagName, -1 when unknown
    QString name;          // release title
    QString publishedAt;   // ISO-8601 string, for the picker label
    QList<ReleaseAsset> assets;
    QString body;          // raw body; sha256 entries are parsed from here

    ReleaseAsset pickAsset(QString os, QString arch, QString backend,
                           bool wantCudart = false) const;
};

}  // namespace llocr