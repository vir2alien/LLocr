#pragma once

#include <QRegularExpression>

#include "parsers/IOutputParser.h"
#include "parsers/ParserOptions.h"

namespace llocr {

class DetTokenParserBase : public IOutputParser
{
    Q_DISABLE_COPY_MOVE(DetTokenParserBase)

public:
    explicit DetTokenParserBase(const ParserOptions &options = {}) : m_options(options) {}

    OcrResult parse(const QString &rawText) const override;
    QString rebuildText(const OcrPage &page) const override;

    virtual const QRegularExpression &tokenRegex(const QString &preparedText) const = 0;

    virtual bool escapesLineBreaks(const QString &preparedText) const = 0;

    virtual QString prepareText(const QString &rawText) const;

    ParserOptions m_options;
};

}  // namespace llocr
