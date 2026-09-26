#pragma once

#include <QString>

namespace llocr {

// Shared text helpers of the det-token parser family (DetTokensParser and
// its model-specific modules: OtslTable, Lfm25VlDrift).

// The wrapped response encodes real newlines as the two characters `\n`;
// this decodes them (nothing else).
QString unescapeModelText(const QString &text);

// LaTeX math (\(…\), \[…\], $…$) to Markdown math, cleaning model
// transcription artifacts (truncated delimiters, nested \(…\) inside
// display \[…\], the "~" non-breaking-space artifact inside math spans).
QString convertMath(const QString &text);

// GFM pipe-table cell: math conversion, pipe escaping, single line.
QString escapeTableCell(QString cell);

// Display (block) math -> clean Markdown $$…$$ block.
QString formatEquation(const QString &text);

// Title text ("3.4.1. Attention computation") -> heading level (#####).
int headingLevelFor(const QString &title);

} // namespace llocr
