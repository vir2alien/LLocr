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
    QList<RequestParameter> parameters;
};

}  // namespace llocr
