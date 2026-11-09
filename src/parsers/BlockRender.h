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

}  // namespace llocr
