#pragma once

#include <QString>

#include "core/OcrResult.h"

namespace llocr {

class IOutputParser {
public:
    virtual ~IOutputParser() = default;
    virtual OcrResult parse(const QString &rawText) const = 0;

    virtual QString id() const = 0;
};

} // namespace llocr
