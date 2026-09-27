#pragma once

#include <QHash>
#include <QString>

#include "app/DocumentModel.h"

namespace llocr {

class PageEditStore
{
public:
    bool reset(int index, const QString &recognizedText);
    QString baseline(int index) const;

    bool revert(int index);

    bool isEdited(int index) const;
    void setEdited(int index, bool edited);

    void clear();
    void remapAfterRemove(int removedIndex);
    void remapAfterMove(int from, int to);

private:
    QHash<int, QString> m_baselines;
    QHash<int, bool> m_edited;
};

}  // namespace llocr
