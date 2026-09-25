#include "app/BlockGroupFilterModel.h"

#include <QAbstractItemModel>

namespace llocr {

namespace {
constexpr int kGroupRole = Qt::UserRole + 3;
}  // namespace

BlockGroupFilterModel::BlockGroupFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

void BlockGroupFilterModel::setGroup(const QString &group)
{
    if (m_group == group)
        return;
    beginFilterChange();
    m_group = group;
    endFilterChange();
    emit groupChanged();
}

bool BlockGroupFilterModel::filterAcceptsRow(int sourceRow,
                                             const QModelIndex &sourceParent) const
{
    if (m_group.isEmpty() || !sourceModel())
        return false;
    const QModelIndex idx = sourceModel()->index(sourceRow, 0, sourceParent);
    return sourceModel()->data(idx, kGroupRole).toString() == m_group;
}

}  // namespace llocr
