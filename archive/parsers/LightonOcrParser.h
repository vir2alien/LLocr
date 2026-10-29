#pragma once

#include "parsers/DetTokenParserBase.h"

namespace llocr {

class LightonOcrParser : public DetTokenParserBase
{
    Q_DISABLE_COPY_MOVE(LightonOcrParser)

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
