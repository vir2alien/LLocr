#include "app/PageListModel.h"
#include "app/PageIndex.h"

#include <algorithm>

namespace llocr {

PageListModel::PageListModel(QObject *parent) : QAbstractListModel(parent) {}

int PageListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_recognized.size();
}

QVariant PageListModel::data(const QModelIndex &index, int role) const
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
    case HasDuplicatesRole:
        return (row < m_hasDuplicates.size()) ? m_hasDuplicates.at(row) : false;
    case RecognitionModeRole:
        return (row < m_recognitionMode.size()) ? m_recognitionMode.at(row) : 0;
    case VerificationRole:
        return (row < m_verification.size()) ? m_verification.at(row) : 0;
    case SelectedRole:
        return (row < m_selected.size()) ? m_selected.at(row) : false;
    }
    return {};
}

QHash<int, QByteArray> PageListModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        {PageIndexRole, "pageIndex"},
        {RecognizedRole, "recognized"},
        {CurrentRole, "current"},
        {EditedRole, "edited"},
        {HasDuplicatesRole, "hasDuplicates"},
        {RecognitionModeRole, "pageRecognitionMode"},
        {VerificationRole, "pageVerification"},
        {SelectedRole, "selected"},
    };
    return roles;
}

void PageListModel::appendPages(int count)
{
    if (count <= 0)
        return;

    const int first = m_recognized.size();
    beginInsertRows({}, first, first + count - 1);
    m_recognized += QList<bool>(count, false);
    m_edited += QList<bool>(count, false);
    m_hasDuplicates += QList<bool>(count, false);
    m_recognitionMode += QList<int>(count, 0);
    m_verification += QList<int>(count, 0);
    m_selected += QList<bool>(count, false);
    endInsertRows();
}

void PageListModel::setRecognized(int index, bool recognized)
{
    if (index < 0 || index >= m_recognized.size())
        return;
    if (m_recognized.at(index) == recognized)
        return;
    m_recognized[index] = recognized;
    const QModelIndex mi = this->index(index);
    emit dataChanged(mi, mi, {RecognizedRole});
}

void PageListModel::setEdited(int index, bool edited)
{
    if (index < 0 || index >= m_edited.size())
        return;
    if (m_edited.at(index) == edited)
        return;
    m_edited[index] = edited;
    const QModelIndex mi = this->index(index);
    emit dataChanged(mi, mi, {EditedRole});
}

void PageListModel::setHasDuplicates(int index, bool hasDup)
{
    if (index < 0 || index >= m_hasDuplicates.size())
        return;
    if (m_hasDuplicates.at(index) == hasDup)
        return;
    m_hasDuplicates[index] = hasDup;
    const QModelIndex mi = this->index(index);
    emit dataChanged(mi, mi, {HasDuplicatesRole});
}

void PageListModel::setRecognitionMode(int index, int mode)
{
    if (index < 0 || index >= m_recognitionMode.size())
        return;
    if (m_recognitionMode.at(index) == mode)
        return;
    m_recognitionMode[index] = mode;
    const QModelIndex mi = this->index(index);
    emit dataChanged(mi, mi, {RecognitionModeRole});
}

void PageListModel::setVerification(int index, int verification)
{
    if (index < 0 || index >= m_verification.size())
        return;
    if (m_verification.at(index) == verification)
        return;
    m_verification[index] = verification;
    const QModelIndex mi = this->index(index);
    emit dataChanged(mi, mi, {VerificationRole});
}

void PageListModel::setCurrent(int index)
{
    if (index < -1 || index >= m_recognized.size())
        return;
    if (index == m_current)
        return;
    const int previous = m_current;
    m_current = index;

    if (previous >= 0 && previous < m_recognized.size()) {
        const QModelIndex mi = this->index(previous);
        emit dataChanged(mi, mi, {CurrentRole});
    }
    if (m_current >= 0 && m_current < m_recognized.size()) {
        const QModelIndex mi = this->index(m_current);
        emit dataChanged(mi, mi, {CurrentRole});
    }
}

void PageListModel::removePage(int index)
{
    if (index < 0 || index >= m_recognized.size())
        return;
    beginRemoveRows({}, index, index);
    m_recognized.removeAt(index);
    m_edited.removeAt(index);
    m_hasDuplicates.removeAt(index);
    m_recognitionMode.removeAt(index);
    m_verification.removeAt(index);
    const bool wasSelected = (index < m_selected.size()) ? m_selected.takeAt(index) : false;
    if (m_current == index)
        m_current = -1;
    else if (m_current > index)
        --m_current;
    endRemoveRows();

    if (index < m_recognized.size())
        emit dataChanged(this->index(index), this->index(m_recognized.size() - 1));
    if (wasSelected)
        emit selectionChanged();
}

void PageListModel::movePage(int from, int to)
{
    if (from < 0 || from >= m_recognized.size())
        return;
    if (to < 0 || to >= m_recognized.size())
        return;
    if (from == to)
        return;

    const int destChild = (to > from) ? to + 1 : to;
    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), destChild))
        return;

    m_recognized.move(from, to);
    m_edited.move(from, to);
    m_hasDuplicates.move(from, to);
    m_recognitionMode.move(from, to);
    m_verification.move(from, to);
    m_selected.move(from, to);

    m_current = remapIndexAfterMove(m_current, from, to);

    endMoveRows();

    if (m_recognized.size() > 0)
        emit dataChanged(index(0), index(m_recognized.size() - 1));
}

void PageListModel::clear()
{
    beginResetModel();
    m_recognized.clear();
    m_edited.clear();
    m_hasDuplicates.clear();
    m_recognitionMode.clear();
    m_verification.clear();
    m_selected.clear();
    m_current = -1;
    endResetModel();
    emit selectionChanged();
}

void PageListModel::selectSingle(int index)
{
    if (index < 0 || index >= m_recognized.size())
        return;

    bool changed = false;
    for (int row = 0; row < m_selected.size(); ++row) {
        const bool nowSelected = (row == index);
        if (m_selected.at(row) == nowSelected)
            continue;
        m_selected[row] = nowSelected;
        const QModelIndex mi = this->index(row);
        emit dataChanged(mi, mi, {SelectedRole});
        changed = true;
    }
    if (changed)
        emit selectionChanged();
}

void PageListModel::setSelected(int index, bool selected)
{
    if (index < 0 || index >= m_selected.size())
        return;
    if (m_selected.at(index) == selected)
        return;
    m_selected[index] = selected;
    const QModelIndex mi = this->index(index);
    emit dataChanged(mi, mi, {SelectedRole});
    emit selectionChanged();
}

void PageListModel::selectAll()
{
    bool changed = false;
    for (int row = 0; row < m_selected.size(); ++row) {
        if (m_selected.at(row))
            continue;
        m_selected[row] = true;
        const QModelIndex mi = this->index(row);
        emit dataChanged(mi, mi, {SelectedRole});
        changed = true;
    }
    if (changed)
        emit selectionChanged();
}

void PageListModel::clearSelection()
{
    bool changed = false;
    for (int row = 0; row < m_selected.size(); ++row) {
        if (!m_selected.at(row))
            continue;
        m_selected[row] = false;
        const QModelIndex mi = this->index(row);
        emit dataChanged(mi, mi, {SelectedRole});
        changed = true;
    }
    if (changed)
        emit selectionChanged();
}

int PageListModel::selectedCount() const
{
    return static_cast<int>(std::count(m_selected.cbegin(), m_selected.cend(), true));
}

bool PageListModel::isSelected(int index) const
{
    return index >= 0 && index < m_selected.size() && m_selected.at(index);
}

}  // namespace llocr
