#pragma once

#include <QHash>
#include <QString>

#include "app/DocumentModel.h"

namespace llocr {

class PageEditStore
{
public:
    enum class Change {
        None,
        NowEdited,
        NowClean,
    };

    QString effectiveText(const DocumentModel& doc, int index) const;
    Change setText(int index, const QString& original, const QString& text);
    void replace(int index, const QString& text);
    bool revert(int index);
    void clear();
    int count() const;
    bool isEdited(int index) const;
    void remapAfterRemove(int removedIndex);
    void remapAfterMove(int from, int to);

private:
    QHash<int, QString> m_edits;
};

}  // namespace llocr