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
        if (c == backslash && i + 1 < text.size()
            && text.at(i + 1) == QLatin1Char('n')) {
            out.append(QLatin1Char('\n'));
            ++i;
            continue;
        }
        out.append(c);
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

QString formatTable(const QString &text, bool tablesAsHtml)
{
    const QString trimmed = text.trimmed();
    if (!trimmed.contains(QStringLiteral("<table")))
        return convertMath(trimmed);

    if (tablesAsHtml)
        return trimmed;

    static const QRegularExpression rowRe(
        QStringLiteral(R"(<tr\b[^>]*>([\s\S]*?)</\s*tr\s*>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression cellRe(
        QStringLiteral(R"(<(td|th)\b([^>]*)>([\s\S]*?)</\s*\1\s*>)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression attrRe(
        QStringLiteral(R"(\b(rowspan|colspan)\s*=\s*["']?(\d+)["']?)"),
        QRegularExpression::CaseInsensitiveOption);

    struct Cell {
        QString content;   // already escaped for a pipe-table cell
        int rowspan = 1;
        int colspan = 1;
    };

    QVector<QVector<Cell>> rows;
    QRegularExpressionMatchIterator rowIt = rowRe.globalMatch(trimmed);
    while (rowIt.hasNext()) {
        const QRegularExpressionMatch row = rowIt.next();
        QVector<Cell> cells;

        QRegularExpressionMatchIterator cellIt = cellRe.globalMatch(row.captured(1));
        while (cellIt.hasNext()) {
            const QRegularExpressionMatch cm = cellIt.next();
            Cell cell;
            const QString content = stripServiceTokens(cm.captured(3)).trimmed();

            QRegularExpressionMatchIterator attrsIt = attrRe.globalMatch(cm.captured(2));
            while (attrsIt.hasNext()) {
                const QRegularExpressionMatch am = attrsIt.next();
                const int v = std::max(1, am.captured(2).toInt());
                if (am.captured(1) == QLatin1String("rowspan"))
                    cell.rowspan = v;
                else
                    cell.colspan = v;
            }
            cell.content = escapeTableCell(content);
            cells.append(cell);
        }

        if (!cells.isEmpty())
            rows.append(cells);
    }

    if (rows.isEmpty())
        return convertMath(trimmed);

    int cols = 1;
    for (const QVector<Cell> &row : std::as_const(rows)) {
        int width = 0;
        for (const Cell &cell : row)
            width += cell.colspan;
        cols = std::max(cols, width);
    }

    QVector<QVector<QString>> grid;
    QVector<QVector<bool>> occupied;

    auto ensureCell = [&](int r, int c) {
        if (r >= grid.size()) {
            grid.resize(r + 1);
            occupied.resize(r + 1);
        }
        if (c >= grid.at(r).size()) {
            grid[r].resize(c + 1);
            occupied[r].resize(c + 1);
        }
    };

    for (int r = 0; r < rows.size(); ++r) {
        ensureCell(r, cols - 1);

        const QVector<Cell> &row = rows.at(r);
        const bool sectionHeader = row.size() == 1 && row.first().colspan > 1;

        int col = 0;
        for (const Cell &cell : row) {
            int colspan = cell.colspan;
            if (sectionHeader) {
                col = 0;
                colspan = cols;
            } else {
                while (col < cols && occupied.at(r).at(col))
                    ++col;
                colspan = std::min(colspan, cols - col);
            }
            if (colspan < 1)
                break;

            for (int dr = 0; dr < cell.rowspan; ++dr) {
                ensureCell(r + dr, col + colspan - 1);
                for (int dc = 0; dc < colspan; ++dc) {
                    const int rr = r + dr;
                    const int cc = col + dc;
                    if (occupied.at(rr).at(cc) && !sectionHeader)
                        continue;
                    occupied[rr][cc] = true;
                    grid[rr][cc] = (dr == 0 && dc == 0) ? cell.content : QString();
                }
            }
            col += colspan;
        }
    }

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

QString applyStyle(const QString &text, const BlockStyleInfo &info, bool tablesAsHtml)
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
        return formatTable(text, tablesAsHtml);
    case BlockStyle::Heading: {
        const int level = info.headingLevel > 0 ? info.headingLevel : headingLevelFor(text);
        return QString(level, QLatin1Char('#')) + QLatin1Char(' ') + text;
    }
    case BlockStyle::PlainText:
        return convertMath(text);
    }
}

} // namespace

QString rebuildPageText(const OcrPage& page, bool keepPageNumbers, bool tablesAsHtml)
{
    QStringList blocks;
    for (int i = 0; i < page.boxes.size(); ++i) {
        const BoundingBox& box = page.boxes.at(i);
        if (!keepPageNumbers && box.label == QLatin1String("page_number"))
            continue;
        BlockStyleInfo style = blockStyleForLabel(box.label);
        if (style.style == BlockStyle::ImagePlaceholder)
            style.imageIndex = i;
        // A verified FIX replaces the recognized text in the output; if the
        // block has no correction the recognized text is used as-is.
        const QString text = box.correctedText.isEmpty() ? box.text : box.correctedText;
        if (text.isEmpty() && style.style != BlockStyle::ImagePlaceholder)
            continue;
        blocks << applyStyle(text, style, tablesAsHtml);
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
        if (wrapped)   // decode the model's \n line separator (nothing else)
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
                blocks[dupIndex] = applyStyle(boxText, style, m_tablesAsHtml);
            continue;
        }

        const int boxIndex = page.boxes.size();
        page.boxes.append(box);
        rawCoords.append({ t.x1, t.y1, t.x2, t.y2 });

        BlockStyleInfo style = blockStyleForLabel(t.label);
        if (style.style == BlockStyle::ImagePlaceholder) {
            style.imageIndex = boxIndex;
            blocks << applyStyle(boxText, style, m_tablesAsHtml);
            continue;
        }
        if (boxText.isEmpty())
            continue;
        blocks << applyStyle(boxText, style, m_tablesAsHtml);
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