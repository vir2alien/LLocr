#include "parsers/LightonOcrParser.h"

namespace llocr {

namespace {

const QRegularExpression &groundingTokenRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(!\[([A-Za-z_][A-Za-z0-9_]*)\]\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\))"));
    return re;
}

const QRegularExpression &continuationLabelRegex()
{
    // A trailing `+` on a label marks a block continued from the previous
    // page; the style map and the block filters speak the bare label.
    static const QRegularExpression re(QStringLiteral(R"(!\[([A-Za-z_][A-Za-z0-9_]*)\+\]\()"));
    return re;
}

}  // namespace

QString LightonOcrParser::id() const
{
    return QStringLiteral("lightonocr");
}

QString LightonOcrParser::displayName() const
{
    return QStringLiteral("LightOnOCR-3");
}

QString LightonOcrParser::prepareText(const QString &rawText) const
{
    QString out = rawText;
    out.replace(continuationLabelRegex(), QStringLiteral("![\\1]("));
    return out;
}

const QRegularExpression &LightonOcrParser::tokenRegex(const QString &) const
{
    return groundingTokenRegex();
}

bool LightonOcrParser::escapesLineBreaks(const QString &) const
{
    return false;
}

}  // namespace llocr
