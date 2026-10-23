#pragma once

#include <QString>
#include <QVector>

namespace llocr {

// A stretch of text that differs between the two versions.
struct DiffRange {
    int start = 0;
    int length = 0;
};

// What a correction changed: the fragments it dropped from the original and the
// fragments it put in. Everything outside these ranges is common to both.
struct TextDiff {
    QVector<DiffRange> removed;  ///< Ranges in the original text.
    QVector<DiffRange> added;    ///< Ranges in the corrected text.
};

// Compares two texts word by word and reports what changed.
//
// The granularity is a word: a run of letters, digits and the characters that
// belong to a word inside LaTeX (\, _, .). Braces, slashes and spaces are
// separators, so a formula rewritten as `\mathrm{н.впр}` against
// `\mathrm{H.BPp}` differs in one fragment instead of the whole line.
TextDiff diffText(const QString &before, const QString &after);

}  // namespace llocr
