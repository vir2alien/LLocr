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

    // The overlay kind follows the label map (BlockStyleMap, the same
    // data-driven source the Markdown rendering uses): image/table/equation
    // labels get their own hue, everything else renders as text.
    void kindRoleFollowsLabelMap()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setBoxes({
            makeBox(QStringLiteral("text"), QRectF(0, 0, 0.1, 0.1)),
            makeBox(QStringLiteral("title"), QRectF(0, 0, 0.1, 0.1)),
            makeBox(QStringLiteral("table"), QRectF(0, 0, 0.2, 0.1)),
            makeBox(QStringLiteral("equation"), QRectF(0, 0, 0.2, 0.1)),
            makeBox(QStringLiteral("image"), QRectF(0, 0, 0.2, 0.1)),
            makeBox(QStringLiteral("chart"), QRectF(0, 0, 0.2, 0.1)),
            makeBox(QStringLiteral("unmapped-label"), QRectF(0, 0, 0.2, 0.1)),
        });

        QCOMPARE(model.data(model.index(0), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Text));
        QCOMPARE(model.data(model.index(1), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Text));
        QCOMPARE(model.data(model.index(2), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Table));
        QCOMPARE(model.data(model.index(3), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Formula));
        QCOMPARE(model.data(model.index(4), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Image));
        QCOMPARE(model.data(model.index(5), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Image));
        // An unknown label is a text block, not a crash.
        QCOMPARE(model.data(model.index(6), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Text));

        // The style model id re-resolves the map for every row.
        QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
        model.setStyleModelId(QStringLiteral("some-model"));
        QCOMPARE(model.data(model.index(0), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Text));
        QCOMPARE(spy.count(), 1);
    }

    // updateBoxCheck mirrors the document: a re-recognized variant that was
    // confirmed (Ok collapse) or a first recognition changes the text too.
    void updateBoxCheckUpdatesText()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setBoxes({makeBox(QStringLiteral("text"), QRectF(0, 0, 0.1, 0.1), QStringLiteral("original"))});

        QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
        model.updateBoxCheck(0, int(BoxCheckStatus::ReRecognized), QStringLiteral("second"), QStringLiteral("original"));
        QCOMPARE(model.data(model.index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::ReRecognized));
        QCOMPARE(model.data(model.index(0), BoxListModel::CorrectedRole).toString(), QStringLiteral("second"));
        QCOMPARE(model.data(model.index(0), BoxListModel::TextRole).toString(), QStringLiteral("original"));
        QCOMPARE(spy.count(), 1);

        // Same-value update must not emit anything.
        model.updateBoxCheck(0, int(BoxCheckStatus::ReRecognized), QStringLiteral("second"), QStringLiteral("original"));
        QCOMPARE(spy.count(), 1);

        // The decision confirmed the second variant: it becomes the text.
        model.updateBoxCheck(0, int(BoxCheckStatus::Ok), QString(), QStringLiteral("second"));
        QCOMPARE(model.data(model.index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::Ok));
        QCOMPARE(model.data(model.index(0), BoxListModel::TextRole).toString(), QStringLiteral("second"));
        QCOMPARE(model.data(model.index(0), BoxListModel::CorrectedRole).toString(), QString());

        // First recognition: text lands in the block, status stays NotChecked.
        model.setBoxes({makeBox(QStringLiteral("text"), QRectF(0, 0, 0.1, 0.1), QString())});
        model.updateBoxCheck(0, int(BoxCheckStatus::NotChecked), QString(), QStringLiteral("transcribed"));
        QCOMPARE(model.data(model.index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(model.data(model.index(0), BoxListModel::TextRole).toString(), QStringLiteral("transcribed"));
    }
};

QTEST_MAIN(TestBoxModel)
#include "test_box_model.moc"
