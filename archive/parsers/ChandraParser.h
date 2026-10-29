#pragma once

#include "parsers/IOutputParser.h"
#include "parsers/ParserOptions.h"

namespace llocr {

// Chandra-OCR-2 answers with HTML. The layout prompt wraps every block in a
// div that carries the block's box and its label; the plain prompt answers
// with the same HTML without the layout markup.
class ChandraParser : public IOutputParser
{
    Q_DISABLE_COPY_MOVE(ChandraParser)

public:
    explicit ChandraParser(const ParserOptions &options = {}) : m_options(options) {}

    OcrResult parse(const QString &rawText) const override;
    QString rebuildText(const OcrPage &page) const override;
    IOutputParser::RebuiltPageText rebuildTextWithRanges(const OcrPage &page) const override;
    QString id() const override;
    QString displayName() const override;

private:
    ParserOptions m_options;
};

}  // namespace llocr
