#pragma once

#include <QString>

#include "core/OcrResult.h"

namespace llocr {

/**
 * @brief Turns raw model text into a structured OcrResult.
 */

class IOutputParser {
public:
    virtual ~IOutputParser() = default;
    virtual OcrResult parse(const QString &rawText) const = 0;

    virtual QString id() const = 0;
};

} // namespace llocr
