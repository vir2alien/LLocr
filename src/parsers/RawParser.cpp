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
    if (page.boxes.isEmpty())
        return page.text;
    // Boxes reach a raw page only through layout markup: the joined effective
    // texts are the page, so a block filled by re-recognition reaches the
    // preview instead of the stale markup text.
    QStringList blocks;
    for (const BoundingBox &box : page.boxes) {
        const QString text = box.correctedText.isEmpty() ? box.text : box.correctedText;
        if (!text.isEmpty())
            blocks.append(text);
    }
    return blocks.join(QStringLiteral("\n\n"));
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
