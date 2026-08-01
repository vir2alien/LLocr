#include "parsers/ParserFactory.h"

#include "parsers/DetTokensParser.h"
#include "parsers/RawParser.h"

namespace llocr {

std::unique_ptr<IOutputParser> ParserFactory::create(const QString& parserId) {
    if (parserId == QStringLiteral("det_tokens")) {
        return std::make_unique<DetTokensParser>();
    }
    return std::make_unique<RawParser>();
}

} // namespace llocr
