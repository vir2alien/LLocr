#pragma once

#include <array>
#include <cstddef>
#include <QString>
#include <QStringList>

namespace llocr {

enum class ConnectionRole {
    PageOcr,
    RawPageOcr,
    BlockRecognition,
    Decision,
    Layout,
    Fix,
};

// The app's six tasks, in the order the settings and the task table show
// them (ADR 215). The list is the single source: the settings dispatch, the
// runtime's per-task state and the QML task table read it. A new role is a
// new enum value, a new row here and a new case in connectionRoleName — the
// compiler says when the case is missing.
inline constexpr std::array<ConnectionRole, 6> connectionRoles = {
    ConnectionRole::PageOcr,
    ConnectionRole::RawPageOcr,
    ConnectionRole::BlockRecognition,
    ConnectionRole::Decision,
    ConnectionRole::Layout,
    ConnectionRole::Fix,
};

inline constexpr std::size_t connectionRoleCount = connectionRoles.size();

// The role values are dense and the list follows them: the settings dispatch
// and the runtime's per-task table are indexed by the role.
static_assert(static_cast<std::size_t>(connectionRoles.back()) + 1 == connectionRoleCount, "the role list must follow the enum's values");

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

// The ids of the roles, in the list's order.
inline QStringList connectionRoleIds()
{
    QStringList ids;
    ids.reserve(int(connectionRoleCount));
    for (const ConnectionRole role : connectionRoles)
        ids << connectionRoleName(role);
    return ids;
}

// Unknown ids resolve to PageOcr: every ForRole dispatcher treats an
// unrecognized role as the page OCR settings. The legacy "ocr" string (old
// settings, registry role tags, QML written before the task rework) maps to
// PageOcr too.
inline ConnectionRole connectionRoleFromName(const QString &name)
{
    const QString wanted = name == QLatin1String("ocr") ? QStringLiteral("pageOcr") : name;
    for (const ConnectionRole role : connectionRoles) {
        if (connectionRoleName(role) == wanted)
            return role;
    }
    return ConnectionRole::PageOcr;
}

}  // namespace llocr
