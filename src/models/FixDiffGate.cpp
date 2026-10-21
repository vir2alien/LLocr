#include "models/FixDiffGate.h"

#include <QRegularExpression>
#include <QSet>
#include <QVector>

#include <algorithm>

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

bool parseEditLine(const QString &rawLine, Edit *edit, StatusMessage *error)
{
    // A structure line declares a rowspan/colspan rewrite and carries no
    // fragments: "structure: rows 1 and 2 swapped".
    static const QRegularExpression structureOnly(QStringLiteral("^structure\\s*:\\s*(.+)$"));
    const auto structure = structureOnly.match(rawLine.trimmed());
    if (structure.hasMatch() && !rawLine.contains(QStringLiteral("=>"))) {
        edit->location = QStringLiteral("structure");
        edit->oldText = structure.captured(1).trimmed();
        return true;
    }

    const int arrow = rawLine.indexOf(QStringLiteral("=>"));
    if (arrow < 0) {
        *error = StatusMessage::translate("FixDiffGate", "Malformed edit line without '=>': %1", rawLine.trimmed().left(80));
        return false;
    }
    QString left = rawLine.left(arrow).trimmed();
    edit->newText = rawLine.mid(arrow + 2).trimmed();

    // Table replies prefix the cell: "r2c3: old => new" (the row-only "2:"
    // spelling and the bare "1c5:" one both occur in the wild).
    static const QRegularExpression location(QStringLiteral("^(\\d*r?\\d+c\\d+|\\d+r\\d+|r?\\d+)\\s*:\\s*(.*)$"));
    const auto located = location.match(left);
    if (located.hasMatch()) {
        edit->location = located.captured(1);
        left = located.captured(2).trimmed();
    }
    edit->oldText = left;
    edit->oldText.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    edit->newText.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    return true;
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

// Every non-empty line was a no-changes word: the reply refuses to list
// edits while still saying FIX. A malformed line among valid ones is
// skipped — long replies get truncated mid-list, and the edits above the
// cut are still anchored and gated.
bool parseEditLines(const QStringList &lines, QList<Edit> *edits, StatusMessage *error, bool *noopOnly)
{
    *noopOnly = true;
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        if (noChangesWord(line))
            continue;
        *noopOnly = false;
        Edit edit;
        if (!parseEditLine(line, &edit, error))
            continue;
        edits->append(edit);
    }
    if (edits->isEmpty()) {
        if (error->isEmpty())
            *error = StatusMessage::translate("FixDiffGate", "The FIX reply lists no edits.");
        return false;
    }
    return true;
}

// Word-level opcodes over the two token lists, with the shared prefix and
// suffix stripped so the LCS table stays small on real blocks.
struct DiffSpan {
    int i1 = 0, i2 = 0, j1 = 0, j2 = 0;
};

QList<DiffSpan> replacedSpans(const QStringList &candidateTokens, const QStringList &correctedTokens)
{
    QList<DiffSpan> spans;
    int begin = 0;
    while (begin < candidateTokens.size() && begin < correctedTokens.size() && candidateTokens.at(begin) == correctedTokens.at(begin))
        ++begin;
    int endCandidate = candidateTokens.size(), endCorrected = correctedTokens.size();
    while (endCandidate > begin && endCorrected > begin && candidateTokens.at(endCandidate - 1) == correctedTokens.at(endCorrected - 1)) {
        --endCandidate;
        --endCorrected;
    }

    const int n = endCandidate - begin, m = endCorrected - begin;
    if (n == 0 || m == 0) {
        if (n > 0 || m > 0)
            spans.append({begin, endCandidate, begin, endCorrected});
        return spans;
    }

    // A pathologically large middle (no shared head or tail) degrades to one
    // replace span: the gate then leans on the declared EDITS: checks only.
    if (static_cast<qint64>(n) * m > 30'000'000) {
        spans.append({begin, endCandidate, begin, endCorrected});
        return spans;
    }

    QVector<quint32> lengths((n + 1) * (m + 1), 0);
    for (int i = n - 1; i >= 0; --i) {
        for (int j = m - 1; j >= 0; --j) {
            const quint32 shared =
                candidateTokens.at(begin + i) == correctedTokens.at(begin + j) ? lengths[(i + 1) * (m + 1) + j + 1] + 1 : std::max(lengths[(i + 1) * (m + 1) + j], lengths[i * (m + 1) + j + 1]);
            lengths[i * (m + 1) + j] = shared;
        }
    }

    int i = 0, j = 0;
    while (i < n && j < m) {
        if (candidateTokens.at(begin + i) == correctedTokens.at(begin + j)) {
            ++i;
            ++j;
            continue;
        }
        const int startI = i, startJ = j;
        while (i < n || j < m) {
            if (i < n && j < m && candidateTokens.at(begin + i) == correctedTokens.at(begin + j))
                break;
            if (i < n && (j >= m || lengths[(i + 1) * (m + 1) + j] >= lengths[i * (m + 1) + j + 1]))
                ++i;
            else
                ++j;
        }
        spans.append({begin + startI, begin + i, begin + startJ, begin + j});
    }
    if (i < n || j < m) {
        if (!spans.isEmpty() && spans.last().i2 == begin + i && spans.last().j2 == begin + j)
            spans.last().i2 = endCandidate, spans.last().j2 = endCorrected;
        else
            spans.append({begin + i, endCandidate, begin + j, endCorrected});
    }
    return spans;
}

