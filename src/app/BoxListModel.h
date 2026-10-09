#pragma once

#include <QAbstractListModel>
#include <QSet>

#include "core/OcrResult.h"

namespace llocr {

class BoxListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        XRole = Qt::UserRole + 1,
        YRole,
        WidthRole,
        HeightRole,
        TextRole,
        LabelRole,
        CheckStatusRole,
        CorrectedRole,
        SuspectRole,
        PlannedRole,
    };

    explicit BoxListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBoxes(const QList<BoundingBox> &boxes);

    void setFromResult(const OcrResult &result);

    // The block types the verification queue would pick up (the user's filter
    // in the verification settings). A change re-evaluates PlannedRole for
    // every row.
    void setPlannedTypes(const QSet<QString> &types);

    Q_INVOKABLE void updateBoxRect(int index, qreal x, qreal y, qreal width, qreal height);
    Q_INVOKABLE void updateBoxCheck(int index, int status, const QString &correctedText);
    void removeBox(int index);
    Q_INVOKABLE bool isImageBox(int index) const;

private:
    bool isPlanned(const BoundingBox &box) const;

    QList<BoundingBox> m_boxes;
    QSet<QString> m_plannedTypes;
};

}  // namespace llocr
