#pragma once

#include <QString>

namespace llocr {

struct ParserOptions {
    bool keepPageNumbers = true;  ///< Output → "Keep page numbers" (drops page_number blocks).
    bool tablesAsHtml = false;    ///< Output → "Keep tables as HTML".
    int bboxRange = 1000;         ///< Coordinate scale the model emits bboxes in.
    QString modelId;              ///< OCR model adapter id — selects label-styling overrides.
};

}  // namespace llocr
