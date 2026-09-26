#include <QtTest>

#include "app/PageEditStore.h"

using namespace llocr;

// The store keeps the revert baseline and the "edited" marker per page (ADR
// 102). It deliberately no longer holds the current text — that lives on the
// page — so these cases pin the baseline/remap contract it does own.
class TestPageEditStore : public QObject
{
    Q_OBJECT

private slots:
    void resetStoresBaselineAndDropsEdit()
    {
        PageEditStore store;
        QVERIFY(!store.isEdited(0));
        QVERIFY(store.baseline(0).isEmpty());

        QVERIFY(!store.reset(0, QStringLiteral("recognized")));
        QCOMPARE(store.baseline(0), QStringLiteral("recognized"));

        store.setEdited(0, true);
        QVERIFY(store.isEdited(0));
        // A re-recognition replaces the baseline and reports the dropped edit.
        QVERIFY(store.reset(0, QStringLiteral("recognized again")));
        QCOMPARE(store.baseline(0), QStringLiteral("recognized again"));
        QVERIFY(!store.isEdited(0));
    }

    void revertClearsBaselineAndMarker()
    {
        PageEditStore store;
        store.reset(2, QStringLiteral("ocr text"));
        store.setEdited(2, true);

        QVERIFY(store.revert(2));
        QVERIFY(!store.isEdited(2));
        QVERIFY(store.baseline(2).isEmpty());
        QVERIFY(!store.revert(2));  // idempotent
    }

    void setEditedIsIdempotent()
    {
        PageEditStore store;
        store.reset(0, QStringLiteral("x"));
        store.setEdited(0, true);
        store.setEdited(0, true);
        QVERIFY(store.isEdited(0));
        store.setEdited(0, false);
        store.setEdited(0, false);
        QVERIFY(!store.isEdited(0));
    }

    void remapAfterRemoveShiftsIndices()
    {
        PageEditStore store;
        store.reset(0, QStringLiteral("a"));
        store.reset(1, QStringLiteral("b"));
        store.reset(2, QStringLiteral("c"));
        store.setEdited(1, true);
        store.setEdited(2, true);

        store.remapAfterRemove(0);

        QCOMPARE(store.baseline(0), QStringLiteral("b"));
        QCOMPARE(store.baseline(1), QStringLiteral("c"));
        QVERIFY(store.baseline(2).isEmpty());
        // The markers move with their pages: old 1 and 2 become 0 and 1.
        QVERIFY(store.isEdited(0));
        QVERIFY(store.isEdited(1));
        QVERIFY(!store.isEdited(2));
    }

    void remapAfterMoveFollowsPages()
    {
        PageEditStore store;
        store.reset(0, QStringLiteral("a"));
        store.reset(1, QStringLiteral("b"));
        store.reset(2, QStringLiteral("c"));
        store.setEdited(2, true);

        store.remapAfterMove(0, 2);  // 0,1,2 -> 1,2,0
        QCOMPARE(store.baseline(0), QStringLiteral("b"));
        QCOMPARE(store.baseline(1), QStringLiteral("c"));
        QCOMPARE(store.baseline(2), QStringLiteral("a"));
        QVERIFY(store.isEdited(1));  // was page 2

        store.remapAfterMove(2, 0);  // and back
        QCOMPARE(store.baseline(0), QStringLiteral("a"));
        QCOMPARE(store.baseline(1), QStringLiteral("b"));
        QCOMPARE(store.baseline(2), QStringLiteral("c"));
        QVERIFY(store.isEdited(2));
    }

    void clearDropsEverything()
    {
        PageEditStore store;
        store.reset(0, QStringLiteral("a"));
        store.setEdited(0, true);
        store.clear();
        QVERIFY(store.baseline(0).isEmpty());
        QVERIFY(!store.isEdited(0));
    }
};

QTEST_MAIN(TestPageEditStore)
#include "test_page_edit_store.moc"
