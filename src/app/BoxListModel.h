#pragma once

#include <QAbstractListModel>

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
    };

    explicit BoxListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBoxes(const QList<BoundingBox>& boxes);

    void setFromResult(const OcrResult& result);

    // --- Mutating helpers (used from QML for image-block editing) ---
    Q_INVOKABLE void updateBoxRect(int index, qreal x, qreal y, qreal width, qreal height);
    Q_INVOKABLE void removeBox(int index);
    Q_INVOKABLE bool isImageBox(int index) const;

signals:
    /// Emitted after a box has been removed (index refers to the old position).
    void boxRemoved(int index);

private:
    QList<BoundingBox> m_boxes;
};

} // namespace llocr
