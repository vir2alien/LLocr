#include <QTest>
#include <QTextDocument>

#include "runtime/LogDocumentSync.h"

using llocr::LogDocumentSync;

namespace {

QString plain(const QTextDocument &doc)
{
    return doc.toPlainText();
}

}  // namespace

class TestLogDocumentSync : public QObject
{
    Q_OBJECT

private slots:
    // The steady state — a chatty log must not rebuild the document per flush.
    void appendsOnlyTheTail()
    {
        QTextDocument doc;
        LogDocumentSync sync;
        sync.attach(&doc);

        sync.apply({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, 0, QString());
        QCOMPARE(plain(doc), QStringLiteral("a\nb\nc"));

        sync.apply({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e")}, 0, QString());
        QCOMPARE(plain(doc), QStringLiteral("a\nb\nc\nd\ne"));
    }

    // The owner capped its list and reports how many lines fell off the front.
    void dropsReportedLinesFromTheFront()
    {
        QTextDocument doc;
        LogDocumentSync sync;
        sync.attach(&doc);

        sync.apply({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), QStringLiteral("e")}, 0, QString());
        sync.apply({QStringLiteral("d"), QStringLiteral("e")}, 3, QString());
        QCOMPARE(plain(doc), QStringLiteral("d\ne"));
    }

    // A clear empties the mirror with no dropped counter; the window must not
    // keep the stale lines.
    void clearEmptiesTheDocument()
    {
        QTextDocument doc;
        LogDocumentSync sync;
        sync.attach(&doc);

        sync.apply({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, 0, QString());
        sync.apply({}, 0, QString());
        QCOMPARE(plain(doc), QString());

        sync.apply({QStringLiteral("d")}, 0, QString());
        QCOMPARE(plain(doc), QStringLiteral("d"));
    }

    void headerAppearsUpdatesAndVanishes()
    {
        QTextDocument doc;
        LogDocumentSync sync;
        sync.attach(&doc);

        sync.apply({}, 0, QStringLiteral("3 dropped"));
        QCOMPARE(plain(doc), QStringLiteral("3 dropped"));

        sync.apply({QStringLiteral("x")}, 0, QStringLiteral("3 dropped"));
        QCOMPARE(plain(doc), QStringLiteral("3 dropped\nx"));

        sync.apply({QStringLiteral("x")}, 0, QStringLiteral("5 dropped"));
        QCOMPARE(plain(doc), QStringLiteral("5 dropped\nx"));

        sync.apply({QStringLiteral("x")}, 0, QString());
        QCOMPARE(plain(doc), QStringLiteral("x"));
    }

    // The dropped counter never eats the header line standing in front.
    void dropsWithHeaderKeepTheHeader()
    {
        QTextDocument doc;
        LogDocumentSync sync;
        sync.attach(&doc);

        sync.apply({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, 0, QStringLiteral("dropped"));
        sync.apply({QStringLiteral("c")}, 2, QStringLiteral("2 dropped"));
        QCOMPARE(plain(doc), QStringLiteral("2 dropped\nc"));
    }

    void forceFullReplacesEverything()
    {
        QTextDocument doc;
        LogDocumentSync sync;
        sync.attach(&doc);
        sync.apply({QStringLiteral("old")}, 0, QStringLiteral("header"));

        sync.apply({QStringLiteral("new1"), QStringLiteral("new2")}, 0, QString(), true);
        QCOMPARE(plain(doc), QStringLiteral("new1\nnew2"));
    }

    // A reopened window attaches a fresh document; the first apply on it is a
    // full rebuild from the owner's current state, not an append.
    void reattachRebuildsFromFullState()
    {
        QTextDocument doc;
        LogDocumentSync sync;
        sync.attach(&doc);
        sync.apply({QStringLiteral("a"), QStringLiteral("b")}, 0, QString());

        QTextDocument fresh;
        sync.attach(&fresh);
        sync.apply({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, 0, QString());
        QCOMPARE(plain(fresh), QStringLiteral("a\nb\nc"));
    }

    // A hidden window pauses the mirror: flushes are dropped on the floor and
    // resume rebuilds from the owner's full current list in one pass.
    void pauseStopsFlushesAndResumeRebuilds()
    {
        QTextDocument doc;
        LogDocumentSync sync;
        sync.attach(&doc);
        sync.apply({QStringLiteral("a"), QStringLiteral("b")}, 0, QString());
        QCOMPARE(plain(doc), QStringLiteral("a\nb"));

        sync.setPaused(true);
        sync.apply({QStringLiteral("c"), QStringLiteral("d")}, 2, QString());
        QCOMPARE(plain(doc), QStringLiteral("a\nb"));

        sync.setPaused(false);
        sync.apply({QStringLiteral("c"), QStringLiteral("d")}, 2, QString());
        QCOMPARE(plain(doc), QStringLiteral("c\nd"));
    }

    // Pause survives nothing: a pause/resume cycle with no flush in between
    // still forces the rebuild.
    void resumeForcesRebuildWithoutFlushes()
    {
        QTextDocument doc;
        LogDocumentSync sync;
        sync.attach(&doc);
        sync.apply({QStringLiteral("a")}, 0, QString());

        sync.setPaused(true);
        sync.setPaused(false);
        sync.apply({QStringLiteral("a"), QStringLiteral("b")}, 0, QString());
        QCOMPARE(plain(doc), QStringLiteral("a\nb"));
    }

    // The rebuild must replace the document's content in one step: the
    // attached QQuickTextEdit re-lays-out the whole document per
    // contentsChanged, so per-line cursor inserts cost a full layout each —
    // seconds on a saturated ring with the window opening on the GUI thread.
    void rebuildFiresContentsChangedOnce()
    {
        QTextDocument doc;
        LogDocumentSync sync;
        sync.attach(&doc);

        int changes = 0;
        QObject::connect(&doc, &QTextDocument::contentsChanged, [&changes]() { ++changes; });

        QStringList lines;
        for (int i = 0; i < 2000; ++i)
            lines.append(QStringLiteral("line %1").arg(i));
        sync.apply(lines, 0, QStringLiteral("header"));
        QCOMPARE(changes, 1);
        QCOMPARE(doc.blockCount(), lines.size() + 1);
    }
};

QTEST_GUILESS_MAIN(TestLogDocumentSync)

#include "test_log_document_sync.moc"
