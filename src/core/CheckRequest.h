#pragma once

#include <QImage>
#include <QList>
#include <QString>

#include "core/RequestProfile.h"

namespace llocr {

// Request for the text-verification (check) flow. Unlike OCR, the request
// carries the previously recognized text together with the block image and
// a user-provided instruction for the general-purpose (checking) model.
struct CheckRequest {
    QImage image;                    ///< Crop of the selected block being verified.
    QString recognizedText;          ///< Text previously recognized by the OCR model.
    QString prompt;                  ///< User-provided instruction for the checking model.
    QString modelId;                 ///< Model to route the request to (from the connection).
    QList<RequestParameter> parameters;  ///< Sampling parameters from the validate request profile.
};

}  // namespace llocr