// The equivalence view a match runs in: NFC per character, soft hyphens
// dropped, ligatures and the ellipsis expanded, spaces and quotes collapsed
// (except code) — and the inline/display math delimiters \( \) \[ \] removed
// outright: they are carrier markup, never content, and the decision rules
// compare without them (docs/21). origins[i] is the source index of the run
// that produced text[i], so a match in this view maps back to a real span.
struct NormalizedText {
    QString text;
    QVector<int> origins;
};

NormalizedText normalizedForMatch(const QString &text, bool isCode, bool joinHyphens = false, bool dropSpaces = false)
{
    NormalizedText out;
    out.origins.reserve(text.size());
    bool pendingSpace = false;
    bool pendingHyphen = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (joinHyphens && pendingHyphen && (c == QLatin1Char(' ') || c == QLatin1Char('\n') || c == QLatin1Char('\t'))) {
            // Line-end hyphenation: "перего-\nродок" matches "перего-родок".
            continue;
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
        } else if (!isCode && (c == QChar(0x00A0) || (c >= QChar(0x2000) && c <= QChar(0x200A)) || c == QChar(0x202F) || c == QChar(0x205F) || c == QChar(0x3000) || c == QLatin1Char(' ') ||
                               c == QLatin1Char('\t'))) {
            if (dropSpaces || pendingSpace || out.text.isEmpty())
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
            if (!isCode && produced.size() == 1 && !out.text.isEmpty() && c.category() == QChar::Mark_NonSpacing) {
                const QString combined = (out.text.at(out.text.size() - 1) + produced).normalized(QString::NormalizationForm_C);
                if (combined.size() == 1) {
                    out.text.replace(out.text.size() - 1, 1, combined);
                    continue;
                }
            }
        }
        for (const QChar &producedChar : produced) {
            out.text.append(producedChar);
            out.origins.append(i);
        }
    }
    while (!out.text.isEmpty() && out.text.endsWith(QLatin1Char(' '))) {
        out.text.chop(1);
        out.origins.removeLast();
    }
    return out;
}

// The [from, to) source span of a normalized-haystack range.
QPair<int, int> sourceSpan(const NormalizedText &hay, int from, int count)
{
    if (from < 0 || count <= 0 || from + count > hay.origins.size())
        return {-1, -1};
    return {hay.origins.at(from), hay.origins.at(from + count - 1) + 1};
}

}  // namespace

bool isFullBlockType(const QString &blockType)
{
    // Equations started out here too, but the measured reality flipped them:
    // qwen3.5-4b answers formulas in span style on its own and drowns the
    // full-block contract in spacing churn (ADR 180). The formula prompt now
    // asks for the complete formula, so fixFullBlock reads a formula reply
    // without an EDITS: section as the correction itself.
    return blockType == QLatin1String("table");
}

bool isMathBlockType(const QString &blockType)
{
    return blockType == QLatin1String("equation") || blockType == QLatin1String("formula");
}

QString normalizeForEquivalence(const QString &text, bool isCode)
{
    return normalizedForMatch(text, isCode).text;
}

