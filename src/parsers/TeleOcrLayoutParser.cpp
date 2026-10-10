#include "parsers/TeleOcrLayoutParser.h"

#include <QStringList>

#include <algorithm>
#include <limits>

namespace llocr {

namespace {

const QRegularExpression &layoutLineRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(<box:\s*([\d\s]+?)\s*>\s*<label:(\w+)>\s*<[^>]*>)"));
    return re;
}

const QRegularExpression &canonicalTokenRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\])"));
    return re;
}

}  // namespace

QString TeleOcrLayoutParser::id() const
{
    return QStringLiteral("teleocr-layout");
}

QString TeleOcrLayoutParser::displayName() const
{
    return QStringLiteral("TeleOCR layout");
}

QString TeleOcrLayoutParser::prepareText(const QString &rawText) const
{
    QRegularExpressionMatchIterator it = layoutLineRegex().globalMatch(rawText);
    if (!it.hasNext())
        return rawText;

    QString out;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QStringList numbers = m.captured(1).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (numbers.size() < 4 || numbers.size() % 2 != 0)
            continue;

        int x1 = std::numeric_limits<int>::max();
        int y1 = std::numeric_limits<int>::max();
        int x2 = std::numeric_limits<int>::min();
        int y2 = std::numeric_limits<int>::min();
        for (int i = 0; i < numbers.size(); i += 2) {
            const int x = numbers.at(i).toInt();
            const int y = numbers.at(i + 1).toInt();
            x1 = std::min(x1, x);
            x2 = std::max(x2, x);
            y1 = std::min(y1, y);
            y2 = std::max(y2, y);
        }

        out += QStringLiteral("%1 [%2,%3,%4,%5]\n").arg(m.captured(2)).arg(x1).arg(y1).arg(x2).arg(y2);
    }
    return out;
}

const QRegularExpression &TeleOcrLayoutParser::tokenRegex(const QString &) const
{
    return canonicalTokenRegex();
}

bool TeleOcrLayoutParser::escapesLineBreaks(const QString &) const
{
    return false;
}

}  // namespace llocr
