#pragma once

#include "parsers/DetTokenParserBase.h"

namespace llocr {

// TeleOCR (NaviDC-OCR) layout pass: one line per block,
// "<box:x1 y1 x2 y2><label:type><orientation>", coordinates on a 0–1000 scale,
// possibly more than four numbers (a polygon). The base parser expects
// "label [x1,y1,x2,y2]" tokens with the label in group 1, so prepareText
// rewrites each line into that shape — taking the bounding rect of the
// polygon — and drops everything the model printed besides the block lines.
class TeleOcrLayoutParser : public DetTokenParserBase
{
    Q_DISABLE_COPY_MOVE(TeleOcrLayoutParser)

public:
    using DetTokenParserBase::DetTokenParserBase;

    QString id() const override;
    QString displayName() const override;

protected:
    QString prepareText(const QString &rawText) const override;
    const QRegularExpression &tokenRegex(const QString &preparedText) const override;
    bool escapesLineBreaks(const QString &preparedText) const override;
};

}  // namespace llocr
