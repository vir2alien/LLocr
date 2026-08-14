#pragma once

#include "parsers/IOutputParser.h"

namespace llocr {

class RawParser : public IOutputParser {
    Q_DISABLE_COPY_MOVE(RawParser)

public:
    RawParser() = default;

    OcrResult parse(const QString &rawText) const override;
    QString id() const override;
};

} // namespace llocr
