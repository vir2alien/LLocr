#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

namespace llocr {

struct ModelPreset {
    QString id;
    QString title;
    QString repo;
    QString revision;    // pinned commit SHA, when known; may be empty
    QString model;       // main .gguf file name (or first part of a multi-file)
    QString mmproj;      // optional vision-projector file name
    QString parser;      // e.g. "det_tokens"
    QString prompt;      // e.g. "document parsing."
    int ctxSize = 8192;
    QString minBuild;    // minimum llama.cpp build tag, e.g. "b4000"
    double approxVramGb = 0.0;
    QString license;     // URL or short license name
    QHash<QString, QString> sha256;

    static ModelPreset fromJson(const QJsonObject &o);
    QJsonObject toJson() const;
};

}  // namespace llocr