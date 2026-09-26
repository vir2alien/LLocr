#pragma once

#include <QString>

namespace llocr {

// Everything a parser needs to know besides the model reply itself. Passed to
// ParserFactory::create() so that the app layer configures a parser without
// knowing its concrete type (the settings live here, not in AppController).
// Aggregate-initializable: ParserOptions{keepPageNumbers, tablesAsHtml}.
struct ParserOptions {
    bool keepPageNumbers = true;   ///< Output → "Keep page numbers" (drops page_number blocks).
    bool tablesAsHtml = false;     ///< Output → "Keep tables as HTML" (ADR 64).
    int bboxRange = 1000;          ///< Coordinate scale the model emits bboxes in.
    QString modelId;               ///< OCR model adapter id — selects label-styling overrides.
};

} // namespace llocr
