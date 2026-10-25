#pragma once

#include <QImage>
#include <QJsonObject>
#include <QString>

namespace llocr {

// The /v1/systemone request (a llama.cpp extension): a named question asked
// over a structured state plus images, answered in one pass with zero
// generated tokens. The state is a JSON object — the candidate text sits in a
// named field, which is what makes the model compare it against the crop (a
// raw string reduces the answer to "is there text on the image",
// docs/17-d1-verification-study.md). The served model is fixed per
// llama-server, so there is no model field to send.
struct DecisionRequest {
    QImage image;            ///< Crop of the block being judged.
    QString blockType;       ///< The block's label, carried into state.block_type.
    QString recognizedText;  ///< The OCR candidate text, carried into state.recognized_text.
    QString notation;        ///< Notation disclaimer carried into state.notation (the profile's statePrompt); empty omits the field.
    QString question;        ///< The single yes/no question wording (the decision role's question).
    QString criteriaTrue;    ///< Optional noul criteria: what counts as a match; empty omits the field.
    QString criteriaFalse;   ///< Optional noul criteria: what counts as a difference; empty omits the field.
    QJsonObject questions;   ///< A ready-made questions dict, handed to the endpoint as it is. The verification asks a yes/no question; the orientation check asks for a choice. Empty means the yes/no
                             ///< `match` question.
};

}  // namespace llocr
