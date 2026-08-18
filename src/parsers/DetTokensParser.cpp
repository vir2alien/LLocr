#include "parsers/DetTokensParser.h"
#include "parsers/BlockStyle.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace llocr {

namespace {

// Regex helpers

// Matches a wrapped token as emitted by the current model:
//   <|det|>label [x1, y1, x2, y2]<|/det|>
// Label is an ASCII identifier (title, text, image, image_caption, …).
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

// The model streams JSON-escaped text: newlines appear as the two characters
// \n and backslashes are doubled (LaTeX \( is streamed as \\( ). Decode the
// common JSON escapes so the token content becomes the real text.
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

// Model control tokens that must never appear in the final text: a trailing
// <|end_of_sentence|> marker, and any <|det|>/<|/det|> wrappers not consumed
// as tokens, <|grounding|>, <|ref|>, etc. The model emits the EOS marker with
// full-width pipes (｜ U+FF5C) and ▁ (U+2581) space markers, so match both
// pipe widths.
QString stripServiceTokens(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral(R"(<(?:\||\x{FF5C})[^>]*>)"));
    QString out = text;
    out.remove(re);
    return out;
}

// LaTeX math to Markdown
// Convert LaTeX math delimiters to Markdown:
//   \( ... \) → $ ... $        (inline math)
//   \[ ... \] → $$ ... $$      (display math)
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
// Strips optional surrounding \[ / \] and wraps the body in $$ … $$ on its own
// lines. Used for the dedicated “equation” token, which is always display math.
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
// "3. Methodology"      -> 1 group  -> level 2 (##)
// "3.1. Long-horizon"   -> 2 groups -> level 3 (###) etc
// No numbering prefix -> fall back to level 2 (##).
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

QString applyStyle(const QString &text, const BlockStyleInfo &info)
{
    switch (info.style) {
    case BlockStyle::ImagePlaceholder:
        return QStringLiteral("![Image]()");
    case BlockStyle::Italic:
        return QLatin1Char('*') + text + QLatin1Char('*');
    case BlockStyle::Equation:
        return formatEquation(text);
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

OcrResult DetTokensParser::parse(const QString &rawText) const
{
    qDebug() << rawText;
    if (rawText.trimmed().isEmpty())
        return OcrResult::makeError(QStringLiteral("Empty OCR text"));

    OcrResult result;
    OcrPage page;
    QStringList blocks;

    // 1) tokens positions
    struct Token {
        QString label;
        int x1, y1, x2, y2;
        int textStart;   // offset right after the closing ']'
        int tokenStart;  // offset of the label itself
    };
    QList<Token> tokens;

    // The current model wraps tokens in <|det|>…<|/det|>; the legacy bare
    // "label [coords]" form is still accepted as a fallback.
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

    // No tokens -> raw text.
    if (tokens.isEmpty()) {
        page.text =
            stripServiceTokens(wrapped ? unescapeModelText(rawText) : rawText).trimmed();
        result.text = page.text;
        result.pages.append(page);
        result.success = true;
        return result;
    }

    result.success = true;

    // 2) capture text before the first token (if any)
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

    // 3) process each token
    const double range = kBboxCoordinateRange;

    for (int i = 0; i < tokens.size(); ++i) {
        const Token &t = tokens.at(i);
        const int spanEnd = (i + 1 < tokens.size())
                                ? tokens.at(i + 1).tokenStart
                                : rawText.size();
        QString boxText = rawText.mid(t.textStart, spanEnd - t.textStart);
        if (wrapped)   // decode JSON escapes only for the model's wrapped stream
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

        page.boxes.append(box);

        BlockStyleInfo style = blockStyleForLabel(t.label);
        if (style.style == BlockStyle::ImagePlaceholder) {
            blocks << applyStyle({}, style);
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