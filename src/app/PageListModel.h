#pragma once

#include <QAbstractListModel>
#include <QList>

namespace llocr {

/**
 * @brief model for the left-hand page-preview panel
 *
 * No bounding boxes here
 */

class PageListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        PageIndexRole = Qt::UserRole + 1,
        RecognizedRole,
        CurrentRole,
        EditedRole,
    };

    explicit PageListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPageCount(int count);

    void setRecognized(int index, bool recognized);

    void setEdited(int index, bool edited);

    void setCurrent(int index);

    void clear();

private:
    QList<bool> m_recognized;
    QList<bool> m_edited;
    int m_current = -1;
};

} // namespace llocr
