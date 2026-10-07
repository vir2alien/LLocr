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

constexpr int kMinRepeatChars = 16;
constexpr int kMinRepeatWords = 8;
constexpr double kRepeatCoverage = 0.6;
constexpr int kMinTailChars = 8;

struct BlockFingerprint {
    QString simplified;
    QStringList words;
};

QStringList wordsForDuplicateCheck(const QString &simplified)
{
    QStringList words;
    QString current;
    for (const QChar ch : simplified) {
        if (ch.isLetterOrNumber()) {
            current += ch.toLower();
        } else if (!current.isEmpty()) {
            words.append(current);
            current.clear();
        }
    }
    if (!current.isEmpty())
        words.append(current);
    return words;
}

BlockFingerprint fingerprintForDuplicateCheck(const QString &text)
{
    BlockFingerprint fingerprint;
    fingerprint.simplified = text.simplified();
    fingerprint.words = wordsForDuplicateCheck(fingerprint.simplified);
    return fingerprint;
}

int longestCommonWordRun(const QStringList &a, const QStringList &b)
{
    QVector<int> previous(b.size() + 1, 0);
    QVector<int> current(b.size() + 1, 0);
    int best = 0;
    for (int i = 1; i <= a.size(); ++i) {
        for (int j = 1; j <= b.size(); ++j) {
            current[j] = (a.at(i - 1) == b.at(j - 1)) ? previous.at(j - 1) + 1 : 0;
            best = std::max(best, current.at(j));
        }
        std::swap(previous, current);
        current.fill(0);
    }
    return best;
}

bool repeatsEarlierText(const BlockFingerprint &candidate, const BlockFingerprint &previous)
{
    if (candidate.simplified.size() < kMinRepeatChars || previous.simplified.size() < kMinRepeatChars)
        return false;
    if (candidate.simplified == previous.simplified)
        return true;
    if (candidate.words.size() < kMinRepeatWords || previous.words.size() < kMinRepeatWords)
        return false;
    const int run = longestCommonWordRun(candidate.words, previous.words);
    if (run < kMinRepeatWords)
        return false;
    const int shorter = std::min(candidate.words.size(), previous.words.size());
    return run >= kRepeatCoverage * shorter;
}

int charOffsetAfterWords(const QString &simplified, int wordCount)
{
    int words = 0;
    int i = 0;
    while (i < simplified.size() && words < wordCount) {
        if (simplified.at(i).isLetterOrNumber()) {
            while (i < simplified.size() && simplified.at(i).isLetterOrNumber())
                ++i;
            ++words;
        } else {
            ++i;
        }
    }
    return i;
}

int keptPrefixWords(const BlockFingerprint &candidate, const BlockFingerprint &previous)
{
    if (candidate.words.size() < kMinRepeatWords || previous.words.isEmpty())
        return 0;
    const QString haystack = QLatin1Char(' ') + previous.words.join(QLatin1Char(' ')) + QLatin1Char(' ');
    for (int k = candidate.words.size(); k >= kMinRepeatWords; --k) {
        const QString needle = QLatin1Char(' ') + QStringList(candidate.words.constBegin(), candidate.words.constBegin() + k).join(QLatin1Char(' ')) + QLatin1Char(' ');
        if (haystack.contains(needle))
            return k;
    }
    return 0;
}

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

QRectF DetTokenParserBase::calibrateRect(const QRectF &rect) const
{
    return rect;
}

QString DetTokenParserBase::rebuildText(const OcrPage &page) const
{
    return rebuildTextWithRanges(page).text;
}

IOutputParser::RebuiltPageText DetTokenParserBase::rebuildTextWithRanges(const OcrPage &page) const
{
    QStringList blocks;
    QList<BlockTextRange> ranges;
    int offset = 0;
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
        const QString rendered = applyStyle(text, style, m_options);
        ranges.append({i, offset, static_cast<int>(rendered.size())});
        blocks << rendered;
        offset += rendered.size() + 2;  // blocks.join("\n\n") separator
    }
    return {blocks.join(QStringLiteral("\n\n")), ranges};
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
        box.rect = calibrateRect(QRectF(nx1, ny1, nx2 - nx1, ny2 - ny1));
        box.positioned = t.hasBbox;

        int dupIndex = -1;
        if (t.hasBbox) {
            for (int j = 0; j < page.boxes.size(); ++j) {
                if (!page.boxes.at(j).positioned)
                    continue;
                const QRectF &other = page.boxes.at(j).rect;
                if (qAbs(other.x() - box.rect.x()) <= kDuplicateTolerance && qAbs(other.y() - box.rect.y()) <= kDuplicateTolerance && qAbs(other.width() - box.rect.width()) <= kDuplicateTolerance &&
                    qAbs(other.height() - box.rect.height()) <= kDuplicateTolerance) {
                    dupIndex = j;
                    break;
                }
            }
        }

        if (dupIndex >= 0) {
            page.hasDuplicates = true;
            page.boxes[dupIndex] = box;
            page.boxes[dupIndex].duplicateSuspect = true;
            continue;
        }

        page.boxes.append(box);
    }

    // The model sometimes re-emits already recognized blocks with shifted or
    // partially overlapping coordinates, so exact bbox matching alone misses
    // them. A re-emission that mostly repeats an earlier block is a duplicate:
    // its unique tail is glued onto the earlier block, otherwise the more
    // complete copy wins. The survivor gets duplicateSuspect so the verifier
    // re-checks it regardless of the label filter.
    QList<BlockFingerprint> fingerprints;
    fingerprints.reserve(page.boxes.size());
    for (const BoundingBox &b : page.boxes)
        fingerprints.append(fingerprintForDuplicateCheck(b.text));

    for (int i = 1; i < page.boxes.size(); ++i) {
        for (int j = 0; j < i; ++j) {
            if (!repeatsEarlierText(fingerprints.at(i), fingerprints.at(j)))
                continue;
            page.hasDuplicates = true;

            const BlockFingerprint &candidate = fingerprints.at(i);
            const int kept = keptPrefixWords(candidate, fingerprints.at(j));
            const QString tail = candidate.simplified.mid(charOffsetAfterWords(candidate.simplified, kept)).trimmed();
            if (kept >= kMinRepeatWords && kept >= kRepeatCoverage * candidate.words.size() && tail.size() >= kMinTailChars) {
                BoundingBox &earlier = page.boxes[j];
                if (earlier.text.endsWith(QLatin1Char('-')))
                    earlier.text += tail;
                else
                    earlier.text += QLatin1Char(' ') + tail;
                fingerprints[j] = fingerprintForDuplicateCheck(earlier.text);
            } else if (page.boxes.at(j).text.size() < page.boxes.at(i).text.size()) {
                page.boxes[j] = page.boxes.at(i);
                fingerprints[j] = fingerprints.at(i);
            }
            page.boxes[j].duplicateSuspect = true;
            page.boxes.removeAt(i);
            fingerprints.removeAt(i);
            --i;
            break;
        }
    }

    page.text = rebuildText(page);
    result.text = page.text;
    result.pages.append(page);
    return result;
}

}  // namespace llocr
