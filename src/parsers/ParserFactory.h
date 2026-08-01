#pragma once

#include <memory>

#include <QString>

#include "parsers/IOutputParser.h"

namespace llocr {

/**
 * @brief Creates output parsers by their identifier
 *
 */
class ParserFactory {
public:
    static std::unique_ptr<IOutputParser> create(const QString& parserId);
};

} // namespace llocr
