#pragma once

#include <QRegularExpression>
#include <QString>

namespace llocr {

// llama.cpp does not stop on the Qwen end-of-sentence marker, so service
// markers (<|…|>, including the fullwidth-pipe variants) reach the client as
// literal text (ADR 18). One shared strip routine so the OCR parser and the
// check-model response post-processing cannot drift apart.
inline QString stripServiceTokens(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral(R"(<(?:\||\x{FF5C})[^>]*>)"));
    QString out = text;
    out.remove(re);
    return out;
}

}  // namespace llocr
