#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>

namespace llocr {

// A precise, compatible model bundle (ADR 43): the exact pairing of main model,
// optional vision projector, parser id, prompt and context that LLocr's
// `det_tokens` parser expects. Selecting a preset applies these fields together
// so an arbitrary vision-GGUF never produces unparsable output.
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
    // Digest per file name (lowercased) for offline verify when the HF `lfs.oid`
    // is unavailable or the user has pinned a fixed revision in the preset.
    QHash<QString, QString> sha256;

    /// Serialization (round-trips toJson → fromJson toJson).
    static ModelPreset fromJson(const QJsonObject &o);
    QJsonObject toJson() const;
};

}  // namespace llocr