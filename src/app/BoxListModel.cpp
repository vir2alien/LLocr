#include "app/BoxListModel.h"

#include "parsers/BlockStyle.h"

namespace llocr {

namespace {

// The page furniture: running heads, running feet, page numbers and seals.
// They carry no content, so the overlay draws them grey instead of the text
// hue. The shipped vocabularies spell them header/footer/page_number
// (LFM says page_header/page_footer).
bool isServiceLabel(const QString &label)
{
    return label == QLatin1String("header") || label == QLatin1String("footer") || label == QLatin1String("page_header") || label == QLatin1String("page_footer") ||
           label == QLatin1String("page_number") || label == QLatin1String("seal");
}

}  // namespace

BoxListModel::BoxListModel(QObject *parent) : QAbstractListModel(parent) {}

int BoxListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_boxes.size());
}

QVariant BoxListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_boxes.size()) {
        return {};
    }

    const BoundingBox &box = m_boxes.at(index.row());
    switch (role) {
    case XRole:
        return box.rect.x();
    case YRole:
        return box.rect.y();
    case WidthRole:
        return box.rect.width();
    case HeightRole:
        return box.rect.height();
    case TextRole:
        return box.text;
    case LabelRole:
        return box.label;
    case CheckStatusRole:
        return static_cast<int>(box.checkStatus);
    case CorrectedRole:
        return box.correctedText;
    case CorrectedCheckStatusRole:
        return static_cast<int>(box.correctedCheckStatus);
    case KindRole:
        return static_cast<int>(kindFor(box));
    case ConfidenceRole:
        return box.confidence;
    case CorrectedConfidenceRole:
        return box.correctedConfidence;
    case FixStatusRole:
        return static_cast<int>(box.fixStatus);
    case UseCorrectedRole:
        return box.useCorrectedText;
    }
    return {};
}

QHash<int, QByteArray> BoxListModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        {XRole, "boxX"},
        {YRole, "boxY"},
        {WidthRole, "boxWidth"},
        {HeightRole, "boxHeight"},
        {TextRole, "boxText"},
        {LabelRole, "boxLabel"},
        {CheckStatusRole, "boxCheckStatus"},
        {CorrectedRole, "boxCorrectedText"},
        {CorrectedCheckStatusRole, "boxCorrectedCheckStatus"},
        {KindRole, "boxKind"},
        {ConfidenceRole, "boxConfidence"},
        {CorrectedConfidenceRole, "boxCorrectedConfidence"},
        {FixStatusRole, "boxFixStatus"},
        {UseCorrectedRole, "boxUseCorrectedText"},
    };
    return roles;
}

BoxListModel::BoxKind BoxListModel::kindFor(const BoundingBox &box) const
{
    switch (blockStyleForLabel(box.label, m_styleModelId).style) {
    case BlockStyle::ImagePlaceholder:
        return BoxKind::Image;
    case BlockStyle::Table:
        return BoxKind::Table;
    case BlockStyle::Equation:
        return BoxKind::Formula;
    default:
        return isServiceLabel(box.label) ? BoxKind::Service : BoxKind::Text;
    }
}

void BoxListModel::setBoxes(const QList<BoundingBox> &boxes)
{
    beginResetModel();
    m_boxes = boxes;
    endResetModel();
}

void BoxListModel::setFromResult(const OcrResult &result)
{
    if (result.pages.isEmpty()) {
        setBoxes({});
    } else {
        setBoxes(result.pages.first().boxes);
    }
}

void BoxListModel::setStyleModelId(const QString &modelId)
{
    if (m_styleModelId == modelId)
        return;
    m_styleModelId = modelId;
    if (m_boxes.isEmpty())
        return;
    emit dataChanged(index(0), index(m_boxes.size() - 1), {KindRole});
}

void BoxListModel::updateBoxRect(int index, qreal x, qreal y, qreal width, qreal height)
{
    if (index < 0 || index >= m_boxes.size())
        return;
    BoundingBox &box = m_boxes[index];
    const QRectF newRect(x, y, width, height);
    if (box.rect == newRect)
        return;
    box.rect = newRect;
    const QModelIndex mi = createIndex(index, 0);
    emit dataChanged(mi, mi, {XRole, YRole, WidthRole, HeightRole});
}

void BoxListModel::updateBoxState(int index, const BoundingBox &box)
{
    if (index < 0 || index >= m_boxes.size())
        return;
    BoundingBox &current = m_boxes[index];
    const bool changed = current.checkStatus != box.checkStatus || current.correctedText != box.correctedText || current.text != box.text || current.confidence != box.confidence ||
                         current.correctedCheckStatus != box.correctedCheckStatus || current.correctedConfidence != box.correctedConfidence || current.fixStatus != box.fixStatus ||
                         current.useCorrectedText != box.useCorrectedText;
    if (!changed)
        return;
    current.checkStatus = box.checkStatus;
    current.correctedText = box.correctedText;
    current.text = box.text;
    current.confidence = box.confidence;
    current.correctedCheckStatus = box.correctedCheckStatus;
    current.correctedConfidence = box.correctedConfidence;
    current.fixStatus = box.fixStatus;
    current.useCorrectedText = box.useCorrectedText;
    const QModelIndex mi = createIndex(index, 0);
    emit dataChanged(mi, mi, {CheckStatusRole, CorrectedRole, CorrectedCheckStatusRole, TextRole, ConfidenceRole, CorrectedConfidenceRole, FixStatusRole, UseCorrectedRole});
}

void BoxListModel::removeBox(int index)
{
    if (index < 0 || index >= m_boxes.size())
        return;
    beginRemoveRows({}, index, index);
    m_boxes.removeAt(index);
    endRemoveRows();
}

// The picture labels of the shipped vocabularies (the MinerU layout model).
// Rendering itself stays data-driven through BlockStyleMap; this only decides
// which blocks the UI treats as images (no text editing, canvas handles).
bool BoxListModel::isImageBox(int index) const
{
    if (index < 0 || index >= m_boxes.size())
        return false;
    const QString &label = m_boxes.at(index).label;
    return label == QLatin1String("image") || label == QLatin1String("chart");
}

}  // namespace llocr
