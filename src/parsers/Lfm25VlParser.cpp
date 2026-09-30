#include "parsers/Lfm25VlParser.h"

#include "parsers/Lfm25VlDrift.h"

namespace llocr {

namespace {

const QRegularExpression &annotationTokenRegex()
{
    static const QRegularExpression re(QStringLiteral("(?|"
                                                      "(?:image_index=\\d+\\s+)?([A-Za-z_][A-Za-z0-9_]*)\\s*\\[\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\]"
                                                      "|"
                                                      "image_index=\\d+\\s+<label>([A-Za-z_][A-Za-z0-9_]*)</label>(?:\\s*\\[\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\])?"
                                                      ")"));
    return re;
}

}  // namespace

QString Lfm25VlParser::id() const
{
    return QStringLiteral("lfm2.5-vl");
}

QString Lfm25VlParser::displayName() const
{
    return QStringLiteral("LFM2.5-VL");
}

const QRegularExpression &Lfm25VlParser::tokenRegex(const QString &) const
{
    return annotationTokenRegex();
}

bool Lfm25VlParser::escapesLineBreaks(const QString &) const
{
    return false;  // the annotation stream carries real newlines
}

QString Lfm25VlParser::prepareText(const QString &rawText) const
{
    return normalizeDriftRegions(rawText);
}

}  // namespace llocr
