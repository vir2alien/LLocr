#include <QAbstractItemModelTester>
#include <QtTest>

#include "app/BoxListModel.h"
#include "core/OcrResult.h"

#include <QSignalSpy>

using namespace llocr;

namespace {

BoundingBox makeBox(const QString &label, const QRectF &rect, const QString &text = QStringLiteral("t"))
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
class TestBoxModel : public QObject
{
    Q_OBJECT

private slots:
    void setBoxesResetsAndExposesRoles()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

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
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

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

    void removeBoxRemovesRow()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setBoxes({
            makeBox(QStringLiteral("text"), QRectF(0, 0, 0.1, 0.1)),
            makeBox(QStringLiteral("image"), QRectF(0, 0, 0.2, 0.2)),
            makeBox(QStringLiteral("chart"), QRectF(0, 0, 0.3, 0.3)),
        });

        QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);

        model.removeBox(1);

        QCOMPARE(removeSpy.count(), 1);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(1), BoxListModel::LabelRole).toString(), QStringLiteral("chart"));
        QVERIFY(model.isImageBox(1));

        // Out-of-range removal is a no-op.
        model.removeBox(5);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(removeSpy.count(), 1);
    }

    // The view model must not be a QML-callable mutation point: it mirrors the
    // document, which AppController owns. removeBox() stays deliberately silent
    // (no boxRemoved signal) so a controller-driven removal cannot re-enter.
    void removeBoxIsNotQmlCallable()
    {
        const QMetaObject *mo = &BoxListModel::staticMetaObject;
        QVERIFY(mo->indexOfMethod("removeBox(Q_ARG(int,int)") < 0);
        QVERIFY(mo->indexOfMethod("removeBox(int)") < 0);
        QCOMPARE(mo->indexOfSignal("boxRemoved(int)"), -1);
    }

    void setFromResultTakesFirstPage()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

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

    // The gray "planned" dot mirrors what the verification queue would pick
    // up: a text block whose type the filter enables (or a duplicate suspect)
    // with a non-empty text and no answer yet.
    void plannedRoleFollowsFilterAndStatus()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        BoundingBox suspect = makeBox(QStringLiteral("text"), QRectF(0, 0, 0.2, 0.1), QStringLiteral("dup"));
        suspect.duplicateSuspect = true;
        BoundingBox empty = makeBox(QStringLiteral("table"), QRectF(0, 0, 0.2, 0.1), QString());
        model.setBoxes({
            makeBox(QStringLiteral("text"), QRectF(0, 0, 0.1, 0.1)),
            makeBox(QStringLiteral("table"), QRectF(0, 0, 0.15, 0.1)),
            suspect,
            empty,
        });

        // Without the filter only the duplicate suspect is planned: it is
        // always asked regardless of the type list.
        QCOMPARE(model.data(model.index(0), BoxListModel::PlannedRole).toBool(), false);
        QCOMPARE(model.data(model.index(2), BoxListModel::PlannedRole).toBool(), true);

        model.setPlannedTypes({QStringLiteral("text")});
        QCOMPARE(model.data(model.index(0), BoxListModel::PlannedRole).toBool(), true);
        QCOMPARE(model.data(model.index(1), BoxListModel::PlannedRole).toBool(), false);
        // A duplicate suspect is planned regardless of the filter.
        QCOMPARE(model.data(model.index(2), BoxListModel::PlannedRole).toBool(), true);
        // An empty block has nothing to verify.
        QCOMPARE(model.data(model.index(3), BoxListModel::PlannedRole).toBool(), false);

        // An answered block loses the dot.
        model.updateBoxCheck(0, int(BoxCheckStatus::Ok), QString());
        QCOMPARE(model.data(model.index(0), BoxListModel::PlannedRole).toBool(), false);
        model.updateBoxCheck(0, int(BoxCheckStatus::NotChecked), QString());
        QCOMPARE(model.data(model.index(0), BoxListModel::PlannedRole).toBool(), true);

        // Switching the filter off re-evaluates every row, suspects included.
        model.setPlannedTypes({QStringLiteral("table")});
        QCOMPARE(model.data(model.index(0), BoxListModel::PlannedRole).toBool(), false);
        QCOMPARE(model.data(model.index(1), BoxListModel::PlannedRole).toBool(), true);
        QCOMPARE(model.data(model.index(2), BoxListModel::PlannedRole).toBool(), true);

        // A setBoxes reset keeps the filter: fresh results are planned too.
        model.setBoxes({makeBox(QStringLiteral("table"), QRectF(0, 0, 0.1, 0.1))});
        QCOMPARE(model.data(model.index(0), BoxListModel::PlannedRole).toBool(), true);
    }
};

QTEST_MAIN(TestBoxModel)
#include "test_box_model.moc"
