#include "parsers/OtslTable.h"

#include "core/ServiceMarkers.h"
#include "parsers/DetTokenFormat.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace llocr {

bool containsOtslTable(const QString &text)
{
    static const QRegularExpression re(QStringLiteral(R"(<\s*/?(?:fcel|lcel|ucel|xcel|nl)\s*>)"), QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

namespace {

struct OtslHtmlCell {
    QString text;  // unescaped cell content
    int colspan = 1;
    int rowspan = 1;
};

// How the cell slot being closed relates to its neighbours' spans.
enum class OtslCellKind { Content, CoveredLeft, CoveredUp, CoveredBoth };

}  // namespace

QString formatOtslTable(QString text, bool tablesAsHtml)
{
    static const QRegularExpression tokenRe(QStringLiteral(R"(<\s*/?(fcel|lcel|ucel|xcel|nl)\s*>)"), QRegularExpression::CaseInsensitiveOption);

    // LFM2.5-VL drift: cells may be separated by CLOSING </fcel> tags and rows
    // by plain newlines instead of <nl>; a stray <table>/</table> wrapper can
    // wrap everything. Normalize before tokenizing.
    QString work = text;
    const bool hadRowBreaks = work.contains(QStringLiteral("<nl"));
    static const QRegularExpression rowEndCloseRe(QStringLiteral(R"(</fcel>[ \t]*(?=\n|$))"));
    work.replace(rowEndCloseRe, QStringLiteral("<nl>"));
    work.replace(QStringLiteral("</fcel>"), QStringLiteral("<fcel>"));
    work.remove(QStringLiteral("<table>"));
    work.remove(QStringLiteral("</table>"));
    if (!hadRowBreaks)
        work.replace(QLatin1Char('\n'), QStringLiteral("<nl>"));
    text = work;

    QVector<QStringList> rows;                // GFM output: one entry per column slot
    QVector<QVector<OtslHtmlCell>> htmlRows;  // HTML output: explicit cells with spans
    QStringList row;
    QVector<OtslHtmlCell> htmlRow;
    QString pending;  // content accumulated for the current cell
    bool haveCell = false;
    OtslCellKind kind = OtslCellKind::Content;
    int pos = 0;

    auto flushCell = [&]() {
        if (!haveCell && !pending.trimmed().isEmpty()) {
            // Content between <nl> tokens with no opening <fcel> (a leaked
            // formula fragment uses bare <nl> separators): treat it as a
            // cell of its own.
            haveCell = true;
            kind = OtslCellKind::Content;
        }
        if (!haveCell) {
            pending.clear();
            return;
        }
        if (kind == OtslCellKind::Content) {
            row.append(escapeTableCell(stripServiceTokens(pending)));
            OtslHtmlCell cell;
            cell.text = convertMath(stripServiceTokens(pending));
            htmlRow.append(cell);
        } else {
            row.append(QString());
            int col = 0;
            for (const OtslHtmlCell &c : std::as_const(htmlRow))
                col += c.colspan;
            bool coveredHandled = false;
            if (kind != OtslCellKind::CoveredLeft && !htmlRows.isEmpty()) {
                const QVector<OtslHtmlCell> &above = htmlRows.last();
                int start = 0;
                for (int i = 0; i < above.size(); ++i) {
                    if (col >= start && col < start + above.at(i).colspan) {
                        ++htmlRows.last()[i].rowspan;
                        coveredHandled = true;
                        break;
                    }
                    start += above.at(i).colspan;
                }
            }
            if (!coveredHandled) {
                if (!htmlRow.isEmpty())
                    ++htmlRow.last().colspan;
                else
                    htmlRow.append(OtslHtmlCell{QString(), 1, 1});
            }
        }
        pending.clear();
    };

    QRegularExpressionMatchIterator it = tokenRe.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        pending += text.mid(pos, m.capturedStart() - pos);
        pos = m.capturedEnd();

        const QString token = m.captured(1).toLower();
        if (token == QLatin1String("nl")) {
            flushCell();
            haveCell = false;
            if (!row.isEmpty()) {
                rows.append(row);
                htmlRows.append(htmlRow);
            }
            row.clear();
            htmlRow.clear();
        } else {
            flushCell();
            haveCell = true;
            if (token == QLatin1String("fcel"))
                kind = OtslCellKind::Content;
            else if (token == QLatin1String("lcel"))
                kind = OtslCellKind::CoveredLeft;
            else if (token == QLatin1String("ucel"))
                kind = OtslCellKind::CoveredUp;
            else
                kind = OtslCellKind::CoveredBoth;
        }
    }
    flushCell();
    if (!row.isEmpty()) {
        rows.append(row);
        htmlRows.append(htmlRow);
    }

    if (rows.isEmpty())
        return convertMath(work);

    int cols = 1;
    for (const QStringList &r : std::as_const(rows))
        cols = std::max(cols, static_cast<int>(r.size()));

    if (cols == 1) {
        QStringList lines;
        for (const QStringList &r : std::as_const(rows))
            if (!r.isEmpty())
                lines << r.first();
        return lines.join(QLatin1Char('\n'));
    }

    if (tablesAsHtml) {
        QString out = QStringLiteral("<table>\n<tbody>\n");
        for (int r = 0; r < htmlRows.size(); ++r) {
            out += QStringLiteral("<tr>");
            const QString tag = (r == 0) ? QStringLiteral("th") : QStringLiteral("td");
            for (const OtslHtmlCell &cell : std::as_const(htmlRows[r])) {
                out += QLatin1Char('<') + tag;
                if (cell.colspan > 1)
                    out += QStringLiteral(" colspan=\"%1\"").arg(cell.colspan);
                if (cell.rowspan > 1)
                    out += QStringLiteral(" rowspan=\"%1\"").arg(cell.rowspan);
                out += QLatin1Char('>') + cell.text.simplified().toHtmlEscaped() + QStringLiteral("</") + tag + QLatin1Char('>');
            }
            out += QStringLiteral("</tr>\n");
        }
        out += QStringLiteral("</tbody>\n</table>");
        return out;
    }

    QString out;
    auto writeRow = [&](const QStringList &r) {
        QString line = QStringLiteral("|");
        for (int c = 0; c < cols; ++c) {
            const QString val = (c < r.size()) ? r.at(c) : QString();
            line += QLatin1Char(' ') + val + QStringLiteral(" |");
        }
        out += line + QLatin1Char('\n');
    };

    writeRow(rows.first());
    out += QLatin1Char('|');
    for (int c = 0; c < cols; ++c)
        out += QStringLiteral(" --- |");
    out += QLatin1Char('\n');
    for (int r = 1; r < rows.size(); ++r)
        writeRow(rows.at(r));

    return out.trimmed();
}

}  // namespace llocr
