#include "parsers/ParserFactory.h"

#include <QCoreApplication>
#include <QDebug>

#include "parsers/Lfm25VlParser.h"
#include "parsers/ParserOptions.h"
#include "parsers/RawParser.h"
#include "parsers/UnlimitedOcrParser.h"

namespace llocr {

const QString ParserFactory::kAutoId = QStringLiteral("auto");

QStringList ParserFactory::registeredIds()
{
    return {QStringLiteral("raw"), QStringLiteral("unlimited-ocr"), QStringLiteral("lfm2.5-vl")};
}

QStringList ParserFactory::selectableIds()
{
    return QStringList{kAutoId} + registeredIds();
}

QStringList ParserFactory::selectableDisplayNames()
{
    QStringList names;
    names.append(QCoreApplication::translate("ParserFactory", "Automatic (model default)"));
    for (const QString &id : registeredIds()) {
        const auto parser = create(id);
        names.append(parser ? parser->displayName() : id);
    }
    return names;
}

std::unique_ptr<IOutputParser> ParserFactory::create(const QString &parserId, const ParserOptions &options)
{
    if (parserId == QLatin1String("unlimited-ocr"))
        return std::make_unique<UnlimitedOcrParser>(options);
    if (parserId == QLatin1String("lfm2.5-vl"))
        return std::make_unique<Lfm25VlParser>(options);
    if (parserId == QLatin1String("raw"))
        return std::make_unique<RawParser>(options);

    if (parserId == kAutoId) {
        qWarning() << "ParserFactory: 'auto' must be resolved before create()";
        return nullptr;
    }

    qWarning() << "ParserFactory: unknown parser id" << parserId << "— falling back to 'raw'";
    return std::make_unique<RawParser>(options);
}

}  // namespace llocr
