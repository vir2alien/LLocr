#include "app/PageListModel.h"

namespace llocr {

PageListModel::PageListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PageListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_recognized.size();
}

QVariant PageListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_recognized.size())
        return {};

    const int row = index.row();
    switch (role) {
    case PageIndexRole:
        return row;
    case RecognizedRole:
        return m_recognized.at(row);
    case CurrentRole:
        return row == m_current;
    case EditedRole:
        return (row < m_edited.size()) ? m_edited.at(row) : false;
    default:
        return {};
    }
}

QHash<int, QByteArray> PageListModel::roleNames() const
{
    return {
        { PageIndexRole,  "pageIndex" },
        { RecognizedRole, "recognized" },
        { CurrentRole,    "current" },
        { EditedRole,     "edited" },
    };
}

void PageListModel::setPageCount(int count)
{
    beginResetModel();
    m_recognized = QList<bool>(count, false);
    m_edited = QList<bool>(count, false);
    m_current = count > 0 ? 0 : -1;
    endResetModel();
}

void PageListModel::setRecognized(int index, bool recognized)
{
    if (index < 0 || index >= m_recognized.size())
        return;
    if (m_recognized.at(index) == recognized)
        return;
    m_recognized[index] = recognized;
    const QModelIndex mi = this->index(index);
    emit dataChanged(mi, mi, { RecognizedRole });
}

void PageListModel::setEdited(int index, bool edited)
{
    if (index < 0 || index >= m_edited.size())
        return;
    if (m_edited.at(index) == edited)
        return;
    m_edited[index] = edited;
    const QModelIndex mi = this->index(index);
    emit dataChanged(mi, mi, { EditedRole });
}

void PageListModel::setCurrent(int index)
{
    if (index == m_current)
        return;
    const int previous = m_current;
    m_current = index;

    if (previous >= 0 && previous < m_recognized.size()) {
        const QModelIndex mi = this->index(previous);
        emit dataChanged(mi, mi, { CurrentRole });
    }
    if (m_current >= 0 && m_current < m_recognized.size()) {
        const QModelIndex mi = this->index(m_current);
        emit dataChanged(mi, mi, { CurrentRole });
    }
}

void PageListModel::removePage(int index)
{
    if (index < 0 || index >= m_recognized.size())
        return;
    beginRemoveRows({}, index, index);
    m_recognized.removeAt(index);
    m_edited.removeAt(index);
    if (m_current == index)
        m_current = -1;
    else if (m_current > index)
        --m_current;
    endRemoveRows();

    if (index < m_recognized.size())
        emit dataChanged(this->index(index), this->index(m_recognized.size() - 1));
}

void PageListModel::movePage(int from, int to)
{
    if (from < 0 || from >= m_recognized.size())
        return;
    if (to < 0 || to >= m_recognized.size())
        return;
    if (from == to)
        return;

    // Within the same parent, moving down requires destinationChild = to + 1
    // so that the moved row actually ends up at index `to`.
    const int destChild = (to > from) ? to + 1 : to;
    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), destChild))
        return;

    m_recognized.move(from, to);
    m_edited.move(from, to);

    if (m_current == from)
        m_current = to;
    else if (from < to && m_current > from && m_current <= to)
        --m_current;
    else if (from > to && m_current >= to && m_current < from)
        ++m_current;

    endMoveRows();

    // Force every delegate to re-read its roles. After a row move the reused
    // delegates (and their position-derived bindings, e.g. the "Page N" label
    // built from pageIndex) are not guaranteed to refresh on their own, so
    // re-notify all rows to keep the numbering in sync with the new order.
    if (m_recognized.size() > 0)
        emit dataChanged(index(0), index(m_recognized.size() - 1));
}

void PageListModel::clear()
{
    beginResetModel();
    m_recognized.clear();
    m_edited.clear();
    m_current = -1;
    endResetModel();
}

} // namespace llocr
