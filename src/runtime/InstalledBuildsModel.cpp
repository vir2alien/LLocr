#include "runtime/InstalledBuildsModel.h"

#include <QDir>
#include <QFileInfo>

#include "config/SettingsStore.h"
#include "runtime/RuntimeInstaller.h"

namespace llocr {

InstalledBuildsModel::InstalledBuildsModel(SettingsStore &settings, QObject *parent) : QAbstractListModel(parent), m_settings(settings) {}

int InstalledBuildsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_builds.size();
}

QHash<int, QByteArray> InstalledBuildsModel::roleNames() const
{
    return {
        {TagRole, "tag"},
        {BuildRole, "build"},
        {BackendRole, "backend"},
        {BackendDisplayRole, "backendDisplay"},
        {ServerPathRole, "serverPath"},
        {BinaryFoundRole, "binaryFound"},
        {ActiveRole, "active"},
    };
}

QVariant InstalledBuildsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_builds.size())
        return {};
    const InstalledBuildInfo &build = m_builds.at(index.row());
    switch (role) {
    case TagRole:
        return build.tag;
    case BuildRole:
        return build.build;
    case BackendRole:
        return build.backend;
    case BackendDisplayRole:
        return build.backend.isEmpty() ? QString() : RuntimeInstaller::backendDisplayName(build.backend);
    case ServerPathRole:
        return build.serverPath;
    case BinaryFoundRole:
        return !build.serverPath.isEmpty();
    case ActiveRole:
        return isActive(build);
    default:
        return {};
    }
}

void InstalledBuildsModel::setBuilds(const QList<InstalledBuildInfo> &builds)
{
    if (builds == m_builds)
        return;
    // A small, short-lived list: one reset is simpler than a diff and gives the
    // delegates every role again, which is what a rescan changes.
    beginResetModel();
    m_builds = builds;
    endResetModel();
    emit countChanged();
}

void InstalledBuildsModel::settingsChanged()
{
    if (m_builds.isEmpty())
        return;
    // Only the active highlight can change; re-emit it for every row.
    const QModelIndex first = index(0, 0);
    const QModelIndex last = index(m_builds.size() - 1, 0);
    emit dataChanged(first, last, {ActiveRole});
}

bool InstalledBuildsModel::isActive(const InstalledBuildInfo &build) const
{
    if (build.serverPath.isEmpty() || m_settings.serverPath().isEmpty())
        return false;
    return normalizedPath(build.serverPath) == normalizedPath(m_settings.serverPath());
}

QString InstalledBuildsModel::normalizedPath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

}  // namespace llocr
