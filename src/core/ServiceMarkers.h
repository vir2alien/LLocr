#pragma once

#include <QRegularExpression>
#include <QString>

namespace llocr {

inline QString stripServiceTokens(const QString &text)
{
    // <|...|> (and the fullwidth variant) are the ADR 18 service markers;
    // the rest is the LFM2.5-VL layout-annotation drift (experimental format,
    // model card): literal <label>…</label> headers, <content>/<figure>/<image>
    // wrappers, stray "image_index=<n>" headers and elision lines (a lone "<"
    // or "...") that leak into block text when the model skips the bbox.
    static const QRegularExpression re(QStringLiteral(
        "<(?:\\||\\x{FF5C})[^>]*"
        ">|<label>[A-Za-z0-9_]*</label>"
        "|</?(?:content|figure|image)>"
        "|\\bimage_index=\\d+"
        "|(?:\\n|^)[ \\t]*<[ \\t]*(?=\\n|$)"
        "|(?:\\n|^)[ \\t]*\\.{3}[ \\t]*(?=\\n|$)"));
    QString out = text;
    out.remove(re);
    return out;
}

}  // namespace llocr
