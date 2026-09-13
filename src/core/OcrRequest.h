#pragma once

#include <QImage>
#include <QList>
#include <QString>

#include "core/RequestProfile.h"

namespace llocr {

struct OcrRequest {
    QImage image;
    QString prompt;
    QString modelId;

    // Request-body parameters from the request profile (ordered; see
    // RequestProfile). The model appends them after model/messages.
    QList<RequestParameter> parameters;
};

}  // namespace llocr
