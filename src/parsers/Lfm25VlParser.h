#pragma once

#include "parsers/DetTokenParserBase.h"

namespace llocr {

class Lfm25VlParser : public DetTokenParserBase
{
    Q_DISABLE_COPY_MOVE(Lfm25VlParser)

public:
    using DetTokenParserBase::DetTokenParserBase;

    QString id() const override;
    QString displayName() const override;

protected:
    const QRegularExpression &tokenRegex(const QString &preparedText) const override;
    bool escapesLineBreaks(const QString &preparedText) const override;
    QString prepareText(const QString &rawText) const override;
};

}  // namespace llocr
