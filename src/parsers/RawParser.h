#pragma once

#include "parsers/IOutputParser.h"
#include "parsers/ParserOptions.h"

namespace llocr {

class RawParser : public IOutputParser {
    Q_DISABLE_COPY_MOVE(RawParser)

public:
    // The raw parser ignores every option: no fragments, no styling, no
    // coordinate space. The parameter keeps the factory signature uniform.
    explicit RawParser(const ParserOptions & = {}) {}

    OcrResult parse(const QString &rawText) const override;
    QString rebuildText(const OcrPage &page) const override;
    QString id() const override;
    QString displayName() const override;
};

} // namespace llocr
