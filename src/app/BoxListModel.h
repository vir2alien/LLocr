#pragma once

#include <QAbstractListModel>

#include "core/OcrResult.h"

namespace llocr {

class BoxListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    // The overlay hue a box renders as (Theme.qml carries one per kind),
    // resolved from the label map of the model that produced the boxes.
    enum class BoxKind {
        Text = 0,
        Image = 1,
        Table = 2,
        Formula = 3,
    };

    enum Roles : int {
        XRole = Qt::UserRole + 1,
        YRole,
        WidthRole,
        HeightRole,
        TextRole,
        LabelRole,
        CheckStatusRole,
        CorrectedRole,
        CorrectedCheckStatusRole,
        KindRole,
        ConfidenceRole,
        CorrectedConfidenceRole,
    };

    explicit BoxListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setBoxes(const QList<BoundingBox> &boxes);

    void setFromResult(const OcrResult &result);

    // The label map follows the OCR/layout model the page was parsed with; an
    // empty id resolves the default vocabulary.
    void setStyleModelId(const QString &modelId);

    Q_INVOKABLE void updateBoxRect(int index, qreal x, qreal y, qreal width, qreal height);
    // Mirrors both layers of the box state (ADR 164): the text layer
    // (status, text, confidence) and the correction layer (correctedText,
    // correctedStatus, correctedConfidence).
    Q_INVOKABLE void updateBoxCheck(
        int index, int status, const QString &correctedText, const QString &text, double confidence = -1.0, int correctedStatus = int(BoxCheckStatus::NotChecked), double correctedConfidence = -1.0);
    void removeBox(int index);
    Q_INVOKABLE bool isImageBox(int index) const;

private:
    BoxKind kindFor(const BoundingBox &box) const;

    QList<BoundingBox> m_boxes;
    QString m_styleModelId;
};

}  // namespace llocr
