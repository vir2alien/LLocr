#include "parsers/ParserFactory.h"

#include <QDebug>

#include "parsers/DetTokensParser.h"
#include "parsers/RawParser.h"

namespace llocr {

static const QStringList kParserIds = {
    QStringLiteral("raw"),
    QStringLiteral("det_tokens"),
};

QStringList ParserFactory::registeredIds()
{
    return kParserIds;
}

std::unique_ptr<IOutputParser> ParserFactory::create(const QString& parserId) {
    if (parserId == QStringLiteral("det_tokens")) {
        return std::make_unique<DetTokensParser>();
    }
    if (parserId == QStringLiteral("raw")) {
        return std::make_unique<RawParser>();
    }
    qWarning() << "ParserFactory: unknown parser id" << parserId
               << "— falling back to 'raw'";
    return std::make_unique<RawParser>();
}

} // namespace llocr