// The math-insensitive view: LaTeX spacing carries no content ("\mathrm{Bi} _ {1}"
// and "\mathrm{Bi}_1" render identically), \tag{n} equals (n), and the display
// wrapper is not content — the v6 rules. A FIX reply that only rewrites these
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

Outcome fixSpan(const QString &candidate, const QString &body, const QString &blockType)
{
    const bool isCode = blockType == QLatin1String("code");

    QList<Edit> edits;
    StatusMessage error;
    bool noopOnly = false;
    if (!parseEditLines(body.split(QLatin1Char('\n')), &edits, &error, &noopOnly)) {
        if (noopOnly) {
            // "FIX none": the model declared no change instead of a verdict —
            // a deviation from the scheme, so the block goes to human eyes.
            return review(StatusMessage::translate("FixDiffGate", "The model declared no change instead of a verdict; the block needs a human look."));
        }
        // The model answers formulas in full-block style on its own even
        // under the span prompt: read it with the structural contract, where
        // the equivalence check still decides between Ok and Review.
        if (!body.contains(QStringLiteral("=>")))
            return fixFullBlock(candidate, body, blockType);
        return review(error);
    }

    QString updated = candidate;
    NormalizedText matchView = normalizedForMatch(updated, isCode);
    int applied = 0;
    for (const Edit &edit : edits) {
        // A replacement needs an anchor in the candidate; a pure insertion
        // has nowhere deterministic to land.
        if (edit.oldText.isEmpty())
            return review(StatusMessage::translate("FixDiffGate", "An edit has no anchor text on the left of '=>'."));

        // LaTeX spacing churn ("\mathrm{Bi} _ {1} => \mathrm{Bi}_1") renders
        // identically: skip it without touching the candidate's bytes.
        if (normalizeForEquivalence(edit.oldText, isCode) == normalizeForEquivalence(edit.newText, isCode) ||
            ((edit.oldText.contains(QLatin1Char('\\')) || edit.newText.contains(QLatin1Char('\\'))) && mathEquivalent(edit.oldText, edit.newText)))
            continue;

        int found = updated.indexOf(edit.oldText);
        int length = edit.oldText.size();
        const bool uniqueLiteral = found >= 0 && updated.indexOf(edit.oldText, found + 1) < 0;
        if (!uniqueLiteral) {
            // The model rarely copies the anchor byte-for-byte: match it in
            // the equivalence view (carrier markup dropped, spaces and quotes
            // collapsed) and map the hit back to a real span. A second
            // occurrence there is as ambiguous as a literal one.
            QString needle = normalizeForEquivalence(edit.oldText, isCode);
            NormalizedText view = matchView;
            if (!needle.isEmpty() && matchView.text.indexOf(needle) < 0) {
                // Line-end hyphenation: "перего-родок" against a candidate
                // that wraps "перего-\nродок" — join hyphens on both sides.
                view = normalizedForMatch(updated, isCode, !isCode);
                needle = normalizeForEquivalence(QString(edit.oldText).replace(QStringLiteral("- "), QStringLiteral("-")), isCode);
            }
            if (!needle.isEmpty() && view.text.indexOf(needle) < 0 && (edit.oldText.contains(QLatin1Char('\\')) || edit.newText.contains(QLatin1Char('\\')))) {
                // The model re-styles LaTeX spacing in the anchor even though
                // the wire carries the canonical form: match in the math
                // view, where whitespace carries no content at all.
                view = normalizedForMatch(updated, isCode, false, true);
                needle = mathNormalize(edit.oldText);
            }
            const int normalizedFirst = view.text.indexOf(needle);
            if (normalizedFirst < 0)
                return review(StatusMessage::translate("FixDiffGate", "The edit anchor was not found in the block: %1", edit.oldText.left(80)));
            if (view.text.indexOf(needle, normalizedFirst + 1) >= 0)
                return review(StatusMessage::translate("FixDiffGate", "The edit anchor matches more than one place: %1", edit.oldText.left(80)));
            const QPair<int, int> span = sourceSpan(view, normalizedFirst, needle.size());
            if (span.first < 0)
                return review(StatusMessage::translate("FixDiffGate", "The edit anchor was not found in the block: %1", edit.oldText.left(80)));
            found = span.first;
            length = span.second - span.first;
        }
        updated.replace(found, length, edit.newText);
        matchView = normalizedForMatch(updated, isCode);
        ++applied;
    }

    if (applied == 0)
        return ok(StatusMessage::translate("FixDiffGate", "Every proposed fix was an equivalent no-op; the block text is kept."));

    Outcome outcome;
    outcome.status = CheckStatus::Fixed;
    outcome.text = updated;
    return outcome;
}

