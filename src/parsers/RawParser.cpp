#include "parsers/RawParser.h"

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

QString RawParser::id() const
{
    return QStringLiteral("raw");
}

} // namespace llocr
