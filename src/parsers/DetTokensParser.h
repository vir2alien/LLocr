#pragma once

#include "parsers/IOutputParser.h"

namespace llocr {

/**
 * @brief Parser for the model's `<|det|>` block stream
 *
 * The current model emits one token per block, wrapped in det tags:
 *
 *   <|det|>title [115, 101, 273, 117]<|/det|>1. Introduction\n
 *   <|det|>text [112, 132, 884, 309]<|/det|>Humans are …\n
 *   <|det|>page_number [493, 924, 506, 935]<|/det|>3
 *
 * Content is JSON-escaped (a real newline is streamed as the two characters
 * `\n`, a LaTeX `\(` as `\\(`); the parser unescapes it back to real text
 * and strips model control tokens (e.g. a trailing `<|end_of_sentence|>`,
 * including its full-width-pipe variant).
 * Labels are model-reported block types: title, text, image, image_caption,
 * page_number, footer, etc.  Coordinates are integer pixel values (typically
 * 0–1000 range) and are normalized to [0,1].
 *
 * The legacy bare form `label [x1, y1, x2, y2] text` (no wrappers) is still
 * accepted as a fallback.
 *
 * Multi-page input (raw model response) is NOT split here — the caller
 * (AppController) feeds one page at a time.  The result is always a
 * single-page OcrResult.
 */

class DetTokensParser : public IOutputParser {
    Q_DISABLE_COPY_MOVE(DetTokensParser)

public:
    DetTokensParser() = default;

    OcrResult parse(const QString &rawText) const override;
    QString id() const override;

private:
    static constexpr int kBboxCoordinateRange = 1000;
};

} // namespace llocr