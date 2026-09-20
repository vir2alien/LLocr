#pragma once

#include <QRegularExpression>
#include <QString>

namespace llocr {

inline QString stripServiceTokens(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral(R"(<(?:\||\x{FF5C})[^>]*>)"));
    QString out = text;
    out.remove(re);
    return out;
}

}  // namespace llocr
