#include "parsers/DetTokensParser.h"
#include "parsers/BlockStyle.h"

#include "core/ServiceMarkers.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace llocr {

namespace {

// Regex helpers

// <|det|>label [x1, y1, x2, y2]<|/det|>
const QRegularExpression &wrappedTokenRegex()
{
    static const QRegularExpression re(
        QStringLiteral(R"(<\|det\|>([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\]<\|/det\|>)"));
    return re;
}

// Legacy fallback: a bare  label [x1, y1, x2, y2]  token without wrappers.
const QRegularExpression &tokenStartRegex()
{
    static const QRegularExpression re(
        QStringLiteral(R"(([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\])"));
    return re;
}

QString unescapeModelText(const QString &text)
{
    QString out;
    out.reserve(text.size());
    const QChar backslash = QLatin1Char('\\');
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c != backslash || i + 1 >= text.size()) {
            out.append(c);
            continue;
        }
        const QChar n = text.at(i + 1);
        switch (n.unicode()) {
        case 'n':  out.append(QLatin1Char('\n')); ++i; break;
        case 't':  out.append(QLatin1Char('\t')); ++i; break;
        case 'r':  out.append(QLatin1Char('\r')); ++i; break;
        case 'b':  out.append(QLatin1Char('\b')); ++i; break;
        case 'f':  out.append(QLatin1Char('\f')); ++i; break;
        case '/':  out.append(QLatin1Char('/'));  ++i; break;
        case '"':  out.append(QLatin1Char('"'));  ++i; break;
        case '\\': out.append(QLatin1Char('\\')); ++i; break;
        default:   out.append(c); break; // unknown escape -> keep as-is
        }
    }
    return out;
}

// LaTeX math to Markdown
QString convertMath(const QString &text)
{
    static const QRegularExpression inlineRe(
        QStringLiteral(R"(\\\(\s*(.*?)\s*\\\))"),
        QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression displayRe(
        QStringLiteral(R"(\\\[\s*(.*?)\s*\\\])"),
        QRegularExpression::DotMatchesEverythingOption);

    QString out = text;
    out.replace(displayRe, QStringLiteral("\n\n$$\n\\1\n$$\n\n"));
    out.replace(inlineRe, QStringLiteral("$\\1$"));
    return out;
}

// Display (block) math -> clean Markdown block.
QString formatEquation(const QString &text)
{
    static const QRegularExpression wrapperRe(
        QStringLiteral(R"(^\s*\\\[\s*([\s\S]*?)\s*\\\]\s*$)"));

    QString body = text.trimmed();
    const QRegularExpressionMatch m = wrapperRe.match(body);
    if (m.hasMatch())
        body = m.captured(1).trimmed();

    return QStringLiteral("$$\n%1\n$$").arg(body.trimmed());
}

// Title to heading level
int headingLevelFor(const QString &title)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*(\d+\s*\.\s*)+)"));
    const QRegularExpressionMatch m = re.match(title);
    if (!m.hasMatch())
        return 2;

    int groups = 0;
    for (int i = 0; i < m.capturedLength(0); ++i) {
        if (m.captured(0).at(i) == QLatin1Char('.'))
            ++groups;
    }
    return std::clamp(groups + 1, 1, 5);
}

QString escapeTableCell(QString cell)
{
    cell = convertMath(cell);
    static const QRegularExpression unescapedPipeRe(QStringLiteral(R"((?<!\\)\|)"));
    cell.replace(unescapedPipeRe, QStringLiteral(R"(\|)"));
    cell.replace(QLatin1Char('\n'), QLatin1Char(' '));
    cell = cell.trimmed();
    return cell;
}

