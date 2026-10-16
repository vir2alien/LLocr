#pragma once

#include <QImage>
#include <QList>
#include <QString>

#include "core/RequestProfile.h"

namespace llocr {

struct CheckRequest {
    // The reply contract the request asks of the universal model: block
    // recognition transcribes the block (the reply is the text), the fix
    // protocol answers OK / FIX / REVIEW over the candidate (ADR 163).
    enum class Protocol { Recognition, Fix };

    QImage image;                        ///< Crop of the selected block being recognized.
    QString systemPrompt;                ///< System-level instruction for the checking model.
    QString typePrompt;                  ///< Instruction for this block's type (from the verification prompts).
    QString recognizedText;              ///< Fix protocol: the OCR candidate under correction.
    QString modelId;                     ///< Model to route the request to (from the connection).
    QList<RequestParameter> parameters;  ///< Sampling parameters from the validate request profile.
    Protocol protocol = Protocol::Recognition;
    // The reply is model-structured markup for a downstream parser (the layout
    // pass): its <|...|> tokens are the data, so the reply must reach the
    // parser without the service-token cleanup the block text gets.
    bool structuralReply = false;
};

}  // namespace llocr
