#pragma once

#include "parsers/DetTokenParserBase.h"

namespace llocr {

// MinerU2.5-Pro layout pass: one "<|box_start|>x1 y1 x2 y2<|box_end|>"
// "<|ref_start|>type<|ref_end|>" line per block, coordinates on a 0–1000 scale
// for the page squashed to 1036×1036, an optional <|rotate_*|> token and a
// continuation flag in the tail. The base parser expects
// "label [x1,y1,x2,y2]" tokens with the label in group 1, so prepareText
// rewrites each line into that shape — following the upstream client's
// cleanup (inline formulas skipped, unknown regions read as images, degenerate
// and out-of-scale boxes dropped) and dropping everything the model printed
// besides the block lines.
class MinerULayoutParser : public DetTokenParserBase
{
    Q_DISABLE_COPY_MOVE(MinerULayoutParser)

public:
    using DetTokenParserBase::DetTokenParserBase;

    QString id() const override;
    QString displayName() const override;

    OcrResult parse(const QString &rawText) const override;

protected:
    QString prepareText(const QString &rawText) const override;
    const QRegularExpression &tokenRegex(const QString &preparedText) const override;
    bool escapesLineBreaks(const QString &preparedText) const override;
};

}  // namespace llocr
