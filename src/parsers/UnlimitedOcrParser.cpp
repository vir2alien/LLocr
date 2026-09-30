#include "parsers/UnlimitedOcrParser.h"

#include "parsers/DetTokenFormat.h"

namespace llocr {

namespace {

const QRegularExpression &wrappedTokenRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(<\|det\|>([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\]<\|/det\|>)"));
    return re;
}

const QRegularExpression &bareTokenRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\])"));
    return re;
}

}  // namespace

QString UnlimitedOcrParser::id() const
{
    return QStringLiteral("unlimited-ocr");
}

QString UnlimitedOcrParser::displayName() const
{
    return QStringLiteral("Unlimited-OCR");
}

const QRegularExpression &UnlimitedOcrParser::tokenRegex(const QString &preparedText) const
{
    return preparedText.contains(QStringLiteral("<|det|>")) ? wrappedTokenRegex() : bareTokenRegex();
}

bool UnlimitedOcrParser::escapesLineBreaks(const QString &preparedText) const
{
    return preparedText.contains(QStringLiteral("<|det|>"));
}

}  // namespace llocr