QString formatTable(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (!trimmed.contains(QStringLiteral("<table")))
        return convertMath(trimmed);

    static const QRegularExpression rowRe(
        QStringLiteral(R"(<tr\b[^>]*>([\s\S]*?)</\s*tr\s*>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression cellRe(
        QStringLiteral(R"(<(td|th)\b([^>]*)>([\s\S]*?)</\s*\1\s*>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression attrRe(
        QStringLiteral(R"(\b(rowspan|colspan)\s*=\s*["']?(\d+)["']?)"),
        QRegularExpression::CaseInsensitiveOption);

    QVector<QVector<QString>> grid;
    grid.reserve(16);

    QRegularExpressionMatchIterator rows = rowRe.globalMatch(trimmed);
    int gridRow = 0;
    bool anyRows = false;

    while (rows.hasNext()) {
        const QRegularExpressionMatch row = rows.next();
        const QString rowBody = row.captured(1);
        anyRows = true;

        QRegularExpressionMatchIterator cells = cellRe.globalMatch(rowBody);
        int gridCol = 0;
        bool rowHasCells = false;

        while (cells.hasNext()) {
            const QRegularExpressionMatch cell = cells.next();
            const QString attrs = cell.captured(2);
            QString content = cell.captured(3);
            content = stripServiceTokens(content).trimmed();

            int rowspan = 1, colspan = 1;
            QRegularExpressionMatchIterator attrsIt = attrRe.globalMatch(attrs);
            while (attrsIt.hasNext()) {
                const QRegularExpressionMatch am = attrsIt.next();
                const int v = am.captured(2).toInt();
                if (am.captured(1) == QLatin1String("rowspan"))
                    rowspan = v;
                else
                    colspan = v;
            }
            rowspan = std::max(1, rowspan);
            colspan = std::max(1, colspan);

            while (gridRow < grid.size() && gridCol < grid.at(gridRow).size()
                   && !grid.at(gridRow).at(gridCol).isEmpty())
                ++gridCol;

            const QString escaped = escapeTableCell(content);
            for (int r = 0; r < rowspan; ++r) {
                if (gridRow + r >= grid.size())
                    grid.resize(gridRow + r + 1);
                if (grid.at(gridRow + r).size() <= gridCol + colspan - 1)
                    grid[gridRow + r].resize(gridCol + colspan);
                for (int c = 0; c < colspan; ++c)
                    grid[gridRow + r][gridCol + c] = escaped;
            }

            rowHasCells = true;
            ++gridCol;
        }

        if (rowHasCells)
            ++gridRow;
    }

    if (!anyRows || grid.isEmpty())
        return convertMath(trimmed);

    int cols = 0;
    for (const auto &row : std::as_const(grid))
        cols = std::max(cols, static_cast<int>(row.size()));

    QString out;
    auto writeRow = [&](const QVector<QString> &row) {
        QString line = QStringLiteral("|");
        for (int c = 0; c < cols; ++c) {
            const QString val = (c < row.size()) ? row.at(c) : QString();
            line += QLatin1Char(' ') + val + QStringLiteral(" |");
        }
        out += line + QLatin1Char('\n');
    };

    writeRow(grid.first());

    out += QLatin1Char('|');
    for (int c = 0; c < cols; ++c)
        out += QStringLiteral(" --- |");
    out += QLatin1Char('\n');

    for (int r = 1; r < grid.size(); ++r)
        writeRow(grid.at(r));

    return out.trimmed();
}

QString applyStyle(const QString &text, const BlockStyleInfo &info)
{
    switch (info.style) {
    case BlockStyle::ImagePlaceholder: {
        const QString alt = text.trimmed().isEmpty() ? QStringLiteral("Image") : text.trimmed();
        return QStringLiteral("![%1](image://ocr/crop/%2)").arg(alt).arg(info.imageIndex);
    }
    case BlockStyle::Italic:
        return QLatin1Char('*') + text + QLatin1Char('*');
    case BlockStyle::Equation:
        return formatEquation(text);
    case BlockStyle::Table:
        return formatTable(text);
    case BlockStyle::Heading: {
        const int level = info.headingLevel > 0 ? info.headingLevel : headingLevelFor(text);
        return QString(level, QLatin1Char('#')) + QLatin1Char(' ') + text;
    }
    case BlockStyle::PlainText:
    default:
        return convertMath(text);
    }
}

} // namespace

QString rebuildPageText(const OcrPage& page, bool keepPageNumbers)
{
    QStringList blocks;
    for (int i = 0; i < page.boxes.size(); ++i) {
        const BoundingBox& box = page.boxes.at(i);
        if (!keepPageNumbers && box.label == QLatin1String("page_number"))
            continue;
        BlockStyleInfo style = blockStyleForLabel(box.label);
        if (style.style == BlockStyle::ImagePlaceholder)
            style.imageIndex = i;
        if (box.text.isEmpty() && style.style != BlockStyle::ImagePlaceholder)
            continue;
        blocks << applyStyle(box.text, style);
    }
    return blocks.join(QStringLiteral("\n\n"));
}

