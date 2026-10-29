#include "parsers/BlockRender.h"
#include "parsers/DetTokenFormat.h"
#include "parsers/OtslTable.h"

#include "core/BoxTransitions.h"
#include "core/ServiceMarkers.h"
#include "parsers/HtmlMarkup.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace llocr {

namespace {

// A hallucinating model can repeat span attributes with huge values; the grid
// is bounded so one reply cannot drive the formatter into a multi-GB
// allocation or a GUI hang.
constexpr int kMaxTableColumns = 512;

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
            // The cell of a table that came as HTML carries its entities.
            cell.content = stripServiceTokens(decodeHtmlEntities(cm.captured(3))).trimmed();

            QRegularExpressionMatchIterator attrsIt = attrRe.globalMatch(cm.captured(2));
            while (attrsIt.hasNext()) {
                const QRegularExpressionMatch am = attrsIt.next();
                const int v = std::clamp(am.captured(2).toInt(), 1, kMaxTableColumns);
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
        cols = (std::max)(cols, width);
    }
    cols = (std::min)(cols, kMaxTableColumns);

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
                colspan = (std::min)(colspan, cols - col);
            }
            if (colspan < 1)
                break;

            // A span cannot run past the table the reply actually declares.
            const int rowspan = (std::min)(cell.rowspan, static_cast<int>(rows.size()));
            for (int dr = 0; dr < rowspan; ++dr) {
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

}  // namespace

QString renderBlockText(const QString &text, const BlockStyleInfo &info, const ParserOptions &options)
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
    default:
        return convertMath(text);
    }
}

IOutputParser::RebuiltPageText renderPageText(const OcrPage &page, const ParserOptions &options)
{
    QStringList blocks;
    QList<BlockTextRange> ranges;
    int offset = 0;
    for (int i = 0; i < page.boxes.size(); ++i) {
        const BoundingBox &box = page.boxes.at(i);
        if (!options.keepPageNumbers && box.label == QLatin1String("page_number"))
            continue;
        BlockStyleInfo style = blockStyleForLabel(box.label, options.modelId);
        if (style.style == BlockStyle::ImagePlaceholder)
            style.imageIndex = i;
        const QString text = BoxTransitions::effectiveText(box);
        if (text.isEmpty() && style.style != BlockStyle::ImagePlaceholder)
            continue;
        const QString rendered = renderBlockText(text, style, options);
        ranges.append({i, offset, static_cast<int>(rendered.size())});
        blocks << rendered;
        offset += rendered.size() + 2;  // blocks.join("\n\n") separator
    }
    return {blocks.join(QStringLiteral("\n\n")), ranges};
}

}  // namespace llocr
