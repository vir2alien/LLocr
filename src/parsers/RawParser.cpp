#include "parsers/RawParser.h"

#include <QCoreApplication>

namespace llocr {

OcrResult RawParser::parse(const QString &rawText) const
{
    OcrResult result;
    result.success = true;
    result.text = rawText;

    OcrPage page;
    page.text = rawText;
    result.pages.append(page);

    return result;
}

QString RawParser::rebuildText(const OcrPage &page) const
{
    // The raw parser keeps no fragments, so the page text is the whole truth.
    return page.text;
}

QString RawParser::id() const
{
    return QStringLiteral("raw");
}

QString RawParser::displayName() const
{
    return QCoreApplication::translate("RawParser", "Raw text");
}

}  // namespace llocr
