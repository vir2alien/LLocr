#pragma once

#include <QRect>
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
