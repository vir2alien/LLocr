#pragma once

#include <QImage>
#include <QString>

namespace llocr {

// The /v1/systemone request (a llama.cpp extension): a named question asked
// over a state (text) plus images, answered in one pass with zero generated
// tokens. The served model is fixed per llama-server, so there is no model
// field to send.
struct DecisionRequest {
    QImage image;       ///< Crop of the block being judged.
    QString stateText;  ///< The state: the OCR candidate text of the block.
    QString question;   ///< The yes/no question wording (the decision role's systemPrompt).
};

}  // namespace llocr
