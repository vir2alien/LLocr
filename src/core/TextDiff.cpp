#include "core/TextDiff.h"

#include <QStringView>

namespace llocr {
namespace {

struct Token {
    int start = 0;
    int length = 0;
};

// The characters a word is made of. The backslash and the dot belong to the
// word inside LaTeX (`\mathrm`, `H.BPp`), so a formula is compared by
// fragments and not by characters.
bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.') || c == QLatin1Char('\\');
}

QVector<Token> tokenize(const QString &text)
{
    QVector<Token> tokens;
    const int length = text.size();
    int index = 0;
    while (index < length) {
        if (text.at(index).isSpace()) {
            ++index;
            continue;
        }
        const int start = index;
        if (isWordChar(text.at(index))) {
            while (index < length && isWordChar(text.at(index)))
                ++index;
        } else {
            ++index;
        }
        tokens.append({start, index - start});
    }
    return tokens;
}

bool sameToken(const QString &before, const Token &a, const QString &after, const Token &b)
{
    if (a.length != b.length)
        return false;
    // The token is compared where it lies: a view borrows the characters, so
    // the table does not copy a fragment for every cell it asks about.
    return QStringView(before).mid(a.start, a.length) == QStringView(after).mid(b.start, b.length);
}

// A range that follows another one with nothing but whitespace in between
// belongs to the same change: the fragments the correction dropped read as
// one hole, not as two.
void appendRange(QVector<DiffRange> &ranges, const QString &text, int start, int length)
{
    if (length <= 0)
        return;
    if (!ranges.isEmpty()) {
        DiffRange &last = ranges.last();
        const int lastEnd = last.start + last.length;
        if (start <= lastEnd) {
            last.length = qMax(lastEnd, start + length) - last.start;
            return;
        }
        if (text.mid(lastEnd, start - lastEnd).trimmed().isEmpty()) {
            last.length = start + length - last.start;
            return;
        }
    }
    ranges.append({start, length});
}

}  // namespace

TextDiff diffText(const QString &before, const QString &after)
{
    TextDiff diff;
    const QVector<Token> a = tokenize(before);
    const QVector<Token> b = tokenize(after);
    const int countA = int(a.size());
    const int countB = int(b.size());
    if (countA == 0 && countB == 0)
        return diff;

    // The common head and the common tail are skipped: a formula that differs
    // in one place is compared in that place only.
    int prefix = 0;
    while (prefix < countA && prefix < countB && sameToken(before, a.at(prefix), after, b.at(prefix)))
        ++prefix;
    int suffix = 0;
    while (suffix < countA - prefix && suffix < countB - prefix && sameToken(before, a.at(countA - 1 - suffix), after, b.at(countB - 1 - suffix)))
        ++suffix;

    const int n = countA - prefix - suffix;
    const int m = countB - prefix - suffix;
    if (n == 0 && m == 0)
        return diff;

    // The comparison table is quadratic; a text too long to compare is shown
    // without highlighting rather than after a long freeze.
    constexpr int kMaxCells = 400000;
    if (static_cast<qsizetype>(n) * m > kMaxCells)
        return diff;

    QVector<int> table((n + 1) * (m + 1), 0);
    for (int i = n - 1; i >= 0; --i) {
        for (int j = m - 1; j >= 0; --j) {
            const int diagonal = table[(i + 1) * (m + 1) + j + 1];
            if (sameToken(before, a.at(prefix + i), after, b.at(prefix + j)))
                table[i * (m + 1) + j] = diagonal + 1;
            else
                table[i * (m + 1) + j] = qMax(diagonal, table[i * (m + 1) + j + 1]);
        }
    }

    int i = 0;
    int j = 0;
    while (i < n && j < m) {
        if (sameToken(before, a.at(prefix + i), after, b.at(prefix + j))) {
            ++i;
            ++j;
        } else if (table[(i + 1) * (m + 1) + j] >= table[i * (m + 1) + j + 1]) {
            const Token &token = a.at(prefix + i);
            appendRange(diff.removed, before, token.start, token.length);
            ++i;
        } else {
            const Token &token = b.at(prefix + j);
            appendRange(diff.added, after, token.start, token.length);
            ++j;
        }
    }
    while (i < n) {
        const Token &token = a.at(prefix + i);
        appendRange(diff.removed, before, token.start, token.length);
        ++i;
    }
    while (j < m) {
        const Token &token = b.at(prefix + j);
        appendRange(diff.added, after, token.start, token.length);
        ++j;
    }
    return diff;
}

}  // namespace llocr
