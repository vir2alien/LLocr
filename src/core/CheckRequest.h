#pragma once

#include <QImage>
#include <QList>
#include <QString>

#include "core/RequestProfile.h"

namespace llocr {

struct CheckRequest {
    QImage image;                    ///< Crop of the selected block being verified.
    QString recognizedText;          ///< Text previously recognized by the OCR model.
    QString systemPrompt;            ///< System-level instruction for the checking model.
    QString typePrompt;              ///< Instruction for this block's type (from the verification prompts).
    QString modelId;                 ///< Model to route the request to (from the connection).
    QList<RequestParameter> parameters;  ///< Sampling parameters from the validate request profile.
};

}  // namespace llocr
