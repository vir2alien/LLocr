#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

namespace llocr {

struct ModelPreset {
    QString id;
    QString title;
    QString profileId;  // the model profile this entry installs; owns the role
    QString repo;
    QString revision;     // pinned commit SHA, when known; may be empty
    QString model;        // main .gguf file name (or first part of a multi-file)
    QString mmproj;       // optional vision-projector file name
    QString mtp;          // optional speculative-decoding draft file name
    QString mtpRepo;      // the draft's HF repo; empty = repo
    QString mtpRevision;  // pinned commit SHA of the draft repo; may be empty
    int ctxSize = 8192;
    QString minBuild;  // minimum llama.cpp build tag, e.g. "b4000"
    QString license;   // URL or short license name
    QHash<QString, QString> sha256;

    QString parserFor(const QString &role) const;

    static ModelPreset fromJson(const QJsonObject &o);
    QJsonObject toJson() const;
};

}  // namespace llocr