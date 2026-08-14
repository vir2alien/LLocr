#pragma once

#include "parsers/IOutputParser.h"

namespace llocr {

/**
 * @brief Parser for the "label [x1, y1, x2, y2] text" format
 *
 * Each line is a plain `label [coords] text` token without <|det|> wrappers.
 * Labels are model-reported block types: title, text, image,
 * image_caption, page_number, footer, etc.  Coordinates are integer
 * pixel values (typically 0–1000 range) and are normalized to [0,1].
 *
 * Output example:
 *   title [92, 109, 890, 165] Document Title
 *   text [81, 304, 745, 400] Body text starts here
 *   image [132, 118, 862, 269]
 *   image_caption [113, 276, 885, 374] Figure 2 | A caption
 *   page_number [493, 923, 506, 935] 5
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