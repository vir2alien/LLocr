#pragma once

#include <QString>

namespace llocr {

enum class ConnectionRole {
    Ocr,
    BlockRecognition,
    Decision,
    Layout,
};

inline QString connectionRoleName(ConnectionRole role)
{
    switch (role) {
    case ConnectionRole::Ocr:
        return QStringLiteral("ocr");
    case ConnectionRole::BlockRecognition:
        return QStringLiteral("blockRecognition");
    case ConnectionRole::Decision:
        return QStringLiteral("decision");
    case ConnectionRole::Layout:
        return QStringLiteral("layout");
    }
    return QStringLiteral("ocr");
}

// Unknown ids resolve to Ocr: every ForRole dispatcher treats an unrecognized
// role as the OCR settings.
inline ConnectionRole connectionRoleFromName(const QString &name)
{
    if (name == QLatin1String("blockRecognition"))
        return ConnectionRole::BlockRecognition;
    if (name == QLatin1String("decision"))
        return ConnectionRole::Decision;
    if (name == QLatin1String("layout"))
        return ConnectionRole::Layout;
    return ConnectionRole::Ocr;
}

}  // namespace llocr
