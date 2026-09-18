#include <QtTest>
#include <QAbstractItemModelTester>

#include "app/BoxListModel.h"
#include "core/OcrResult.h"

#include <QSignalSpy>

using namespace llocr;

namespace {

BoundingBox makeBox(const QString& label, const QRectF& rect,
                    const QString& text = QStringLiteral("t"))
{
    BoundingBox box;
    box.label = label;
    box.rect = rect;
    box.text = text;
    return box;
}

}  // namespace

// QAbstractItemModelTester coverage for BoxListModel (I-02): the model drives
// the ImagePreview box overlay and must keep its signals consistent across
// resets, rect updates and removals.
class TestBoxModel : public QObject {
    Q_OBJECT

private slots:
    void setBoxesResetsAndExposesRoles()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::Fatal);

        const QList<BoundingBox> boxes = {
            makeBox(QStringLiteral("text"), QRectF(0.1, 0.1, 0.3, 0.2)),
            makeBox(QStringLiteral("image"), QRectF(0.4, 0.4, 0.5, 0.5)),
        };
        model.setBoxes(boxes);

        QCOMPARE(model.rowCount(), 2);
        const QModelIndex mi = model.index(0);
        QCOMPARE(model.data(mi, BoxListModel::XRole).toDouble(), 0.1);
        QCOMPARE(model.data(mi, BoxListModel::YRole).toDouble(), 0.1);
        QCOMPARE(model.data(mi, BoxListModel::WidthRole).toDouble(), 0.3);
        QCOMPARE(model.data(mi, BoxListModel::HeightRole).toDouble(), 0.2);
        QCOMPARE(model.data(mi, BoxListModel::LabelRole).toString(), QStringLiteral("text"));
        QVERIFY(!model.isImageBox(0));
        QVERIFY(model.isImageBox(1));
        QVERIFY(!model.isImageBox(-1));
        QVERIFY(!model.isImageBox(99));
    }

    void updateBoxRectEmitsDataChanged()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setBoxes({makeBox(QStringLiteral("text"), QRectF(0, 0, 0.1, 0.1))});

        QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
        model.updateBoxRect(0, 0.2, 0.3, 0.4, 0.5);

        QCOMPARE(spy.count(), 1);
        const QModelIndex mi = model.index(0);
        QCOMPARE(model.data(mi, BoxListModel::XRole).toDouble(), 0.2);
        QCOMPARE(model.data(mi, BoxListModel::YRole).toDouble(), 0.3);
        QCOMPARE(model.data(mi, BoxListModel::WidthRole).toDouble(), 0.4);
        QCOMPARE(model.data(mi, BoxListModel::HeightRole).toDouble(), 0.5);

        // Same-value update must not emit anything.
        model.updateBoxRect(0, 0.2, 0.3, 0.4, 0.5);
        QCOMPARE(spy.count(), 1);

        // Out-of-range update must not emit or crash.
        model.updateBoxRect(7, 0.2, 0.3, 0.4, 0.5);
        QCOMPARE(spy.count(), 1);
    }

    void updateBoxTextEmitsDataChanged()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setBoxes({makeBox(QStringLiteral("text"), QRectF(0, 0, 0.1, 0.1),
                                QStringLiteral("before"))});

        QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
        model.updateBoxText(0, QStringLiteral("after"));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(model.data(model.index(0), BoxListModel::TextRole).toString(),
                 QStringLiteral("after"));

        // Same-value update must not emit anything.
        model.updateBoxText(0, QStringLiteral("after"));
        QCOMPARE(spy.count(), 1);

        // Out-of-range update must not emit or crash.
        model.updateBoxText(7, QStringLiteral("x"));
        QCOMPARE(spy.count(), 1);
    }

    void removeBoxRemovesRowAndReports()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setBoxes({
            makeBox(QStringLiteral("text"), QRectF(0, 0, 0.1, 0.1)),
            makeBox(QStringLiteral("image"), QRectF(0, 0, 0.2, 0.2)),
            makeBox(QStringLiteral("chart"), QRectF(0, 0, 0.3, 0.3)),
        });

        QSignalSpy removedSpy(&model, &BoxListModel::boxRemoved);
        QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);

        model.removeBox(1);

        QCOMPARE(removedSpy.count(), 1);
        QCOMPARE(removedSpy.at(0).at(0).toInt(), 1);
        QCOMPARE(removeSpy.count(), 1);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(1), BoxListModel::LabelRole).toString(),
                 QStringLiteral("chart"));
        QVERIFY(model.isImageBox(1));

        // Out-of-range removal is a no-op.
        model.removeBox(5);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(removedSpy.count(), 1);
    }

    void setFromResultTakesFirstPage()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::Fatal);

        OcrResult result;
        OcrPage page;
        page.boxes.append(makeBox(QStringLiteral("text"), QRectF(0, 0, 0.1, 0.1)));
        result.pages.append(page);

        model.setFromResult(result);
        QCOMPARE(model.rowCount(), 1);

        OcrResult empty;
        model.setFromResult(empty);
        QCOMPARE(model.rowCount(), 0);
    }
};

QTEST_MAIN(TestBoxModel)
#include "test_box_model.moc"
