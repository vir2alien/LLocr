#pragma once

#include <QImage>
#include <QString>

namespace llocr {

// The `protocol` value of a decision role whose model compares the crop with
// the candidate rendered back into an image (MiniCPM-OCR Jev, docs/31): the
// task wording is baked into the model's prompt, the request carries two
// images and a bare noul question.
inline QString visualDiffDecisionProtocol()
{
    return QStringLiteral("systemone_visual_diff_v1");
}

// The /v1/systemone request (a llama.cpp extension): a named question asked
// over a structured state plus images, answered in one pass with zero
// generated tokens. The state is a JSON object — the candidate text sits in a
// named field, which is what makes the model compare it against the crop (a
// raw string reduces the answer to "is there text on the image",
// docs/17-d1-verification-study.md). The served model is fixed per
// llama-server, so there is no model field to send.
struct DecisionRequest {
    QImage image;             ///< Crop of the block being judged.
    QImage renderImage;       ///< The candidate text rendered back into an image; visualDiff requests carry it next to the crop.
    QString blockType;        ///< The block's label, carried into state.block_type.
    QString recognizedText;   ///< The OCR candidate text, carried into state.recognized_text (the bare state of a visualDiff request).
    QString notation;         ///< Notation disclaimer carried into state.notation (the profile's statePrompt); empty omits the field.
    QString question;         ///< The single yes/no question wording (the decision role's question).
    QString criteriaTrue;     ///< Optional noul criteria: what counts as a match; empty omits the field.
    QString criteriaFalse;    ///< Optional noul criteria: what counts as a difference; empty omits the field.
    bool visualDiff = false;  ///< The visual-diff request shape: string state, two images, a bare noul question (no instructions — the server rejects them).
};

}  // namespace llocr
