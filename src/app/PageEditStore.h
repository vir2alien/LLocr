#pragma once

#include <QHash>
#include <QString>

#include "app/DocumentModel.h"

namespace llocr {

class PageEditStore
{
public:
    bool isEdited(int index) const;
    void setEdited(int index, bool edited);

    void clear();
    void remapAfterRemove(int removedIndex);
    void remapAfterMove(int from, int to);

private:
    QHash<int, bool> m_edited;
};

}  // namespace llocr