OcrResult DetTokensParser::parse(const QString &rawText) const
{
    if (rawText.trimmed().isEmpty())
        return OcrResult::makeError(QStringLiteral("Empty OCR text"));

    OcrResult result;
    OcrPage page;
    QStringList blocks;

    struct Token {
        QString label;
        int x1, y1, x2, y2;
        int textStart;   // offset right after the closing ']'
        int tokenStart;  // offset of the label itself
    };
    QList<Token> tokens;

    const bool wrapped = rawText.contains(QStringLiteral("<|det|>"));
    const QRegularExpression &re = wrapped ? wrappedTokenRegex() : tokenStartRegex();
    QRegularExpressionMatchIterator it = re.globalMatch(rawText);

    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        Token t;
        t.label       = m.captured(1);
        t.x1          = m.captured(2).toInt();
        t.y1          = m.captured(3).toInt();
        t.x2          = m.captured(4).toInt();
        t.y2          = m.captured(5).toInt();
        t.textStart   = m.capturedEnd(0);
        t.tokenStart  = m.capturedStart(0);
        tokens.append(t);
    }

    if (tokens.isEmpty()) {
        page.text =
            stripServiceTokens(wrapped ? unescapeModelText(rawText) : rawText).trimmed();
        result.text = page.text;
        result.pages.append(page);
        result.success = true;
        return result;
    }

    result.success = true;

    {
        QString preamble = rawText.left(tokens.first().tokenStart);
        if (wrapped)   // decode JSON escapes only for the model's wrapped stream
            preamble = unescapeModelText(preamble);
        preamble = stripServiceTokens(preamble).trimmed();
        if (!preamble.isEmpty()) {
            BoundingBox untagged;
            untagged.label = QStringLiteral("text");
            untagged.text  = preamble;
            page.boxes.append(untagged);
            blocks << convertMath(preamble);
        }
    }

    const double range = kBboxCoordinateRange;

    constexpr double kDedupTolerance = 10.0 / kBboxCoordinateRange;

    struct RawCoords { int x1, y1, x2, y2; };
    QList<RawCoords> rawCoords;

    for (int i = 0; i < tokens.size(); ++i) {
        const Token &t = tokens.at(i);
        if (!m_keepPageNumbers && t.label == QLatin1String("page_number"))
            continue;
        const int spanEnd = (i + 1 < tokens.size())
                                ? tokens.at(i + 1).tokenStart
                                : rawText.size();
        QString boxText = rawText.mid(t.textStart, spanEnd - t.textStart);
        if (wrapped)
            boxText = unescapeModelText(boxText);
        boxText = stripServiceTokens(boxText).trimmed();

        const double nx1 = (std::min)(t.x1, t.x2) / range;
        const double ny1 = (std::min)(t.y1, t.y2) / range;
        const double nx2 = (std::max)(t.x1, t.x2) / range;
        const double ny2 = (std::max)(t.y1, t.y2) / range;

        BoundingBox box;
        box.label = t.label;
        box.text  = boxText;
        box.rect  = QRectF(nx1, ny1, nx2 - nx1, ny2 - ny1);

        int dupIndex = -1;
        for (int j = 0; j < rawCoords.size(); ++j) {
            const RawCoords &rc = rawCoords.at(j);
            if (qAbs(rc.x1 - t.x1) <= 10 && qAbs(rc.y1 - t.y1) <= 10
                && qAbs(rc.x2 - t.x2) <= 10 && qAbs(rc.y2 - t.y2) <= 10) {
                dupIndex = j;
                break;
            }
        }

        if (dupIndex >= 0) {
            page.hasDuplicates = true;

            page.boxes[dupIndex] = box;
            rawCoords[dupIndex] = { t.x1, t.y1, t.x2, t.y2 };

            BlockStyleInfo style = blockStyleForLabel(t.label);
            if (style.style == BlockStyle::ImagePlaceholder)
                style.imageIndex = dupIndex;
            if (!boxText.isEmpty() || style.style == BlockStyle::ImagePlaceholder)
                blocks[dupIndex] = applyStyle(boxText, style);
            continue;
        }

        const int boxIndex = page.boxes.size();
        page.boxes.append(box);
        rawCoords.append({ t.x1, t.y1, t.x2, t.y2 });

        BlockStyleInfo style = blockStyleForLabel(t.label);
        if (style.style == BlockStyle::ImagePlaceholder) {
            style.imageIndex = boxIndex;
            blocks << applyStyle(boxText, style);
            continue;
        }
        if (boxText.isEmpty())
            continue;
        blocks << applyStyle(boxText, style);
    }

    page.text = blocks.join(QStringLiteral("\n\n"));
    result.text = page.text;
    result.pages.append(page);
    return result;
}

QString DetTokensParser::id() const
{
    return QStringLiteral("det_tokens");
}

} // namespace llocr