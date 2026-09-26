#include "parsers/Lfm25VlDrift.h"

#include <QRegularExpression>

namespace llocr {

QString normalizeDriftRegions(const QString &raw)
{
    static const QRegularExpression longLabelRe(
        QStringLiteral(R"(image_index=\d+ +<label>([^<\n]+ [^<\n]+)[ \t]*</label>[ \t]*\n[ \t]*<content>[ \t]*\n?)"));

    QString out = raw;
    out.replace(longLabelRe, QStringLiteral(
        "image_index=0 <label>table_caption</label>\n\\1\n"
        "image_index=0 <label>table</label>\n"));
    return out;
}

} // namespace llocr
