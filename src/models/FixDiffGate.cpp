#include "models/FixDiffGate.h"

#include <QRegularExpression>

namespace llocr::FixDiffGate {

namespace {

Outcome review(const StatusMessage &reason)
{
    Outcome outcome;
    outcome.status = CheckStatus::Review;
    outcome.downgradeReason = reason;
    return outcome;
}

Outcome ok(const StatusMessage &reason)
{
    Outcome outcome;
    outcome.status = CheckStatus::Ok;
    outcome.downgradeReason = reason;
    return outcome;
}

// "none" / "Nothing to change" — the model found nothing but chose FIX.
bool noChangesWord(const QString &line)
{
    static const QStringList words = {QStringLiteral("none"),
                                      QStringLiteral("nothing"),
                                      QStringLiteral("nothingtochange"),
                                      QStringLiteral("nochanges"),
                                      QStringLiteral("nochange"),
                                      QStringLiteral("ничего"),
                                      QStringLiteral("нечегоисправлять"),
                                      QStringLiteral("нетправок")};
    QString compact = line.left(24);
    compact.remove(QRegularExpression(QStringLiteral("[.:!\\s]")));
    return words.contains(compact.toLower());
}

// The equivalence view a comparison runs in: NFC per character, soft hyphens
// dropped, ligatures and the ellipsis expanded, spaces and line breaks
// collapsed (except code) — and the inline/display math delimiters \( \) \[ \]
// removed outright: they are carrier markup, never content. A hyphen that
// ends a line keeps the word whole ("перего-\nродок" reads as
// "перего-родок"), which is the line-end hyphenation the prompts never fix.
QString normalizeForMatch(const QString &text, bool isCode)
{
    QString out;
    out.reserve(text.size());
    bool pendingSpace = false;
    bool pendingHyphen = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        const bool isSpace = c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\n') || c == QLatin1Char('\r');
        if (isSpace && pendingHyphen && !isCode) {
            pendingHyphen = false;
            continue;  // the hyphen of a wrapped word: the break is not content
        }
        pendingHyphen = c == QLatin1Char('-');
        if (!isCode && c == QLatin1Char('\\') && i + 1 < text.size()) {
            const QChar next = text.at(i + 1);
            if (next == QLatin1Char('(') || next == QLatin1Char(')') || next == QLatin1Char('[') || next == QLatin1Char(']')) {
                ++i;  // math delimiter pair: carrier, dropped entirely
                continue;
            }
        }
        QString produced;
        if (c == QChar(0x00AD)) {
            continue;  // soft hyphen
        } else if (c == QChar(0x2026)) {
            produced = QStringLiteral("...");
        } else if (c == QChar(0xFB00)) {
            produced = QStringLiteral("ff");
        } else if (c == QChar(0xFB01)) {
            produced = QStringLiteral("fi");
        } else if (c == QChar(0xFB02)) {
            produced = QStringLiteral("fl");
        } else if (c == QChar(0xFB03)) {
            produced = QStringLiteral("ffi");
        } else if (c == QChar(0xFB04)) {
            produced = QStringLiteral("ffl");
        } else if (!isCode && (isSpace || c == QChar(0x00A0) || (c >= QChar(0x2000) && c <= QChar(0x200A)) || c == QChar(0x202F) || c == QChar(0x205F) || c == QChar(0x3000))) {
            if (pendingSpace || out.isEmpty())
                continue;
            pendingSpace = true;
            produced = QStringLiteral(" ");
        } else if (!isCode && QStringLiteral("«»“”„‟").contains(c)) {
            produced = QStringLiteral("\"");
        } else if (!isCode && QStringLiteral("‘’‚‛").contains(c)) {
            produced = QStringLiteral("'");
        } else {
            pendingSpace = false;
            produced = QString(c).normalized(QString::NormalizationForm_C);
            // Per-character NFC cannot compose "e" + combining mark; glue a
            // combining mark onto the previous output character instead.
            if (!isCode && produced.size() == 1 && !out.isEmpty() && c.category() == QChar::Mark_NonSpacing) {
                const QString combined = (out.at(out.size() - 1) + produced).normalized(QString::NormalizationForm_C);
                if (combined.size() == 1) {
                    out.replace(out.size() - 1, 1, combined);
                    continue;
                }
            }
        }
        out.append(produced);
    }
    while (!out.isEmpty() && out.endsWith(QLatin1Char(' ')))
        out.chop(1);
    return out;
}

// The math-insensitive view: LaTeX spacing carries no content ("\mathrm{Bi} _ {1}"
// and "\mathrm{Bi}_1" render identically), \tag{n} equals (n), and the display
// wrapper is not content — the v6 rules. A reply that only rewrites these
// is churn, never a correction.
QString mathNormalize(const QString &text)
{
    static const QRegularExpression tag(QStringLiteral("\\tag\\s*\\{([^}]*)\\}"));
    QString out = text;
    out.replace(tag, QStringLiteral("(\\1)"));
    // x_{1} and x_1 render identically for a single-token script.
    static const QRegularExpression script(QStringLiteral("([_^])\\s*\\{([^{}\\s]+)\\}"));
    out.replace(script, QStringLiteral("\\1\\2"));
    out.remove(QRegularExpression(QStringLiteral("\\s+")));
    // The display wrapper is carrier, not content: "$$x$$" and "\[x\]" are the
    // same formula.
    static const QRegularExpression dollars(QStringLiteral("^\\$\\$([\\s\\S]*)\\$\\$$"));
    static const QRegularExpression brackets(QStringLiteral("^\\\\\\[([\\s\\S]*)\\\\\\]$"));
    for (const QRegularExpression &wrapper : {dollars, brackets}) {
        const QRegularExpressionMatch match = wrapper.match(out);
        if (match.hasMatch())
            out = match.captured(1);
    }
    return out;
}

bool mathEquivalent(const QString &a, const QString &b)
{
    return mathNormalize(a) == mathNormalize(b);
}

}  // namespace

