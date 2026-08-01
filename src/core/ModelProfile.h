#pragma once

#include <QJsonObject>
#include <QString>

namespace llocr {

/**
 * @brief Generation parameters for a model, mirrored into an OcrRequest.
 */
struct GenerationParams {
    double temperature = 0.0;
    int maxTokens = 4096;
};

/**
 * @brief A model profile loaded from a JSON file.
 *
 * Describes everything the application needs to talk to one specific OCR model
 * through an OpenAI-compatible endpoint: which model id to send, the task
 * prompt, which output parser interprets the reply, and generation settings.
 *
 * `runnerHints` is intentionally kept as a raw JSON object: it is metadata for
 * the user/runner (mode, DPI, image size) and is never sent to the API.
 */
struct ModelProfile {
    QString modelId;              ///< Sent as "model" in the API request.
    QString displayName;          ///< Human-readable name for the UI.
    QString promptTemplate;       ///< Task prompt, e.g. "document parsing.".
    QString outputParser;         ///< Parser id: "det_tokens", "raw", ...
    bool supportsBbox = false;    ///< Whether the model returns positions.
    int bboxCoordinateRange = 1000; ///< Raw coordinate scale (0..this).
    GenerationParams generation;  ///< temperature / max_tokens.
    QString preferredImageFormat = QStringLiteral("png"); ///< Encode format.
    QJsonObject runnerHints;      ///< Free-form hints, not sent to the API.

    /**
     * @brief Build a ModelProfile from a parsed JSON object.
     * @param obj The root object of a profile file.
     * @param ok  Set to false if a required field (model_id) is missing.
     * @return A profile with defaults applied for any absent optional fields.
     */
    static ModelProfile fromJson(const QJsonObject& obj, bool* ok = nullptr);

    /// True when the profile has at least a usable model id.
    bool isValid() const { return !modelId.isEmpty(); }
};

} // namespace llocr
