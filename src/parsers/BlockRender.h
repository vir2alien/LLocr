#pragma once

#include <QString>

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

}  // namespace llocr