bool isMathBlockType(const QString &blockType)
{
    return blockType == QLatin1String("equation") || blockType == QLatin1String("formula");
}

QString normalizeForEquivalence(const QString &text, bool isCode)
{
    return normalizeForMatch(text, isCode);
}

QString normalizeLatexCandidate(const QString &text)
{
    static const QRegularExpression spaceBeforeBrace(QStringLiteral("([\\w}\\)])\\s+([{}\\[()])"));
    static const QRegularExpression spaceAfterOpen(QStringLiteral("([\\{\\[(])\\s+"));
    QString out = text;
    out.replace(spaceBeforeBrace, QStringLiteral("\\1\\2"));
    out.replace(spaceAfterOpen, QStringLiteral("\\1"));
    out.replace(QStringLiteral("\\tag {"), QStringLiteral("\\tag{"));
    out = out.normalized(QString::NormalizationForm_C);
    return out;
}

Outcome fixBlock(const QString &candidate, const QString &body, const QString &blockType)
{
    const bool isCode = blockType == QLatin1String("code");
    const QString corrected = body.trimmed();

    if (corrected.isEmpty())
        return review(StatusMessage::translate("FixDiffGate", "The corrected block is empty."));
    if (noChangesWord(corrected))
        return review(StatusMessage::translate("FixDiffGate", "The model declared no change instead of a verdict; the block needs a human look."));

    // A block the model re-spelled without changing anything is not a
    // correction: the candidate's own bytes stay (ADR 192).
    const bool equivalent = normalizeForEquivalence(corrected, isCode) == normalizeForEquivalence(candidate, isCode) ||
                            ((corrected.contains(QLatin1Char('\\')) || candidate.contains(QLatin1Char('\\'))) && mathEquivalent(corrected, candidate));
    if (equivalent)
        return ok(StatusMessage::translate("FixDiffGate", "The corrected block is equivalent to the candidate; the block text is kept."));

    Outcome outcome;
    outcome.status = CheckStatus::Fixed;
    outcome.text = corrected;
    return outcome;
}

}  // namespace llocr::FixDiffGate
