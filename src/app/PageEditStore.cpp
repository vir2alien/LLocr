#include "app/PageEditStore.h"

#include "app/PageIndex.h"

namespace llocr {

bool PageEditStore::isEdited(int index) const
{
    return m_edited.value(index, false);
}

void PageEditStore::setEdited(int index, bool edited)
{
    if (edited)
        m_edited.insert(index, true);
    else
        m_edited.remove(index);
}

void PageEditStore::clear()
{
    m_edited.clear();
}

void PageEditStore::remapAfterRemove(int removedIndex)
{
    m_edited = remapHashAfterRemove(m_edited, removedIndex);
}

void PageEditStore::remapAfterMove(int from, int to)
{
    m_edited = remapHashAfterMove(m_edited, from, to);
}

}  // namespace llocr
