#include <QtTest>
#include <QAbstractItemModelTester>

#include "app/PageListModel.h"

using namespace llocr;

// Verification of PageListModel::movePage ordering, including the
// beginMoveRows()/endMoveRows() signalling that keeps the QML ListView
// (QQmlDelegateModel) in sync with the underlying data.
class TestPageModel : public QObject {
    Q_OBJECT

private:
    // Returns true if `recognized` is set on exactly row `row`.
    bool isRecognizedAt(const PageListModel& model, int row, bool recognized = true) const
    {
        const QModelIndex mi = model.index(row);
        return mi.isValid() && model.data(mi, PageListModel::RecognizedRole).toBool() == recognized;
    }

private slots:
    void moveDown()
    {
        PageListModel model;
        model.setPageCount(4);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::Fatal);

        // Mark page 0 as recognized so we can track it across the move.
        model.setRecognized(0, true);
        QVERIFY(isRecognizedAt(model, 0));

        // Move page 0 down to position 2.
        model.movePage(0, 2);

        QCOMPARE(model.rowCount(), 4);
        // The recognized page should now sit at index 2.
        QVERIFY(isRecognizedAt(model, 2));
        QVERIFY(isRecognizedAt(model, 0, false));
        QVERIFY(isRecognizedAt(model, 1, false));
        QVERIFY(isRecognizedAt(model, 3, false));
    }

    void moveDownByOne()
    {
        PageListModel model;
        model.setPageCount(3);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setRecognized(0, true);

        // Move page 0 down by exactly one position (edge case for beginMoveRows).
        model.movePage(0, 1);

        QCOMPARE(model.rowCount(), 3);
        QVERIFY(isRecognizedAt(model, 1));
        QVERIFY(isRecognizedAt(model, 0, false));
    }

    void moveUp()
    {
        PageListModel model;
        model.setPageCount(4);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setRecognized(3, true);

        // Move page 3 up to position 1.
        model.movePage(3, 1);

        QCOMPARE(model.rowCount(), 4);
        QVERIFY(isRecognizedAt(model, 1));
        QVERIFY(isRecognizedAt(model, 3, false));
    }

    void currentFollowsMove()
    {
        PageListModel model;
        model.setPageCount(4);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setCurrent(2);

        // Move page 0 down past the current page: pages 1..3 shift left by one,
        // so the page that was current (index 2) lands at index 1.
        model.movePage(0, 3);
        QVERIFY(isCurrent(model, 1));

        // Move page 1 (the current page) up to the top.
        model.movePage(1, 0);
        QVERIFY(isCurrent(model, 0));

        // No-op move must not change the current index.
        model.movePage(0, 0);
        QVERIFY(isCurrent(model, 0));
    }

    void renumberNotifiesAllRows()
    {
        PageListModel model;
        model.setPageCount(4);

        QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

        model.movePage(0, 2);

        // The view must be told all rows changed so the "Page N" labels refresh.
        QCOMPARE(spy.count(), 1);
        const QList<QVariant>& args = spy.at(0);
        QCOMPARE(args.at(0).value<QModelIndex>().row(), 0);
        QCOMPARE(args.at(1).value<QModelIndex>().row(), 3);

        // The pageIndex role matches the new row order (i.e. numbering is
        // positional after the move).
        for (int row = 0; row < model.rowCount(); ++row) {
            const QModelIndex mi = model.index(row);
            QCOMPARE(model.data(mi, PageListModel::PageIndexRole).toInt(), row);
        }
    }

    void appendPreservesExistingRows()
    {
        PageListModel model;
        model.setPageCount(2);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setRecognized(0, true);
        model.setEdited(1, true);
        model.setCurrent(1);

        model.appendPages(3);

        // Total rows grow by the appended count; existing state is untouched.
        QCOMPARE(model.rowCount(), 5);
        QVERIFY(isRecognizedAt(model, 0));
        QVERIFY(isRecognizedAt(model, 1, false));
        QVERIFY(isRecognizedAt(model, 2, false));
        QVERIFY(isRecognizedAt(model, 3, false));
        QVERIFY(isRecognizedAt(model, 4, false));

        QVERIFY(model.data(model.index(1), PageListModel::EditedRole).toBool());
        QVERIFY(!model.data(model.index(2), PageListModel::EditedRole).toBool());

        QVERIFY(isCurrent(model, 1));
    }

    void appendZeroIsNoOp()
    {
        PageListModel model;
        model.setPageCount(2);
        model.appendPages(0);
        QCOMPARE(model.rowCount(), 2);
    }

    bool isCurrent(const PageListModel& model, int row) const
    {
        const QModelIndex mi = model.index(row);
        return mi.isValid() && model.data(mi, PageListModel::CurrentRole).toBool();
    }
};

QTEST_MAIN(TestPageModel)
#include "test_pagemodel.moc"
