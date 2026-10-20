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
            makeBox(QStringLiteral("header"), QRectF(0, 0, 0.2, 0.1)),
            makeBox(QStringLiteral("footer"), QRectF(0, 0, 0.2, 0.1)),
            makeBox(QStringLiteral("page_number"), QRectF(0, 0, 0.2, 0.1)),
            makeBox(QStringLiteral("page_footer"), QRectF(0, 0, 0.2, 0.1)),
        });

        QCOMPARE(model.data(model.index(0), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Text));
        QCOMPARE(model.data(model.index(1), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Text));
        QCOMPARE(model.data(model.index(2), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Table));
        QCOMPARE(model.data(model.index(3), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Formula));
        QCOMPARE(model.data(model.index(4), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Image));
        QCOMPARE(model.data(model.index(5), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Image));
        // An unknown label is a text block, not a crash.
        QCOMPARE(model.data(model.index(6), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Text));
        // The page furniture is its own kind: it renders grey, not as text.
        QCOMPARE(model.data(model.index(7), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Service));
        QCOMPARE(model.data(model.index(8), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Service));
        QCOMPARE(model.data(model.index(9), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Service));
        QCOMPARE(model.data(model.index(10), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Service));

        // The style model id re-resolves the map for every row.
        QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
        model.setStyleModelId(QStringLiteral("some-model"));
        QCOMPARE(model.data(model.index(0), BoxListModel::KindRole).toInt(), int(BoxListModel::BoxKind::Text));
        QCOMPARE(spy.count(), 1);
    }

    // updateBoxCheck mirrors the document's two layers (ADR 164): the text
    // layer (status, text, confidence) and the correction layer (correctedText,
    // its own verdict and confidence) travel together in one update.
    void updateBoxCheckMirrorsBothLayers()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setBoxes({makeBox(QStringLiteral("text"), QRectF(0, 0, 0.1, 0.1), QStringLiteral("original"))});

        QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
        // A corrected block: the rejected text keeps its verdict, the fix is
        // stored without one.
        model.updateBoxCheck(0, int(BoxCheckStatus::Mismatch), QStringLiteral("corrected"), QStringLiteral("original"), 0.2);
        QCOMPARE(model.data(model.index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::Mismatch));
        QCOMPARE(model.data(model.index(0), BoxListModel::TextRole).toString(), QStringLiteral("original"));
        QCOMPARE(model.data(model.index(0), BoxListModel::ConfidenceRole).toDouble(), 0.2);
        QCOMPARE(model.data(model.index(0), BoxListModel::CorrectedRole).toString(), QStringLiteral("corrected"));
        QCOMPARE(model.data(model.index(0), BoxListModel::CorrectedCheckStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(model.data(model.index(0), BoxListModel::CorrectedConfidenceRole).toDouble(), -1.0);
        QCOMPARE(spy.count(), 1);

        // Same-value update must not emit anything.
        model.updateBoxCheck(0, int(BoxCheckStatus::Mismatch), QStringLiteral("corrected"), QStringLiteral("original"), 0.2);
        QCOMPARE(spy.count(), 1);

        // The decision confirms the fix: the verdict lands on the correction
        // layer, the OCR text stays untouched (no promotion).
        model.updateBoxCheck(0, int(BoxCheckStatus::Mismatch), QStringLiteral("corrected"), QStringLiteral("original"), 0.2, int(BoxCheckStatus::Ok), 0.97);
        QCOMPARE(model.data(model.index(0), BoxListModel::CorrectedCheckStatusRole).toInt(), int(BoxCheckStatus::Ok));
        QCOMPARE(model.data(model.index(0), BoxListModel::CorrectedConfidenceRole).toDouble(), 0.97);
        QCOMPARE(model.data(model.index(0), BoxListModel::TextRole).toString(), QStringLiteral("original"));
        QCOMPARE(spy.count(), 2);

        // A re-recognition erases the correction layer: the fresh text is the
        // block text and no verdict survives.
        model.updateBoxCheck(0, int(BoxCheckStatus::NotChecked), QString(), QStringLiteral("fresh"));
        QCOMPARE(model.data(model.index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(model.data(model.index(0), BoxListModel::TextRole).toString(), QStringLiteral("fresh"));
        QCOMPARE(model.data(model.index(0), BoxListModel::CorrectedRole).toString(), QString());
        QCOMPARE(model.data(model.index(0), BoxListModel::CorrectedCheckStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(model.data(model.index(0), BoxListModel::ConfidenceRole).toDouble(), -1.0);
        QCOMPARE(spy.count(), 3);
    }

    // The QML entry point must pass through the same int->enum gate as the
    // project load: an out-of-range value (or the reserved 2 of v1 projects)
    // coerces to NotChecked instead of reaching the model and later
    // project.json.
    void updateBoxCheckClampsOutOfRangeStatus()
    {
        BoxListModel model;
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setBoxes({makeBox(QStringLiteral("text"), QRectF(0, 0, 0.1, 0.1), QStringLiteral("original"))});

        model.updateBoxCheck(0, 99, QStringLiteral("bogus"), QStringLiteral("text"));
        QCOMPARE(model.data(model.index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
        QCOMPARE(model.data(model.index(0), BoxListModel::CorrectedRole).toString(), QStringLiteral("bogus"));
        model.updateBoxCheck(0, -3, QString(), QStringLiteral("text"));
        QCOMPARE(model.data(model.index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
        model.updateBoxCheck(0, 2, QString(), QStringLiteral("text"));
        QCOMPARE(model.data(model.index(0), BoxListModel::CheckStatusRole).toInt(), int(BoxCheckStatus::NotChecked));
    }
};

QTEST_MAIN(TestBoxModel)
#include "test_box_model.moc"
