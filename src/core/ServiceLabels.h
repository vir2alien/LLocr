#pragma once

#include <QString>

namespace llocr {

// The page furniture: running heads, running feet and page numbers. The
// shipped vocabularies spell the heads and the feet header/footer (LFM says
// page_header/page_footer).
inline bool isPageFurnitureLabel(const QString &label)
{
    return label == QLatin1String("header") || label == QLatin1String("footer")
           || label == QLatin1String("page_header") || label == QLatin1String("page_footer")
           || label == QLatin1String("page_number");
}

}  // namespace llocr
