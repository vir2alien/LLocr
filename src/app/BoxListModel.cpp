#include "app/BoxListModel.h"

namespace llocr {

BoxListModel::BoxListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int BoxListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_boxes.size());
}

QVariant BoxListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_boxes.size()) {
        return {};
    }

    const BoundingBox& box = m_boxes.at(index.row());
    switch (role) {
        case XRole:      return box.rect.x();
        case YRole:      return box.rect.y();
        case WidthRole:  return box.rect.width();
        case HeightRole: return box.rect.height();
        case TextRole:   return box.text;
        case LabelRole:  return box.label;
        default:         return {};
    }
}

QHash<int, QByteArray> BoxListModel::roleNames() const
{
    return {
        {XRole,      "boxX"},
        {YRole,      "boxY"},
        {WidthRole,  "boxWidth"},
        {HeightRole, "boxHeight"},
        {TextRole,   "boxText"},
        {LabelRole,  "boxLabel"},
    };
}

void BoxListModel::setBoxes(const QList<BoundingBox>& boxes) {
    beginResetModel();
    m_boxes = boxes;
    endResetModel();
}

void BoxListModel::setFromResult(const OcrResult& result)
{
    if (result.pages.isEmpty()) {
        setBoxes({});
    } else {
        // TODO check this
        setBoxes(result.pages.first().boxes);
    }
}

void BoxListModel::updateBoxRect(int index, qreal x, qreal y, qreal width, qreal height)
{
    if (index < 0 || index >= m_boxes.size())
        return;
    BoundingBox& box = m_boxes[index];
    const QRectF newRect(x, y, width, height);
    if (box.rect == newRect)
        return;
    box.rect = newRect;
    const QModelIndex mi = createIndex(index, 0);
    emit dataChanged(mi, mi, {XRole, YRole, WidthRole, HeightRole});
}

void BoxListModel::removeBox(int index)
{
    if (index < 0 || index >= m_boxes.size())
        return;
    beginRemoveRows({}, index, index);
    m_boxes.removeAt(index);
    endRemoveRows();
    emit boxRemoved(index);
}

bool BoxListModel::isImageBox(int index) const
{
    return index >= 0 && index < m_boxes.size()
        && m_boxes.at(index).label == QLatin1String("image");
}

} // namespace llocr
