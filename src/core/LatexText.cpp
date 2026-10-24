#include "core/LatexText.h"

#include <QRegularExpression>
#include <QStringList>

namespace llocr::LatexText {

namespace {

const QStringList &mathLabels()
{
    static const QStringList labels = {QStringLiteral("equation"), QStringLiteral("formula"), QStringLiteral("equation_block")};
    return labels;
}

struct CommandSpec {
    const char *name;
    int arguments;
};

// The commands whose argument count the app knows. A formula that lost one of
// them cannot be rendered: "\frac{1}" is half a fraction.
const CommandSpec kCommandsWithArguments[] = {
    {"binom", 2},
    {"dfrac", 2},
    {"frac", 2},
    {"tfrac", 2},
    {"mathbb", 1},
    {"mathcal", 1},
    {"mathbf", 1},
    {"mathit", 1},
    {"mathrm", 1},
    {"operatorname", 1},
    {"sqrt", 1},
    {"text", 1},
    {"textbf", 1},
    {"textit", 1},
};

int requiredArguments(const QString &name)
{
    for (const CommandSpec &spec : kCommandsWithArguments) {
        if (name == QLatin1String(spec.name))
            return spec.arguments;
    }
    return 0;
}

// The kind of a \left / \right delimiter: the opening character it belongs
// to. LaTeX pairs "(" with ")", "[" with "]", "{" with "}", "<" with ">" and
// the invisible "." with itself — "\left( x \right]" is an error.
QChar delimiterKind(const QString &text, int index)
{
    while (index < text.size() && text.at(index).isSpace())
        ++index;
    if (index >= text.size())
        return QChar();
    QChar c = text.at(index);
    if (c == QLatin1Char('\\') && index + 1 < text.size())
        c = text.at(index + 1);
    switch (c.unicode()) {
    case ')':
        return QLatin1Char('(');
    case ']':
        return QLatin1Char('[');
    case '}':
        return QLatin1Char('{');
    case '>':
        return QLatin1Char('<');
    default:
        return c;
    }
}

// The "{name}" that follows \begin or \end.
bool readEnvironmentName(const QString &text, int &index, QString &name)
{
    while (index < text.size() && text.at(index).isSpace())
        ++index;
    if (index >= text.size() || text.at(index) != QLatin1Char('{'))
        return false;
    const int close = text.indexOf(QLatin1Char('}'), index + 1);
    if (close < 0)
        return false;
    name = text.mid(index + 1, close - index - 1);
    index = close + 1;
    return true;
}

// Counts the arguments that follow a command: a braced group or a single
// character, as TeX reads them. An optional argument in brackets is skipped.
// The index moves past the arguments, but the text itself is left alone — the
// scan that calls this one counts the braces again.
int countArguments(const QString &text, int &index, int wanted)
{
    int counted = 0;
    while (counted < wanted) {
        while (index < text.size() && text.at(index).isSpace())
            ++index;
        if (index >= text.size())
            break;
        const QChar c = text.at(index);
        if (c == QLatin1Char('[')) {
            const int close = text.indexOf(QLatin1Char(']'), index + 1);
            if (close < 0)
                break;
            index = close + 1;
            continue;
        }
        if (c == QLatin1Char('{')) {
            int depth = 0;
            int cursor = index;
            for (; cursor < text.size(); ++cursor) {
                const QChar inner = text.at(cursor);
                if (inner == QLatin1Char('{'))
                    ++depth;
                else if (inner == QLatin1Char('}')) {
                    --depth;
                    if (depth == 0) {
                        ++cursor;
                        break;
                    }
                }
            }
            index = cursor;
            ++counted;
            continue;
        }
        // The closing delimiter of the math span ends the formula: what
        // follows it is not an argument of the command.
        if (c == QLatin1Char('$'))
            break;
        ++index;
        ++counted;
    }
    return counted;
}

// A markdown code fence is carrier markup, not formula content: the model
// answers "```latex\n…\n```" — sometimes in several pieces, sometimes inside
// the math wrapper. The fence is unwrapped and the LaTeX inside is kept: a
// formula with a fence in it cannot be rendered. An unbalanced fence is left
// alone — the syntax check names it.
QString unwrapCodeFences(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    QStringList kept;
    kept.reserve(lines.size());
    bool inside = false;
    for (const QString &line : lines) {
        if (line.trimmed().startsWith(QStringLiteral("```"))) {
            inside = !inside;
            continue;
        }
        kept.append(line);
    }
    if (inside)
        return text;
    return kept.join(QLatin1Char('\n'));
}

}  // namespace

bool isMathLabel(const QString &label)
{
    return mathLabels().contains(label);
}

QString canonical(const QString &text)
{
    static const QRegularExpression spaceBeforeBrace(QStringLiteral("([\\w}\\)])\\s+([{}\\[()])"));
    static const QRegularExpression spaceAfterOpen(QStringLiteral("([\\{\\[(])\\s+"));
    QString out = unwrapCodeFences(text);
    out.replace(spaceBeforeBrace, QStringLiteral("\\1\\2"));
    out.replace(spaceAfterOpen, QStringLiteral("\\1"));
    out.replace(QStringLiteral("\\tag {"), QStringLiteral("\\tag{"));
    out = out.normalized(QString::NormalizationForm_C);
    return out;
}

Problem check(const QString &text)
{
    if (text.isEmpty())
        return Problem::None;

    QList<int> braces;
    QList<QString> environments;
    QList<QChar> leftDelimiters;
    int inlineMathOpens = 0;
    int inlineMathCloses = 0;
    int displayMathOpens = 0;
    int displayMathCloses = 0;
    bool dollarSpanOpen = false;
    bool dollarSpanDisplay = false;
    int missingArgumentAt = -1;

    const int size = text.size();
    for (int i = 0; i < size; ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\\')) {
            if (i + 1 >= size)
                return Problem::TruncatedCommand;
            const QChar next = text.at(i + 1);
            if (!next.isLetter()) {
                switch (next.unicode()) {
                case '(':
                    ++inlineMathOpens;
                    break;
                case ')':
                    ++inlineMathCloses;
                    break;
                case '[':
                    ++displayMathOpens;
                    break;
                case ']':
                    ++displayMathCloses;
                    break;
                default:
                    break;
                }
                ++i;
                continue;
            }

            int end = i + 1;
            while (end < size && text.at(end).isLetter())
                ++end;
            const QString name = text.mid(i + 1, end - i - 1);
            const int commandStart = i;
            i = end - 1;

            if (name == QLatin1String("left")) {
                leftDelimiters.append(delimiterKind(text, end));
                continue;
            }
            if (name == QLatin1String("right")) {
                if (leftDelimiters.isEmpty())
                    return Problem::UnbalancedLeftRight;
                const QChar opened = leftDelimiters.takeLast();
                const QChar closed = delimiterKind(text, end);
                // The invisible delimiter (".") closes any opening delimiter
                // and is closed by any of them: that is how a bracket is split
                // across the lines of a formula ("\left[ a \right. \\ \left. b
                // \right]").
                if (!opened.isNull() && !closed.isNull() && opened != closed && opened != QLatin1Char('.') && closed != QLatin1Char('.'))
                    return Problem::UnbalancedLeftRight;
                continue;
            }
            if (name == QLatin1String("begin") || name == QLatin1String("end")) {
                QString environment;
                int cursor = end;
                if (!readEnvironmentName(text, cursor, environment) || environment.isEmpty())
                    return Problem::UnclosedEnvironment;
                i = cursor - 1;
                if (name == QLatin1String("begin")) {
                    environments.append(environment);
                } else {
                    if (environments.isEmpty() || environments.last() != environment)
                        return Problem::MismatchedEnvironment;
                    environments.removeLast();
                }
                continue;
            }
            const int arguments = requiredArguments(name);
            if (arguments > 0) {
                int cursor = end;
                if (countArguments(text, cursor, arguments) < arguments)
                    missingArgumentAt = commandStart;
            }
            continue;
        }

        if (c == QLatin1Char('{')) {
            braces.append(i);
            continue;
        }
        if (c == QLatin1Char('}')) {
            if (braces.isEmpty())
                return Problem::ExtraBrace;
            braces.removeLast();
            continue;
        }
        if (c == QLatin1Char('$')) {
            if (!dollarSpanOpen) {
                dollarSpanOpen = true;
                dollarSpanDisplay = i + 1 < size && text.at(i + 1) == QLatin1Char('$');
                if (dollarSpanDisplay)
                    ++i;
                continue;
            }
            if (!dollarSpanDisplay) {
                dollarSpanOpen = false;
                continue;
            }
            if (i + 1 < size && text.at(i + 1) == QLatin1Char('$')) {
                dollarSpanOpen = false;
                ++i;
            }
            continue;
        }
        // A backtick is markdown, not LaTeX: the code fence that carried the
        // formula was not unwrapped (ADR 195).
        if (c == QLatin1Char('`'))
            return Problem::CodeMarkup;
    }

    if (!braces.isEmpty())
        return Problem::UnclosedBrace;
    if (!environments.isEmpty())
        return Problem::UnclosedEnvironment;
    if (!leftDelimiters.isEmpty())
        return Problem::UnbalancedLeftRight;
    if (inlineMathOpens != inlineMathCloses || displayMathOpens != displayMathCloses)
        return Problem::UnbalancedDelimiter;
    if (dollarSpanOpen && text.trimmed().startsWith(QLatin1Char('$')))
        return Problem::UnbalancedDelimiter;
    if (missingArgumentAt >= 0)
        return Problem::MissingArgument;
    return Problem::None;
}

}  // namespace llocr::LatexText
