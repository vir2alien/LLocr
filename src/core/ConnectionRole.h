#pragma once

#include <QString>

namespace llocr {

enum class ConnectionRole {
    PageOcr,
    RawPageOcr,
    BlockRecognition,
    Decision,
    Layout,
    Fix,
};

inline QString connectionRoleName(ConnectionRole role)
{
    switch (role) {
    case ConnectionRole::PageOcr:
        return QStringLiteral("pageOcr");
    case ConnectionRole::RawPageOcr:
        return QStringLiteral("rawPageOcr");
    case ConnectionRole::BlockRecognition:
        return QStringLiteral("blockRecognition");
    case ConnectionRole::Decision:
        return QStringLiteral("decision");
    case ConnectionRole::Layout:
        return QStringLiteral("layout");
    case ConnectionRole::Fix:
        return QStringLiteral("fix");
    }
    return QStringLiteral("pageOcr");
}

// Unknown ids resolve to PageOcr: every ForRole dispatcher treats an
// unrecognized role as the page OCR settings. The legacy "ocr" string (old
// settings, registry role tags, QML written before the task rework) maps to
// PageOcr too.
inline ConnectionRole connectionRoleFromName(const QString &name)
{
    if (name == QLatin1String("pageOcr") || name == QLatin1String("ocr"))
        return ConnectionRole::PageOcr;
    if (name == QLatin1String("rawPageOcr"))
        return ConnectionRole::RawPageOcr;
    if (name == QLatin1String("blockRecognition"))
        return ConnectionRole::BlockRecognition;
    if (name == QLatin1String("decision"))
        return ConnectionRole::Decision;
    if (name == QLatin1String("layout"))
        return ConnectionRole::Layout;
    if (name == QLatin1String("fix"))
        return ConnectionRole::Fix;
    return ConnectionRole::PageOcr;
}

}  // namespace llocr
