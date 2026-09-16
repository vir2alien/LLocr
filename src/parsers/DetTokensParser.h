#pragma once

#include "parsers/IOutputParser.h"

namespace llocr {

QString rebuildPageText(const OcrPage& page);

class DetTokensParser : public IOutputParser {
    Q_DISABLE_COPY_MOVE(DetTokensParser)

public:
    DetTokensParser() = default;

    OcrResult parse(const QString &rawText) const override;
    QString id() const override;

private:
    static constexpr int kBboxCoordinateRange = 1000;
};

} // namespace llocr