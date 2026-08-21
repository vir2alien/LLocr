#include "app/PageEditStore.h"

#include "app/PageIndex.h"

namespace llocr {

QString PageEditStore::effectiveText(const DocumentModel& doc, int index) const
{
    if (!doc.isValidIndex(index))
        return {};
    const DocumentPage& page = doc.page(index);
    if (!page.recognized)
        return {};
    const auto it = m_edits.constFind(index);
    if (it != m_edits.constEnd())
        return it.value();
    return page.result.text;
}

PageEditStore::Change PageEditStore::setText(int index, const QString& original, const QString& text)
{
    if (text == original) {
        if (m_edits.remove(index) > 0)
            return Change::NowClean;
        return Change::None;
    }
    const bool wasEdited = m_edits.contains(index);
    m_edits.insert(index, text);
    return wasEdited ? Change::None : Change::NowEdited;
}

void PageEditStore::replace(int index, const QString& text)
{
    m_edits.insert(index, text);
}

bool PageEditStore::revert(int index)
{
    return m_edits.remove(index) > 0;
}

void PageEditStore::clear()
{
    m_edits.clear();
}

bool PageEditStore::isEdited(int index) const
{
    return m_edits.contains(index);
}

void PageEditStore::remapAfterRemove(int removedIndex)
{
    QHash<int, QString> shifted;
    shifted.reserve(m_edits.size());
    for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it) {
        if (it.key() == removedIndex)
            continue;
        shifted.insert(it.key() > removedIndex ? it.key() - 1 : it.key(), it.value());
    }
    m_edits = shifted;
}

void PageEditStore::remapAfterMove(int from, int to)
{
    QHash<int, QString> shifted;
    shifted.reserve(m_edits.size());
    for (auto it = m_edits.constBegin(); it != m_edits.constEnd(); ++it)
        shifted.insert(remapIndexAfterMove(it.key(), from, to), it.value());
    m_edits = shifted;
}

}  // namespace llocr