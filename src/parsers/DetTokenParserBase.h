#pragma once

#include <QRect>
#include <QPair>
#include <QRegularExpression>

#include "parsers/IOutputParser.h"
#include "parsers/ParserOptions.h"

namespace llocr {

// The rotation word a layout model reports for a block (TeleOCR's
// `<orientation>`, MinerU's `<|rotate_…|>`): the direction the block's text
// top points. An unknown word reads as up — inert, the same answer a block
// without the token gives.
inline int orientationDegrees(const QString &word)
{
    if (word == QLatin1String("right"))
        return 90;
    if (word == QLatin1String("down"))
        return 180;
    if (word == QLatin1String("left"))
        return 270;
    return 0;
}

// The page-level turn the block rotation tokens vote for (ADR 245): the
// clockwise turn that levels the page, when the vote is consistent — one
// direction took at least nine tenths of the blocks that voted against up —
// and the page's layout backs it: at least six tenths of all lines voted
// against up at all. Stray up blocks (headers, page numbers the model reads
// upright on a sideways page) do not sink the advice; a page whose blocks
// disagree about half and half is no advice at all. First = the turn (90,
// 180, 270; 0 = no winner), second = the share of all lines the winner took
// (0..100).
inline QPair<int, int> orientationVote(const QList<int> &orientations)
{
    if (orientations.isEmpty())
        return {0, 0};
    int counts[4] = {0, 0, 0, 0};
    for (const int value : orientations) {
        if (value % 90 != 0 || value < 0 || value > 270)
            return {0, 0};
        ++counts[value / 90];
    }
    const int total = orientations.size();
    const int nonUp = total - counts[0];
    int winner = 0;
    for (int candidate = 1; candidate < 4; ++candidate) {
        if (counts[candidate] > counts[winner])
            winner = candidate;
    }
    const int share = counts[winner] * 100 / total;
    if (winner == 0 || nonUp * 10 < total * 6 || counts[winner] * 10 < nonUp * 9)
        return {0, share};
    return {(360 - winner * 90) % 360, share};
}

class DetTokenParserBase : public IOutputParser
{
    Q_DISABLE_COPY_MOVE(DetTokenParserBase)

public:
    explicit DetTokenParserBase(const ParserOptions &options = {}) : m_options(options) {}

    OcrResult parse(const QString &rawText) const override;
    QString rebuildText(const OcrPage &page) const override;
    RebuiltPageText rebuildTextWithRanges(const OcrPage &page) const override;

    virtual const QRegularExpression &tokenRegex(const QString &preparedText) const = 0;

    virtual bool escapesLineBreaks(const QString &preparedText) const = 0;

    virtual QString prepareText(const QString &rawText) const;

    // Compensation of a model-wide systematic coordinate bias; identity for
    // models whose [0, bboxRange] coordinates are already page-accurate.
    virtual QRectF calibrateRect(const QRectF &rect) const;

    ParserOptions m_options;
};

}  // namespace llocr
