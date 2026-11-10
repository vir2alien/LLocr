#include <QtTest>

#include "app/EditorBridge.h"

using namespace llocr;

class TestEditorBridge : public QObject
{
    Q_OBJECT

private slots:
    // Each notify slot is the QWebChannel entry point for exactly one signal
    // the QML editor subscribes to; the payload travels unchanged.
    void slotsEmitPairedSignals()
    {
        EditorBridge bridge;
        QCOMPARE(bridge.objectName(), QStringLiteral("editorBridge"));

        QSignalSpy ready(&bridge, &EditorBridge::pageReady);
        QSignalSpy ref(&bridge, &EditorBridge::editorReferenceChanged);
        QSignalSpy click(&bridge, &EditorBridge::editorBlockClicked);
        QSignalSpy blockChanged(&bridge, &EditorBridge::editorBlockChanged);
        QSignalSpy resync(&bridge, &EditorBridge::editorResyncRequested);
        QSignalSpy structure(&bridge, &EditorBridge::editorBlockStructureRequested);

        bridge.notifyPageReady();
        bridge.notifyBlockChanged(2, QStringLiteral("block md"), 3);
        bridge.notifyResyncRequested();
        bridge.notifyBlockStructureAction(7, QStringLiteral("down"));
        bridge.notifyReferenceChanged(QStringLiteral("# t"), 4);
        bridge.notifyBlockClicked(3);

        QCOMPARE(ready.count(), 1);
        QCOMPARE(ref.count(), 1);
        QCOMPARE(ref.first().first().toString(), QStringLiteral("# t"));
        QCOMPARE(ref.first().at(1).toLongLong(), 4);
        QCOMPARE(click.count(), 1);
        QCOMPARE(click.first().first().toInt(), 3);
        QCOMPARE(blockChanged.count(), 1);
        QCOMPARE(blockChanged.first().at(0).toInt(), 2);
        QCOMPARE(blockChanged.first().at(1).toString(), QStringLiteral("block md"));
        QCOMPARE(blockChanged.first().at(2).toLongLong(), 3);
        QCOMPARE(resync.count(), 1);
        QCOMPARE(structure.count(), 1);
        QCOMPARE(structure.first().at(0).toInt(), 7);
        QCOMPARE(structure.first().at(1).toString(), QStringLiteral("down"));
    }
};

QTEST_GUILESS_MAIN(TestEditorBridge)
#include "test_editor_bridge.moc"
