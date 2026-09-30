#pragma once

#include "parsers/DetTokenParserBase.h"

namespace llocr {

class UnlimitedOcrParser : public DetTokenParserBase
{
    Q_DISABLE_COPY_MOVE(UnlimitedOcrParser)

public:
    using DetTokenParserBase::DetTokenParserBase;

    QString id() const override;
    QString displayName() const override;

protected:
    const QRegularExpression &tokenRegex(const QString &preparedText) const override;
    bool escapesLineBreaks(const QString &preparedText) const override;
};

}  // namespace llocr
