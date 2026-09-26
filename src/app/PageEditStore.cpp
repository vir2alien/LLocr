#include "app/PageEditStore.h"

#include "app/PageIndex.h"

namespace llocr {

bool PageEditStore::reset(int index, const QString &recognizedText)
{
    m_baselines.insert(index, recognizedText);
    return m_edited.remove(index);
}

QString PageEditStore::baseline(int index) const
{
    return m_baselines.value(index);
}

bool PageEditStore::revert(int index)
{
    m_baselines.remove(index);
    return m_edited.remove(index);
}

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
    m_baselines.clear();
    m_edited.clear();
}

void PageEditStore::remapAfterRemove(int removedIndex)
{
    m_baselines = remapHashAfterRemove(m_baselines, removedIndex);
    m_edited = remapHashAfterRemove(m_edited, removedIndex);
}

void PageEditStore::remapAfterMove(int from, int to)
{
    m_baselines = remapHashAfterMove(m_baselines, from, to);
    m_edited = remapHashAfterMove(m_edited, from, to);
}

}  // namespace llocr
