#pragma once

#include <QString>

namespace llocr {

struct ParserOptions {
    bool keepHeadersAndPageNumbers = true;  ///< Output → "Keep headers and page numbers" (drops the page furniture).
    bool tablesAsHtml = false;              ///< Output → "Keep tables as HTML".
    int bboxRange = 1000;                   ///< Coordinate scale the model emits bboxes in.
    QString modelId;                        ///< OCR model adapter id — selects label-styling overrides.
    qreal pageAspect = 1.4142;              ///< H/W of the page image the reply answers (A4 by default).
};

}  // namespace llocr
