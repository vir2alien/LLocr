#pragma once

#include "parsers/IOutputParser.h"

namespace llocr {

/**
 * @brief Parser for the "<|det|> label [x1, y1, x2, y2] <|/det|> text" format
 *
 * Output example:
 *   <|det|>title [357, 135, 642, 155]<|/det|>Some heading
 *   <|det|>text [120, 200, 890, 340]<|/det|>Body text...
 *   <PAGE>
 *   <|det|>text [100, 150, 800, 300]<|/det|>Next page
 */

class DetTokensParser : public IOutputParser {
public:
    OcrResult parse(const QString &rawText) const override;
    QString id() const override;

private:
    static OcrPage parsePage(const QString& pageText, int coordRange);

private:
    int m_bboxCoordinateRange = 1000;
};

} // namespace llocr
