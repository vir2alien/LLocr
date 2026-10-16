#include <QAbstractItemModelTester>
#include <QtTest>

#include "app/PageListModel.h"

using namespace llocr;

// Verification of PageListModel: move ordering, selection and the page-range
// field, including the beginMoveRows()/endMoveRows() signalling that keeps
// the QML ListView (QQmlDelegateModel) in sync with the underlying data.
class TestPageModel : public QObject
{
    Q_OBJECT

private:
    // Returns true if `recognized` is set on exactly row `row`.
    bool isRecognizedAt(const PageListModel &model, int row, bool recognized = true) const
    {
        const QModelIndex mi = model.index(row);
        return mi.isValid() && model.data(mi, PageListModel::RecognizedRole).toBool() == recognized;
    }

private slots:
    void moveDown()
    {
        PageListModel model;
        model.appendPages(4);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        // Mark page 0 as recognized so we can track it across the move.
        model.setRecognized(0, true);
        QVERIFY(isRecognizedAt(model, 0));

        model.movePage(0, 2);

        QCOMPARE(model.rowCount(), 4);
        QVERIFY(isRecognizedAt(model, 2));
        QVERIFY(isRecognizedAt(model, 0, false));
        QVERIFY(isRecognizedAt(model, 1, false));
        QVERIFY(isRecognizedAt(model, 3, false));
    }

    void moveDownByOne()
    {
        PageListModel model;
        model.appendPages(3);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

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
        model.appendPages(4);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setRecognized(3, true);

        model.movePage(3, 1);

        QCOMPARE(model.rowCount(), 4);
        QVERIFY(isRecognizedAt(model, 1));
        QVERIFY(isRecognizedAt(model, 3, false));
    }

    void currentFollowsMove()
    {
        PageListModel model;
        model.appendPages(4);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setCurrent(2);

        // Move page 0 down past the current page: pages 1..3 shift left by one,
        // so the page that was current (index 2) lands at index 1.
        model.movePage(0, 3);
        QVERIFY(isCurrent(model, 1));

        model.movePage(1, 0);
        QVERIFY(isCurrent(model, 0));

        // No-op move must not change the current index.
        model.movePage(0, 0);
        QVERIFY(isCurrent(model, 0));
    }

    void renumberNotifiesAllRows()
    {
        PageListModel model;
        model.appendPages(4);

        QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

        model.movePage(0, 2);

        // The view must be told all rows changed so the "Page N" labels refresh.
        QCOMPARE(spy.count(), 1);
        const QList<QVariant> &args = spy.at(0);
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
        model.appendPages(2);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

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
        model.appendPages(2);
        model.appendPages(0);
        QCOMPARE(model.rowCount(), 2);
    }

    void setCurrentGuardsRange()
    {
        PageListModel model;
        model.appendPages(3);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.setCurrent(1);
        QVERIFY(isCurrent(model, 1));

        // Out-of-range indices must be rejected, leaving the current row put.
        model.setCurrent(-5);
        QVERIFY(isCurrent(model, 1));

        model.setCurrent(99);
        QVERIFY(isCurrent(model, 1));

        // -1 is the documented "no current" sentinel.
        model.setCurrent(-1);
        for (int row = 0; row < model.rowCount(); ++row)
            QVERIFY(!isCurrent(model, row));

        // And a valid selection still works after the sentinel.
        model.setCurrent(2);
        QVERIFY(isCurrent(model, 2));
    }

    bool isCurrent(const PageListModel &model, int row) const
    {
        const QModelIndex mi = model.index(row);
        return mi.isValid() && model.data(mi, PageListModel::CurrentRole).toBool();
    }

    bool isSelectedAt(const PageListModel &model, int row, bool selected = true) const
    {
        const QModelIndex mi = model.index(row);
        return mi.isValid() && model.data(mi, PageListModel::SelectedRole).toBool() == selected;
    }