// MinerU-style table carriers (<fcel>/<nl>/...) glue cells into one token
// when the cell text has no spaces; split them so the token diff sees the
// cells. HTML tables tokenize naturally.
QStringList tokenizeForDiff(const QString &normalized)
{
    QString spaced = normalized;
    for (const QLatin1String tag : {QLatin1String("<fcel>"), QLatin1String("<nl>"), QLatin1String("<lcel>"), QLatin1String("<ucel>")})
        spaced.replace(tag, QLatin1Char(' ') + tag + QLatin1Char(' '));
    QStringList tokens;
    for (const QString &token : spaced.split(QLatin1Char(' '), Qt::SkipEmptyParts))
        tokens.append(token);
    return tokens;
}

Outcome fixFullBlock(const QString &candidate, const QString &body, const QString &blockType)
{
    const bool isCode = blockType == QLatin1String("code");
    const bool isTable = blockType == QLatin1String("table");

    const QStringList lines = body.split(QLatin1Char('\n'));
    int editsLine = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString upper = lines.at(i).trimmed().toUpper();
        if (upper == QLatin1String("EDITS:") || upper == QLatin1String("EDITS")) {
            editsLine = i;
            break;
        }
    }

    const QString correctedBlock = lines.mid(0, editsLine < 0 ? lines.size() : editsLine).join(QLatin1Char('\n')).trimmed();
    if (correctedBlock.isEmpty())
        return review(StatusMessage::translate("FixDiffGate", "The corrected block is empty."));
    if (noChangesWord(correctedBlock))
        return review(StatusMessage::translate("FixDiffGate", "The model declared no change instead of a verdict; the block needs a human look."));

    // The equivalence check runs before the EDITS: requirement: an
    // equivalent rewrite is Ok whatever the declarations say, and a reply
    // without EDITS: at least gets that chance.
    const bool equivalent = normalizeForEquivalence(correctedBlock, isCode) == normalizeForEquivalence(candidate, isCode) ||
                            ((correctedBlock.contains(QLatin1Char('\\')) || candidate.contains(QLatin1Char('\\'))) && mathEquivalent(correctedBlock, candidate));
    if (equivalent)
        return ok(StatusMessage::translate("FixDiffGate", "The corrected block is equivalent to the candidate; the block text is kept."));
    if (editsLine < 0) {
        if (isMathBlockType(blockType)) {
            // The formula prompt asks for the complete formula and declares no
            // edits: the equivalence check above already ruled out a pure
            // respacing, so what is left is the corrected formula.
            Outcome outcome;
            outcome.status = CheckStatus::Fixed;
            outcome.text = correctedBlock;
            return outcome;
        }
        if (blockType == QLatin1String("table"))
            return review(StatusMessage::translate("FixDiffGate", "The corrected block is missing its EDITS: section."));
        return review(StatusMessage::translate("FixDiffGate", "The reply carries a whole block; this block type expects span edits."));
    }

    QList<Edit> edits;
    StatusMessage error;
    bool noopOnly = false;
    if (!parseEditLines(lines.mid(editsLine + 1), &edits, &error, &noopOnly)) {
        // "EDITS: none" — the reply claims no changes on a differing block.
        if (!noopOnly)
            return review(error);
        return review(StatusMessage::translate("FixDiffGate", "The reply declares no edits but the corrected block differs."));
    }

    const NormalizedText candidateView = normalizedForMatch(candidate, isCode);
    const auto containsEquivalent = [isCode](const QString &haystack, const QString &needle) {
        if (needle.isEmpty() || haystack.contains(needle))
            return true;
        const NormalizedText hayView = normalizedForMatch(haystack, isCode);
        return hayView.text.contains(normalizeForEquivalence(needle, isCode));
    };

    for (const Edit &edit : edits) {
        if (edit.location == QLatin1String("structure"))
            continue;  // a rowspan/colspan rewrite has no literal old fragment
        if (!edit.oldText.isEmpty() && !containsEquivalent(candidate, edit.oldText))
            return review(StatusMessage::translate("FixDiffGate", "A declared edit is not part of the block: %1", edit.oldText.left(80)));
        // "(removed …)"/"(empty)" style right sides describe the edit instead
        // of quoting the table; the token diff below still pins the result.
        const QString newTrimmed = edit.newText.trimmed();
        const bool descriptiveNew = newTrimmed.startsWith(QLatin1Char('(')) && newTrimmed.endsWith(QLatin1Char(')'));
        if (!edit.newText.isEmpty() && !descriptiveNew && !containsEquivalent(correctedBlock, edit.newText))
            return review(StatusMessage::translate("FixDiffGate", "A declared edit is not part of the corrected block: %1", edit.newText.left(80)));
    }

    const QStringList candidateTokens = tokenizeForDiff(candidateView.text);
    const QStringList correctedTokens = tokenizeForDiff(normalizeForEquivalence(correctedBlock, isCode));
    QSet<QString> declaredOldTokens;
    QSet<QString> declaredNewTokens;
    bool hasStructuralEdit = false;
    for (const Edit &edit : edits) {
        for (const QString &token : normalizeForEquivalence(edit.oldText, isCode).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            declaredOldTokens.insert(token);
        for (const QString &token : normalizeForEquivalence(edit.newText, isCode).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            declaredNewTokens.insert(token);
        hasStructuralEdit |= edit.location == QLatin1String("structure");
    }

    // Whole-token containment: every content token of a diff chunk must have
    // been declared, so a re-worded word cannot ride on a shared short token
    // ("x", "=", "и"). Markup carriers (<fcel>, <td>, …) are never content.
    static const QSet<QString> markupTokens = {QStringLiteral("<fcel>"),
                                               QStringLiteral("<nl>"),
                                               QStringLiteral("<lcel>"),
                                               QStringLiteral("<ucel>"),
                                               QStringLiteral("<td>"),
                                               QStringLiteral("</td>"),
                                               QStringLiteral("<tr>"),
                                               QStringLiteral("</tr>"),
                                               QStringLiteral("<table>"),
                                               QStringLiteral("</table>")};
    const auto covered = [](const QStringList &chunkTokens, const QSet<QString> &declared) {
        return std::all_of(chunkTokens.cbegin(), chunkTokens.cend(), [&declared](const QString &token) { return markupTokens.contains(token) || declared.contains(token); });
    };

    for (const DiffSpan &span : replacedSpans(candidateTokens, correctedTokens)) {
        const QStringList oldTokens = candidateTokens.mid(span.i1, span.i2 - span.i1);
        const QStringList newTokens = correctedTokens.mid(span.j1, span.j2 - span.j1);
        const QString oldChunk = oldTokens.join(QLatin1Char(' '));
        const QString newChunk = newTokens.join(QLatin1Char(' '));
        if (normalizeForEquivalence(oldChunk, isCode) == normalizeForEquivalence(newChunk, isCode))
            continue;
        // LaTeX spacing is equivalent by the v6 rules ("\tag {8.37}" and
        // "\tag{8.37}" tokenize differently but render the same): a chunk
        // with a backslash in it compares space-insensitively.
        if (!isCode && (oldChunk.contains(QLatin1Char('\\')) || newChunk.contains(QLatin1Char('\\'))) &&
            normalizeForEquivalence(oldChunk, isCode).remove(QLatin1Char(' ')) == normalizeForEquivalence(newChunk, isCode).remove(QLatin1Char(' ')))
            continue;
        // A structural rewrite of a table reshuffles whole rows; the cell-level
        // declarations above already pin its content.
        if (isTable && hasStructuralEdit)
            continue;
        if (!covered(oldTokens, declaredOldTokens) || !covered(newTokens, declaredNewTokens))
            return review(StatusMessage::translate("FixDiffGate", "The corrected block changes text the EDITS: list does not declare: %1", oldChunk.isEmpty() ? newChunk.left(80) : oldChunk.left(80)));
    }

    Outcome outcome;
    outcome.status = CheckStatus::Fixed;
    outcome.text = correctedBlock;
    return outcome;
}

}  // namespace llocr::FixDiffGate
