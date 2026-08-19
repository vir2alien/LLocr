#pragma once

#include <QHash>
#include <QString>

namespace llocr {

enum class BlockStyle {
    PlainText,
    Heading,
    ImagePlaceholder,
    Italic,
    Equation,
    Table,
};

struct BlockStyleInfo {
    BlockStyle style = BlockStyle::PlainText;
    int headingLevel = 0;
    int imageIndex = -1;  ///< Index of the block in OcrPage::boxes (for ImagePlaceholder).
};

inline BlockStyleInfo blockStyleForLabel(const QString &label)
{
    static const QHash<QString, BlockStyleInfo> table = {
        {QStringLiteral("title"),          {BlockStyle::Heading,          0}},
        {QStringLiteral("image"),          {BlockStyle::ImagePlaceholder, 0}},
        {QStringLiteral("image_caption"),  {BlockStyle::Italic,           0}},
        {QStringLiteral("table_caption"),  {BlockStyle::Italic,           0}},
        {QStringLiteral("table_footnote"), {BlockStyle::Italic,           0}},
        {QStringLiteral("page_number"),    {BlockStyle::Italic,           0}},
        {QStringLiteral("equation"),       {BlockStyle::Equation,          0}},
        {QStringLiteral("table"),          {BlockStyle::Table,            0}},
        // "text", "footer" and others → absent → fall through to PlainText.
    };
    return table.value(label, {BlockStyle::PlainText, 0});
}

} // namespace llocr