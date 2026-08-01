#include "core/ModelProfile.h"

#include <QJsonValue>

namespace llocr {

ModelProfile ModelProfile::fromJson(const QJsonObject& obj, bool* ok) {
    ModelProfile profile;

    // Required field: without a model id the profile is unusable.
    profile.modelId = obj.value(QStringLiteral("model_id")).toString();
    if (profile.modelId.isEmpty()) {
        if (ok) {
            *ok = false;
        }
        return profile;
    }

    // Optional fields fall back to sensible defaults.
    profile.displayName =
        obj.value(QStringLiteral("display_name")).toString(profile.modelId);
    profile.promptTemplate =
        obj.value(QStringLiteral("prompt_template")).toString();
    profile.outputParser =
        obj.value(QStringLiteral("output_parser")).toString(QStringLiteral("raw"));
    profile.supportsBbox =
        obj.value(QStringLiteral("supports_bbox")).toBool(false);
    profile.bboxCoordinateRange =
        obj.value(QStringLiteral("bbox_coordinate_range")).toInt(1000);

    // Nested "generation" object.
    const QJsonObject gen = obj.value(QStringLiteral("generation")).toObject();
    profile.generation.temperature =
        gen.value(QStringLiteral("temperature")).toDouble(0.0);
    profile.generation.maxTokens =
        gen.value(QStringLiteral("max_tokens")).toInt(4096);

    // Nested "image" object.
    const QJsonObject image = obj.value(QStringLiteral("image")).toObject();
    profile.preferredImageFormat =
        image.value(QStringLiteral("preferred_format")).toString(QStringLiteral("png"));

    // Runner hints are stored verbatim; the app does not interpret them.
    profile.runnerHints = obj.value(QStringLiteral("runner_hints")).toObject();

    if (ok) {
        *ok = true;
    }
    return profile;
}

} // namespace llocr
