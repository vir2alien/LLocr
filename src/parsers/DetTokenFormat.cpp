#include "parsers/DetTokenFormat.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>
#include <QRegularExpressionMatchIterator>

#include <algorithm>

namespace llocr {

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
    // LFM2.5-VL sometimes drops the closing delimiter at a cell/row end —
    // a trailing "\(" (or "\[") with no closer is still a formula.
    static const QRegularExpression danglingInlineRe(
        QStringLiteral(R"(\\\((?![\s\S]*\\\))([\s\S]*)$)"));
    static const QRegularExpression danglingDisplayRe(
        QStringLiteral(R"(\\\[(?![\s\S]*\\\])([\s\S]*)$)"));
    // The model wraps inline \(…\) inside display \[…\] — after the display
    // conversion those inner delimiters are redundant (and render literally).
    // Only backslash-paren delimiters are removed — bare parens are legit
    // formula content.
    static const QRegularExpression innerInlineRe(
        QStringLiteral(R"(\\\(|\\\))"));
    // Non-breaking-space artifact inside formulas (e.g. \mathrm{~r}) —
    // cleaned within math spans only, plain-text tildes are kept.
    static const QRegularExpression mathSpanRe(
        QStringLiteral(R"(\$[^\$]+\$)"));

    QString out = text;
    out.replace(displayRe, QStringLiteral("\n\n$$\n\\1\n$$\n\n"));
    out.replace(danglingDisplayRe, QStringLiteral("$$\\1$$"));

    // Protect the display-math blocks: the inline pass below must not touch
    // their content (nested \(…\) there would become $…$ and corrupt the
    // block). Placeholders keep the spans out of the inline replacements.
    QStringList displayBlocks;
    {
        static const QRegularExpression displaySpanRe(
            QStringLiteral(R"(\$\$[\s\S]*?\$\$)"));
        QString kept;
        qsizetype last = 0;
        QRegularExpressionMatchIterator dit = displaySpanRe.globalMatch(out);
        while (dit.hasNext()) {
            const QRegularExpressionMatch m = dit.next();
            kept += out.mid(last, m.capturedStart() - last);
            QString block = m.captured(0);
            block.remove(innerInlineRe);   // nested \(…\) -> bare content
            displayBlocks.append(block);
            kept += QStringLiteral("\x01%1\x01").arg(displayBlocks.size() - 1);
            last = m.capturedEnd();
        }
        kept += out.mid(last);
        out = kept;
    }

    out.replace(inlineRe, QStringLiteral("$\\1$"));
    out.replace(danglingInlineRe, QStringLiteral("$\\1$"));

    // Restore the protected display blocks.
    static const QRegularExpression placeholderRe(
        QStringLiteral(R"(\x01(\d+)\x01)"));
    {
        QString restored;
        qsizetype last = 0;
        QRegularExpressionMatchIterator pit = placeholderRe.globalMatch(out);
        while (pit.hasNext()) {
            const QRegularExpressionMatch m = pit.next();
            restored += out.mid(last, m.capturedStart() - last);
            restored += displayBlocks.at(m.captured(1).toInt());
            last = m.capturedEnd();
        }
        restored += out.mid(last);
        out = restored;
    }

    QString rebuilt;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = mathSpanRe.globalMatch(out);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        rebuilt += out.mid(last, m.capturedStart() - last);
        rebuilt += QString(m.captured(0)).replace(QLatin1Char('~'), QLatin1Char(' '));
        last = m.capturedEnd();
    }
    rebuilt += out.mid(last);
    return rebuilt;
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

// Display (block) math -> clean Markdown block.
QString formatEquation(const QString &text)
{
    static const QRegularExpression wrapperRe(
        QStringLiteral(R"(^\s*\\\[\s*([\s\S]*?)\s*\\\]\s*$)"));
    // The model often nests inline \(…\) inside the display \[…\] wrapper —
    // strip the redundant delimiters (bare parens are left untouched).
    static const QRegularExpression innerDelimRe(
        QStringLiteral(R"(\\\(|\\\))"));

    QString body = text.trimmed();
    const QRegularExpressionMatch m = wrapperRe.match(body);
    if (m.hasMatch())
        body = m.captured(1).trimmed();
    body.remove(innerDelimRe);

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

} // namespace llocr
