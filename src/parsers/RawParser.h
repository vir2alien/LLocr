#pragma once

#include "parsers/IOutputParser.h"

namespace llocr {

/**
 * @brief Pass-through parser: returns the model text unchanged as a single page
 *
 * For the plain text without positional markers
 */
class RawParser : public IOutputParser {
public:
    OcrResult parse(const QString &rawText) const override;
    QString id() const override;
};

} // namespace llocr
