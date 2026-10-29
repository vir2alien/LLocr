#include "parsers/TeleOcrLayoutParser.h"

#include <QStringList>

#include <algorithm>
#include <limits>

namespace llocr {

namespace {

const QRegularExpression &layoutLineRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(<box:\s*([\d\s]+?)\s*>\s*<label:(\w+)>\s*(?:<([a-z]+)>)?)"));
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

        int x1 = (std::numeric_limits<int>::max)();
        int y1 = (std::numeric_limits<int>::max)();
        int x2 = (std::numeric_limits<int>::min)();
        int y2 = (std::numeric_limits<int>::min)();
        for (int i = 0; i < numbers.size(); i += 2) {
            const int x = numbers.at(i).toInt();
            const int y = numbers.at(i + 1).toInt();
            x1 = (std::min)(x1, x);
            x2 = (std::max)(x2, x);
            y1 = (std::min)(y1, y);
            y2 = (std::max)(y2, y);
        }

        out += QStringLiteral("%1 [%2,%3,%4,%5]\n").arg(m.captured(2)).arg(x1).arg(y1).arg(x2).arg(y2);
    }
    return out;
}

OcrResult TeleOcrLayoutParser::parse(const QString &rawText) const
{
    OcrResult result = DetTokenParserBase::parse(rawText);
    if (result.pages.isEmpty())
        return result;

    OcrPage &page = result.pages.first();
    // The orientation tag the reply carries after every block (ADR 245): the
    // extraction walks the same lines prepareText keeps, so the zipping onto
    // the base's boxes only fires when the two lists line up — any surprise
    // (a preamble, a dropped line) leaves the blocks unmarked and the
    // page-level vote inert.
    struct LayoutLine {
        QString label;
        int orientation = 0;
    };
    QList<LayoutLine> lines;
    QRegularExpressionMatchIterator it = layoutLineRegex().globalMatch(rawText);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QStringList numbers = m.captured(1).simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (numbers.size() < 4 || numbers.size() % 2 != 0)
            continue;
        lines.append({m.captured(2), orientationDegrees(m.captured(3))});
    }
    // The vote reads the reply's lines directly — the page-level advice must
    // not depend on the box zip lining up.
    QList<int> orientations;
    orientations.reserve(lines.size());
    for (const LayoutLine &line : lines)
        orientations.append(line.orientation);
    const auto [voteTurn, voteShare] = orientationVote(orientations);
    page.orientationVote = voteTurn;
    page.orientationVoteShare = voteShare;
    const int kept = m_options.keepPageNumbers ? lines.size() : int(std::count_if(lines.cbegin(), lines.cend(), [](const LayoutLine &line) { return line.label != QLatin1String("page_number"); }));
    // The base prepends a preamble box when the reply opens with text: a
    // mismatch means the lists do not line up and nothing is zipped.
    if (kept == page.boxes.size()) {
        int boxIndex = 0;
        for (const LayoutLine &line : lines) {
            if (!m_options.keepPageNumbers && line.label == QLatin1String("page_number"))
                continue;
            page.boxes[boxIndex].orientation = line.orientation;
            ++boxIndex;
        }
    }
    return result;
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
