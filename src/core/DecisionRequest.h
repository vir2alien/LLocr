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
// over a state plus images, answered in one pass with zero generated tokens.
// The state is the bare candidate text; the images are the crop and the
// candidate rendered back into an image, in that order (docs/31). The served
// model is fixed per llama-server, so the `model` field names it — a server
// that routes by the field rejects a request without it.
struct DecisionRequest {
    QImage image;             ///< Crop of the block being judged.
    QImage renderImage;       ///< The candidate text rendered back into an image; travels next to the crop.
    QString recognizedText;   ///< The OCR candidate text — the request's state.
    bool visualDiff = false;  ///< The visual-diff request shape; the only wire shape the decision speaks.
};

}  // namespace llocr