    void selectionBasics()
    {
        PageListModel model;
        model.appendPages(4);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        // A fresh list selects nothing: the scope is the whole document.
        QCOMPARE(model.selectedCount(), 0);
        QVERIFY(!model.isSelected(0));

        QSignalSpy spy(&model, &PageListModel::selectionChanged);

        model.selectSingle(2);
        QCOMPARE(model.selectedCount(), 1);
        QVERIFY(isSelectedAt(model, 2));
        QVERIFY(isSelectedAt(model, 0, false));
        QCOMPARE(spy.count(), 1);

        // A no-op select stays silent.
        model.selectSingle(2);
        QCOMPARE(spy.count(), 1);

        // Toggle one extra page on, then off.
        model.setSelected(0, true);
        QCOMPARE(model.selectedCount(), 2);
        model.setSelected(0, false);
        QCOMPARE(model.selectedCount(), 1);
        QCOMPARE(spy.count(), 3);

        model.selectAll();
        QCOMPARE(model.selectedCount(), 4);
        QCOMPARE(spy.count(), 4);
        model.selectAll();
        QCOMPARE(spy.count(), 4);  // already complete

        model.clearSelection();
        QCOMPARE(model.selectedCount(), 0);
        QVERIFY(isSelectedAt(model, 2, false));

        // Guards: out-of-range writes change nothing.
        model.setSelected(9, true);
        model.selectSingle(-1);
        QCOMPARE(model.selectedCount(), 0);
    }

    void selectionFollowsMovesAndRemovals()
    {
        PageListModel model;
        model.appendPages(4);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        model.selectSingle(1);
        model.movePage(1, 3);
        QVERIFY(isSelectedAt(model, 3));  // the flag moves with the page
        QVERIFY(isSelectedAt(model, 1, false));

        model.removePage(3);
        QCOMPARE(model.selectedCount(), 0);  // the selected page is gone

        model.setSelected(1, true);
        model.removePage(0);
        QVERIFY(isSelectedAt(model, 0));  // the selection shifts up with the row
    }

    void pageRangeFormat()
    {
        QCOMPARE(PageListModel::formatPageRange({0, 1, 2, 9}), QStringLiteral("1-3, 10"));
        QCOMPARE(PageListModel::formatPageRange({0}), QStringLiteral("1"));
        QCOMPARE(PageListModel::formatPageRange({0, 1}), QStringLiteral("1-2"));
        QCOMPARE(PageListModel::formatPageRange({}), QString());
        // unsorted and duplicated input is normalized
        QCOMPARE(PageListModel::formatPageRange({9, 0, 2, 1, 2}), QStringLiteral("1-3, 10"));
    }

    void pageRangeParse()
    {
        QCOMPARE(PageListModel::parsePageRange(QStringLiteral("1-10, 15"), 20), (QList<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 14}));
        // clamped into the document; a reversed range is normalized
        QCOMPARE(PageListModel::parsePageRange(QStringLiteral("0, 99, 5-3"), 4), (QList<int>{0, 2, 3}));
        // garbage tokens are ignored; commas, spaces and semicolons separate
        QCOMPARE(PageListModel::parsePageRange(QStringLiteral("abc, 2; 4..6, x-7, 3"), 6), (QList<int>{1, 2}));
        // a three-bound token is malformed
        QCOMPARE(PageListModel::parsePageRange(QStringLiteral("1-2-3, 2"), 5), (QList<int>{1}));
        QVERIFY(PageListModel::parsePageRange(QString(), 5).isEmpty());
        QVERIFY(PageListModel::parsePageRange(QStringLiteral("1-2"), 0).isEmpty());
    }

    void pageRangeApply()
    {
        PageListModel model;
        model.appendPages(20);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        QSignalSpy spy(&model, &PageListModel::selectionChanged);
        QCOMPARE(model.applyPagesRange(QStringLiteral("1-10, 15")), 11);
        QCOMPARE(model.selectedCount(), 11);
        QVERIFY(isSelectedAt(model, 0));
        QVERIFY(isSelectedAt(model, 9));
        QVERIFY(isSelectedAt(model, 14));
        QVERIFY(isSelectedAt(model, 10, false));
        QVERIFY(isSelectedAt(model, 13, false));
        QCOMPARE(spy.count(), 1);

        // The toolbar round-trip: the formatted selection parses back to itself.
        QCOMPARE(model.selectedPagesRange(), QStringLiteral("1-10, 15"));
        QCOMPARE(model.applyPagesRange(model.selectedPagesRange()), 11);
        QCOMPARE(spy.count(), 1);  // a no-op stays silent

        // An empty input is the whole document.
        QCOMPARE(model.applyPagesRange(QStringLiteral("  ")), 0);
        QCOMPARE(model.selectedCount(), 0);
        QCOMPARE(spy.count(), 2);

        // Garbage changes nothing: a typo must not silently widen the scope.
        model.selectSingle(3);
        spy.clear();
        QCOMPARE(model.applyPagesRange(QStringLiteral("abc, -")), 0);
        QCOMPARE(model.selectedCount(), 1);
        QVERIFY(isSelectedAt(model, 3));
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestPageModel)
#include "test_pagemodel.moc"
