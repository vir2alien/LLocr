#pragma once

#include <array>
#include <cstddef>
#include <QString>
#include <QStringList>

namespace llocr {

enum class ConnectionRole {
    BlockRecognition,
    Decision,
    Layout,
    Fix,
};

// The app's four tasks, in the order the settings and the task table show
// them. The list is the single source: the settings dispatch, the runtime's
// per-task state and the QML task table read it. A new role is a new enum
// value, a new row here and a new case in connectionRoleName — the compiler
// says when the case is missing.
inline constexpr std::array<ConnectionRole, 4> connectionRoles = {
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
    case ConnectionRole::BlockRecognition:
        return QStringLiteral("blockRecognition");
    case ConnectionRole::Decision:
        return QStringLiteral("decision");
    case ConnectionRole::Layout:
        return QStringLiteral("layout");
    case ConnectionRole::Fix:
        return QStringLiteral("fix");
    }
    return QStringLiteral("blockRecognition");
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

// Unknown ids resolve to BlockRecognition: every ForRole dispatcher treats an
// unrecognized role as the OCR model's settings. The install queue's filter
// (`task != connectionRoleName(connectionRoleFromName(task))`) does not follow
// the fallback — a task survives it only when the id is exactly a canonical
// role name, so an unknown id is dropped whatever the fallback is.
inline ConnectionRole connectionRoleFromName(const QString &name)
{
    for (const ConnectionRole role : connectionRoles) {
        if (connectionRoleName(role) == name)
            return role;
    }
    return ConnectionRole::BlockRecognition;
}

}  // namespace llocr
