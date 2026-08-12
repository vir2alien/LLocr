#include "parsers/DetTokensParser.h"

#include <QRegularExpression>
#include <QStringList>

namespace llocr {

namespace {

const QString kPageMarker = QStringLiteral("<PAGE>");

QRegularExpression fragmentRegex()
{
    static const QRegularExpression re(
        QStringLiteral(
            R"(<\|det\|>\s*([^\[]*?)\s*\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\]\s*<\|/det\|>(.*?)(?=<\|det\|>|$))"),
        QRegularExpression::DotMatchesEverythingOption);
    return re;
}

} // namespace

OcrPage DetTokensParser::parsePage(const QString& pageText, int coordRange)
{
    OcrPage page;
    QStringList textPieces;

    const double range = coordRange > 0 ? static_cast<double>(coordRange) : 1000.0;

    const QRegularExpression re = fragmentRegex();
    QRegularExpressionMatchIterator it = re.globalMatch(pageText);

    bool anyMatch = false;
    while (it.hasNext()) {
        anyMatch = true;
        const QRegularExpressionMatch m = it.next();

        const QString label = m.captured(1).trimmed();
        const double x1 = m.captured(2).toDouble();
        const double y1 = m.captured(3).toDouble();
        const double x2 = m.captured(4).toDouble();
        const double y2 = m.captured(5).toDouble();
        const QString fragmentText = m.captured(6).trimmed();

        BoundingBox box;
        box.text = fragmentText;
        box.label = label;  // TODO: used later for Markdown styling.

        const double nx = x1 / range;
        const double ny = y1 / range;
        const double nw = (x2 - x1) / range;
        const double nh = (y2 - y1) / range;
        box.rect = QRectF(nx, ny, nw, nh);

        page.boxes.append(box);
        if (!fragmentText.isEmpty()) {
            textPieces.append(fragmentText);
        }
    }

    if (!anyMatch) {
        page.text = pageText.trimmed();
    } else {
        page.text = textPieces.join(QStringLiteral("\n\n"));
    }

    return page;
}

OcrResult DetTokensParser::parse(const QString &rawText) const
{
    OcrResult result;
    result.success = true;

    OcrPage page;
    QStringList plainLines;

    static const QRegularExpression headerRe(
        QStringLiteral(R"((\w+)\s*\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\])"));

    auto it = headerRe.globalMatch(rawText);

    struct Header {
        QString label;
        int x1, y1, x2, y2;
        int textStart;
        int headerStart;
    };
    QList<Header> headers;

    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        Header h;
        h.label = m.captured(1);
        h.x1 = m.captured(2).toInt();
        h.y1 = m.captured(3).toInt();
        h.x2 = m.captured(4).toInt();
        h.y2 = m.captured(5).toInt();
        h.textStart = m.capturedEnd(0);
        h.headerStart = m.capturedStart(0);
        headers.append(h);
    }

    if (headers.isEmpty()) {
        page.text = rawText.trimmed();
        result.text = page.text;
        result.pages.append(page);
        return result;
    }

    const double range = m_bboxCoordinateRange;

    for (int i = 0; i < headers.size(); ++i) {
        const Header& h = headers.at(i);

        const int spanEnd = (i + 1 < headers.size())
                                ? headers.at(i + 1).headerStart
                                : rawText.length();
        QString text = rawText.mid(h.textStart, spanEnd - h.textStart).trimmed();

        BoundingBox box;
        box.label = h.label;
        box.text = text;

        const double nx = h.x1 / range;
        const double ny = h.y1 / range;
        const double nw = (h.x2 - h.x1) / range;
        const double nh = (h.y2 - h.y1) / range;
        box.rect = QRectF(nx, ny, nw, nh);

        page.boxes.append(box);

        if (!text.isEmpty()) {
            plainLines.append(text);
        }
    }

    page.text = plainLines.join(QStringLiteral("\n"));
    result.text = page.text;
    result.pages.append(page);
    return result;
}

QString DetTokensParser::id() const
{
    return QStringLiteral("det_tokens");
}

} // namespace llocr