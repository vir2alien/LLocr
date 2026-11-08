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
    QString profileId;  ///< Profile the role lookups resolve by: on a managed connection modelId is the server's alias, which no profile carries.
    QList<RequestParameter> parameters;
};

}  // namespace llocr
