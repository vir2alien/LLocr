#pragma once

#include <QString>
#include <QVariant>

#include "core/OcrResult.h"
#include "parsers/BlockStyle.h"
#include "parsers/IOutputParser.h"
#include "parsers/ParserOptions.h"

namespace llocr {

// The text of a block, in the style its label asks for.
QString renderBlockText(const QString &text, const BlockStyleInfo &style, const ParserOptions &options);

// The page text from its blocks: the styled texts joined by a blank line,
// with the range every block occupies in it.
IOutputParser::RebuiltPageText renderPageText(const OcrPage &page, const ParserOptions &options);

// The box owning each top-level "\n\n"-separated block of the page text, in
// order, or -1 where no range covers it: the WYSIWYG editor tags its DOM
// blocks with this order and maps clicks and highlights back through it.
QVariantList blockBoxMap(const QString &pageText, const QList<BlockTextRange> &ranges);

// The top-level "\n\n"-separated blocks of the page text: the same order the
// editor's DOM blocks walk, one entry per block (a model fragment may span
// several of them).
QVariantList blockSegments(const QString &pageText);

// A legacy project's page text (the user's edits already inside it) maps back
// into per-block user layers (ADR 296). False when the fragments do not
// appear in order — the canonical document stands, nothing is layered.
bool migrateLegacyPageText(OcrPage &page, const QString &legacyText, const ParserOptions &options);

}  // namespace llocr
