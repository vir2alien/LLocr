#pragma once

#include <QString>

#include "core/OcrResult.h"

namespace llocr {

/**
 * @brief Options controlling how raw model output is interpreted.
 */

struct ParserOptions {
    int bboxCoordinateRange = 1000;  ///< Raw coordinates are in [0, this].
};

/**
 * @brief Turns raw model text into a structured OcrResult.
 */

class IOutputParser {
public:
    virtual ~IOutputParser() = default;
    virtual OcrResult parse(const QString& rawText,
                            const ParserOptions& options) const = 0;

    virtual QString id() const = 0;
};

} // namespace llocr
