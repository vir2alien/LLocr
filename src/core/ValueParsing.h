#pragma once

#include <QString>
#include <optional>

namespace llocr {

inline std::optional<double> toFiniteNumber(const QString &text)
{
    bool ok = false;
    const double number = text.trimmed().toDouble(&ok);
    if (!ok || !qIsFinite(number))
        return std::nullopt;
    return number;
}

}
