#include "parsers/DetTokenParserBase.h"
#include "parsers/BlockStyle.h"
#include "parsers/DetTokenFormat.h"
#include "parsers/OtslTable.h"

#include "core/ServiceMarkers.h"

#include <QCoreApplication>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace llocr {

namespace {

constexpr int kDiagnosticMinLength = 32;

constexpr double kDuplicateTolerance = 0.01;

QString formatTable(const QString &text, bool tablesAsHtml)
{
    const QString trimmed = text.trimmed();
    if (containsOtslTable(trimmed))
        return formatOtslTable(trimmed, tablesAsHtml);
    if (!trimmed.contains(QStringLiteral("<table")))
        return convertMath(trimmed);

    if (tablesAsHtml)
        return trimmed;

    static const QRegularExpression rowRe(QStringLiteral(R"(<tr\b[^>]*>([\s\S]*?)</\s*tr\s*>)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression cellRe(QStringLiteral(R"(<(td|th)\b([^>]*)>([\s\S]*?)</\1\s*>)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression attrRe(QStringLiteral(R"(\b(rowspan|colspan)\s*=\s*["']?(\d+)["']?)"), QRegularExpression::CaseInsensitiveOption);

    struct Cell {
        QString content;
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
            cell.content = stripServiceTokens(cm.captured(3)).trimmed();

            QRegularExpressionMatchIterator attrsIt = attrRe.globalMatch(cm.captured(2));
            while (attrsIt.hasNext()) {
                const QRegularExpressionMatch am = attrsIt.next();
                const int v = std::max(1, am.captured(2).toInt());
                if (am.captured(1) == QLatin1String("rowspan"))
                    cell.rowspan = v;
                else
                    cell.colspan = v;
            }
            cell.content = escapeTableCell(cell.content);
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
        for (const Cell &c : std::as_const(row))
            width += c.colspan;
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
                    if (occupied.at(r + dr).at(col + dc) && !sectionHeader)
                        continue;
                    occupied[r + dr][col + dc] = true;
                    grid[r + dr][col + dc] = (dr == 0 && dc == 0) ? cell.content : QString();
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
        writeRow(grid[r]);

    return out.trimmed();
}

QString applyStyle(const QString &text, const BlockStyleInfo &info, const ParserOptions &options)
{
    if (info.style != BlockStyle::ImagePlaceholder && !text.contains(QStringLiteral("<table")) && containsOtslTable(text)) {
        return formatOtslTable(text, options.tablesAsHtml);
    }

    switch (info.style) {
    case BlockStyle::ImagePlaceholder: {
        QString alt = text.section(QLatin1Char('\n'), 0, 0);
        while (alt.startsWith(QLatin1Char('!')) || alt.startsWith(QLatin1Char('*')) || alt.startsWith(QLatin1Char('-')) || alt.startsWith(QLatin1Char(' ')))
            alt = alt.mid(1).trimmed();
        for (const QChar ch : {QLatin1Char('['), QLatin1Char(']'), QLatin1Char('('), QLatin1Char(')'), QLatin1Char('!')})
            alt.remove(ch);
        alt = alt.trimmed();
        if (alt.size() > 60)
            alt = alt.left(60).trimmed();
        if (alt.isEmpty())
            alt = QStringLiteral("Image");
        return QStringLiteral("![%1](image://ocr/crop/%2)").arg(alt).arg(info.imageIndex);
    }
    case BlockStyle::Italic:
        return QLatin1Char('*') + convertMath(text) + QLatin1Char('*');
    case BlockStyle::Equation:
        return formatEquation(text);
    case BlockStyle::Table:
        return formatTable(text, options.tablesAsHtml);
    case BlockStyle::Heading: {
        const int level = info.headingLevel > 0 ? info.headingLevel : headingLevelFor(text);
        return QString(level, QLatin1Char('#')) + QLatin1Char(' ') + text;
    }
    case BlockStyle::PlainText:
        return convertMath(text);
    }
}

}  // namespace

QString DetTokenParserBase::prepareText(const QString &rawText) const
{
    return rawText;
}

QString DetTokenParserBase::rebuildText(const OcrPage &page) const
{
    QStringList blocks;
    for (int i = 0; i < page.boxes.size(); ++i) {
        const BoundingBox &box = page.boxes.at(i);
        if (!m_options.keepPageNumbers && box.label == QLatin1String("page_number"))
            continue;
        BlockStyleInfo style = blockStyleForLabel(box.label, m_options.modelId);
        if (style.style == BlockStyle::ImagePlaceholder)
            style.imageIndex = i;
        const QString text = box.correctedText.isEmpty() ? box.text : box.correctedText;
        if (text.isEmpty() && style.style != BlockStyle::ImagePlaceholder)
            continue;
        blocks << applyStyle(text, style, m_options);
    }
    return blocks.join(QStringLiteral("\n\n"));
}

OcrResult DetTokenParserBase::parse(const QString &rawText) const
{
    if (rawText.trimmed().isEmpty())
        return OcrResult::makeError(QStringLiteral("Empty OCR text"));

    OcrResult result;
    OcrPage page;

    struct Token {
        QString label;
        int x1, y1, x2, y2;
        bool hasBbox;    // false for a header written without coordinates
        int textStart;   // offset right after the closing ']'
        int tokenStart;  // offset of the label itself
    };
    QList<Token> tokens;

    const QString text = prepareText(rawText);
    const QRegularExpression &re = tokenRegex(text);
    const bool unescape = escapesLineBreaks(text);
    const auto decode = [unescape](const QString &part) { return unescape ? unescapeModelText(part) : part; };
    QRegularExpressionMatchIterator it = re.globalMatch(text);

    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        Token t;
        t.label = m.captured(1);
        t.x1 = m.captured(2).toInt();
        t.y1 = m.captured(3).toInt();
        t.x2 = m.captured(4).toInt();
        t.y2 = m.captured(5).toInt();
        t.hasBbox = !m.captured(2).isNull();
        t.textStart = m.capturedEnd(0);
        t.tokenStart = m.capturedStart(0);
        tokens.append(t);
    }

    if (tokens.isEmpty()) {
        page.text = stripServiceTokens(decode(text)).trimmed();
        result.text = page.text;
        result.pages.append(page);
        result.success = true;
        if (page.text.length() > kDiagnosticMinLength)
            result.notes.append(QCoreApplication::translate("DetTokenParser",
                                                            "No layout tokens found in the model reply — the text was kept as "
                                                            "one block. Check that the OCR model and the output parser match."));
        return result;
    }

    result.success = true;

    {
        QString preamble = decode(text.left(tokens.first().tokenStart));
        preamble = stripServiceTokens(preamble).trimmed();
        if (!preamble.isEmpty()) {
            BoundingBox untagged;
            untagged.label = QStringLiteral("text");
            untagged.text = preamble;
            untagged.positioned = false;
            page.boxes.append(untagged);
        }
    }

    const double range = m_options.bboxRange > 0 ? m_options.bboxRange : 1000;

    for (int i = 0; i < tokens.size(); ++i) {
        const Token &t = tokens.at(i);
        if (!m_options.keepPageNumbers && t.label == QLatin1String("page_number"))
            continue;
        const int spanEnd = (i + 1 < tokens.size()) ? tokens.at(i + 1).tokenStart : text.size();
        QString boxText = stripServiceTokens(decode(text.mid(t.textStart, spanEnd - t.textStart))).trimmed();

        const double nx1 = (std::min)(t.x1, t.x2) / range;
        const double ny1 = (std::min)(t.y1, t.y2) / range;
        const double nx2 = (std::max)(t.x1, t.x2) / range;
        const double ny2 = (std::max)(t.y1, t.y2) / range;

        BoundingBox box;
        box.label = t.label;
        box.text = boxText;
        box.rect = QRectF(nx1, ny1, nx2 - nx1, ny2 - ny1);
        box.positioned = t.hasBbox;

        int dupIndex = -1;
        if (t.hasBbox) {
            for (int j = 0; j < page.boxes.size(); ++j) {
                if (!page.boxes.at(j).positioned)
                    continue;
                const QRectF &other = page.boxes.at(j).rect;
                if (qAbs(other.x() - nx1) <= kDuplicateTolerance && qAbs(other.y() - ny1) <= kDuplicateTolerance && qAbs(other.width() - (nx2 - nx1)) <= kDuplicateTolerance &&
                    qAbs(other.height() - (ny2 - ny1)) <= kDuplicateTolerance) {
                    dupIndex = j;
                    break;
                }
            }
        }

        if (dupIndex >= 0) {
            page.hasDuplicates = true;
            page.boxes[dupIndex] = box;
            continue;
        }

        page.boxes.append(box);
    }

    page.text = rebuildText(page);
    result.text = page.text;
    result.pages.append(page);
    return result;
}

}  // namespace llocr
