#include "parsers/DetTokensParser.h"
#include "parsers/BlockStyle.h"
#include "parsers/DetTokenFormat.h"
#include "parsers/Lfm25VlDrift.h"
#include "parsers/OtslTable.h"

#include "core/ServiceMarkers.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

#include <algorithm>

namespace llocr {

namespace {

// A reply shorter than this that yields no tokens is a model answering
// something short (e.g. "OK") — not a parse failure, so no diagnostic.
constexpr int kDiagnosticMinLength = 32;

// Duplicate-region tolerance: 1 % of the page in normalized coordinates (the
// same 10 units in the usual 0–1000 space), so a model emitting another scale
// is handled too.
constexpr double kDuplicateTolerance = 0.01;

// <|det|>label [x1, y1, x2, y2]<|/det|>
const QRegularExpression &wrappedTokenRegex()
{
    static const QRegularExpression re(QStringLiteral(R"(<\|det\|>([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\]<\|/det\|>)"));
    return re;
}

// Bare token: [image_index=<n>] label [x1, y1, x2, y2]  (no wrappers).
// The optional "image_index=<n>" header prefix is emitted by LFM2.5-VL
// (layout annotation format); Unlimited-OCR emits the bare token only.
// The layout annotation is experimental (model card) and the model
// sometimes drifts into an XML-ish shape — "image_index=<n> <label>name</label>",
// with the bbox optional — which the second (?|...) branch matches too
// (branch reset keeps the capture numbers aligned; absent bbox coordinates
// come out as null captures and yield a zero rect).
const QRegularExpression &tokenStartRegex()
{
    static const QRegularExpression re(QStringLiteral("(?|"
                                                      "(?:image_index=\\d+\\s+)?([A-Za-z_][A-Za-z0-9_]*)\\s*\\[\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\]"
                                                      "|"
                                                      "image_index=\\d+\\s+<label>([A-Za-z_][A-Za-z0-9_]*)</label>(?:\\s*\\[\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*\\])?"
                                                      ")"));
    return re;
}

QString formatTable(const QString &text, bool tablesAsHtml)
{
    const QString trimmed = text.trimmed();
    // OTSL first: a drifted LFM2.5-VL table can carry a stray </table>
    // wrapper, and real HTML tables never contain fcel tokens.
    if (containsOtslTable(trimmed))
        return formatOtslTable(trimmed, tablesAsHtml);
    if (!trimmed.contains(QStringLiteral("<table")))
        return convertMath(trimmed);

    if (tablesAsHtml)
        return trimmed;

    static const QRegularExpression rowRe(QStringLiteral(R"(<tr\b[^>]*>([\s\S]*?)</\s*tr\s*>)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression cellRe(QStringLiteral(R"(<(td|th)\b([^>]*)>([\s\S]*?)</\s*\1\s*>)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression attrRe(QStringLiteral(R"(\b(rowspan|colspan)\s*=\s*["']?(\d+)["']?)"), QRegularExpression::CaseInsensitiveOption);

    struct Cell {
        QString content;  // already escaped for a pipe-table cell
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

QString applyStyle(const QString &text, const BlockStyleInfo &info, const ParserOptions &options)
{
    // LFM2.5-VL can leak OTSL rows into non-table blocks (a formula under a
    // text/equation token, a table fragment after it): convert them wherever
    // the tags appear, not only in Table blocks.
    if (info.style != BlockStyle::ImagePlaceholder && !text.contains(QStringLiteral("<table")) && containsOtslTable(text)) {
        return formatOtslTable(text, options.tablesAsHtml);
    }

    switch (info.style) {
    case BlockStyle::ImagePlaceholder: {
        // The model's figure text (e.g. OCR-ed figure labels) is not a useful
        // alt text — keep only the first meaningful line.
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
        // Captions carry math too (\(n\) etc.) — convert like plain text.
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

QString DetTokensParser::rebuildText(const OcrPage &page) const
{
    QStringList blocks;
    for (int i = 0; i < page.boxes.size(); ++i) {
        const BoundingBox &box = page.boxes.at(i);
        if (!m_options.keepPageNumbers && box.label == QLatin1String("page_number"))
            continue;
        BlockStyleInfo style = blockStyleForLabel(box.label, m_options.modelId);
        if (style.style == BlockStyle::ImagePlaceholder)
            style.imageIndex = i;
        // A verified FIX replaces the recognized text in the output; if the
        // block has no correction the recognized text is used as-is.
        const QString text = box.correctedText.isEmpty() ? box.text : box.correctedText;
        if (text.isEmpty() && style.style != BlockStyle::ImagePlaceholder)
            continue;
        blocks << applyStyle(text, style, m_options);
    }
    return blocks.join(QStringLiteral("\n\n"));
}

OcrResult DetTokensParser::parse(const QString &rawText) const
{
    if (rawText.trimmed().isEmpty())
        return OcrResult::makeError(QStringLiteral("Empty OCR text"));

    OcrResult result;
    OcrPage page;

    struct Token {
        QString label;
        int x1, y1, x2, y2;
        bool hasBbox;    // false for the XML-drift token without coordinates
        int textStart;   // offset right after the closing ']'
        int tokenStart;  // offset of the label itself
    };
    QList<Token> tokens;

    const bool wrapped = rawText.contains(QStringLiteral("<|det|>"));
    const QString text = wrapped ? rawText : normalizeDriftRegions(rawText);
    const QRegularExpression &re = wrapped ? wrappedTokenRegex() : tokenStartRegex();
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
        page.text = stripServiceTokens(wrapped ? unescapeModelText(rawText) : text).trimmed();
        result.text = page.text;
        result.pages.append(page);
        result.success = true;
        // A det-token reply always carries at least one header, so an empty
        // match means the model/parser pair is wrong (or the model drifted into
        // an unsupported shape). The page is still usable as plain text, but the
        // user gets told why the overlay is empty.
        if (page.text.length() > kDiagnosticMinLength)
            result.notes.append(QCoreApplication::translate("DetTokensParser",
                                                            "No layout tokens found in the model reply — the text was kept as "
                                                            "one block. Check that the OCR model and the output parser match."));
        return result;
    }

    result.success = true;

    {
        QString preamble = text.left(tokens.first().tokenStart);
        if (wrapped)  // decode the model's \n line separator (nothing else)
            preamble = unescapeModelText(preamble);
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
        QString boxText = text.mid(t.textStart, spanEnd - t.textStart);
        if (wrapped)
            boxText = unescapeModelText(boxText);
        boxText = stripServiceTokens(boxText).trimmed();

        const double nx1 = (std::min)(t.x1, t.x2) / range;
        const double ny1 = (std::min)(t.y1, t.y2) / range;
        const double nx2 = (std::max)(t.x1, t.x2) / range;
        const double ny2 = (std::max)(t.y1, t.y2) / range;

        BoundingBox box;
        box.label = t.label;
        box.text = boxText;
        box.rect = QRectF(nx1, ny1, nx2 - nx1, ny2 - ny1);
        box.positioned = t.hasBbox;

        // Duplicate-region replacement: the model emitted the same region twice
        // (a page repeated on facing pages), so the later, better-attributed
        // block replaces the earlier one. The search runs over page.boxes — the
        // single list of fragments — and skips unpositioned ones, which all
        // share the zero rect and must never match each other or a real region
        // near the page origin.
        int dupIndex = -1;
        if (t.hasBbox) {
            for (int j = 0; j < page.boxes.size(); ++j) {
                const QRectF &other = page.boxes.at(j).rect;
                if (!page.boxes.at(j).positioned)
                    continue;
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

    // The page text is rendered from the boxes by the same code rebuildText()
    // uses, so the overlay, the Markdown and the crop indices cannot drift apart.
    page.text = rebuildText(page);
    result.text = page.text;
    result.pages.append(page);
    return result;
}

QString DetTokensParser::id() const
{
    return QStringLiteral("det_tokens");
}

QString DetTokensParser::displayName() const
{
    return QCoreApplication::translate("DetTokensParser", "Layout tokens (with boxes)");
}

}  // namespace llocr
