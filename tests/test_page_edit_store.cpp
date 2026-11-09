#include <QtTest>

#include "app/PageEditStore.h"

using namespace llocr;

// The store only tracks the per-page edited flags now (ADR 296): the revert
// baseline died with the hand-written page text — the document is the join of
// the blocks, and «Отменить изменения» releases the user layers instead.
class TestPageEditStore : public QObject
{
    Q_OBJECT

private slots:
    void editedFlagRoundTrip()
    {
        PageEditStore store;
        QVERIFY(!store.isEdited(0));
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
        store.setEdited(1, true);
        store.setEdited(2, true);

        store.remapAfterRemove(0);

        // The markers move with their pages: old 1 and 2 become 0 and 1.
        QVERIFY(store.isEdited(0));
        QVERIFY(store.isEdited(1));
        QVERIFY(!store.isEdited(2));
    }

    void remapAfterMoveFollowsPages()
    {
        PageEditStore store;
        store.setEdited(2, true);

        store.remapAfterMove(0, 2);  // 0,1,2 -> 1,2,0
        QVERIFY(store.isEdited(1));  // was page 2

        store.remapAfterMove(2, 0);  // and back
        QVERIFY(store.isEdited(2));
    }

    void clearDropsEverything()
    {
        PageEditStore store;
        store.setEdited(0, true);
        store.clear();
        QVERIFY(!store.isEdited(0));
    }
};

QTEST_MAIN(TestPageEditStore)
#include "test_page_edit_store.moc"
