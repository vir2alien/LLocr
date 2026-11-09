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
        QSignalSpy text(&bridge, &EditorBridge::editorTextChanged);
        QSignalSpy ref(&bridge, &EditorBridge::editorReferenceChanged);
        QSignalSpy focus(&bridge, &EditorBridge::editorFocusChanged);
        QSignalSpy click(&bridge, &EditorBridge::editorBlockClicked);

        bridge.notifyPageReady();
        bridge.notifyTextChanged(QStringLiteral("$$x$$"), 3);
        bridge.notifyReferenceChanged(QStringLiteral("# t"), 4);
        bridge.notifyEditorFocus(true);
        bridge.notifyBlockClicked(3);

        QCOMPARE(ready.count(), 1);
        QCOMPARE(text.count(), 1);
        QCOMPARE(text.first().first().toString(), QStringLiteral("$$x$$"));
        QCOMPARE(text.first().at(1).toLongLong(), 3);
        QCOMPARE(ref.count(), 1);
        QCOMPARE(ref.first().first().toString(), QStringLiteral("# t"));
        QCOMPARE(ref.first().at(1).toLongLong(), 4);
        QCOMPARE(focus.count(), 1);
        QCOMPARE(focus.first().first().toBool(), true);
        QCOMPARE(click.count(), 1);
        QCOMPARE(click.first().first().toInt(), 3);
    }
};

QTEST_GUILESS_MAIN(TestEditorBridge)
#include "test_editor_bridge.moc"
