#include <QElapsedTimer>
#include <QGuiApplication>
#include <QtTest>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QWebChannel>
#include <QWebEnginePage>

#include "app/EditorBridge.h"

using namespace llocr;

// The WYSIWYG editor page, loaded for real: editor.html must pull the
// vendored Vditor bundle from qrc, compile the Lute WASM under the page CSP
// and answer over the QWebChannel bridge. This is the offline gate for the
// whole web stack the editor tab stands on — a failure here means the
// qrc-carried assets (or the CSP) broke, and the disk-fallback path from the
// design takes over.
class TestEditorPage : public QObject
{
    Q_OBJECT

private slots:
    void loadsVditorUnderThePageCspAndRendersPushedMarkdown();
    void undoBrokenMathPreviewsAreReRendered();

private:
    void spinUntil(const std::function<bool()> &done, int timeoutMs = 60000);
};

void TestEditorPage::spinUntil(const std::function<bool()> &done, int timeoutMs)
{
    // done() may carry side effects (an async fetch whose callback lands in
    // the next spin), so it is evaluated exactly once per attempt and the
    // verdict is kept — re-evaluating it at the assert would undo the pass.
    QElapsedTimer timer;
    timer.start();
    bool ok = false;
    while (!ok && timer.elapsed() < timeoutMs) {
        ok = done();
        if (!ok)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    QVERIFY2(ok, "timed out waiting for the editor page");
}

void TestEditorPage::loadsVditorUnderThePageCspAndRendersPushedMarkdown()
{
    EditorBridge bridge;
    QWebChannel channel;
    channel.registerObject(QStringLiteral("editorBridge"), &bridge);
    QWebEnginePage page;
    page.setWebChannel(&channel);

    bool loaded = false;
    bool loadOk = false;
    bool ready = false;
    QObject::connect(&page, &QWebEnginePage::loadFinished, [&](bool ok) {
        loaded = true;
        loadOk = ok;
    });
    QObject::connect(&bridge, &EditorBridge::pageReady, [&ready]() { ready = true; });

    page.load(QUrl(QStringLiteral("qrc:/editor/editor.html")));
    spinUntil([&]() { return loaded; });
    QVERIFY2(loadOk, "editor.html failed to load");
    // Vditor's after() only runs once its lazy qrc fetches (Lute WASM, KaTeX)
    // succeeded under the CSP; pageReady is the whole stack's answer.
    spinUntil([&]() { return ready; });

    bool pushed = false;
    page.runJavaScript(QStringLiteral(R"js(pushPage("# T\n\nBody text", {dark: false, bg: "#ffffff", fg: "#1a1a1a", hl: "rgba(48,140,198,0.2)"});)js"), [&pushed](const QVariant &) { pushed = true; });
    spinUntil([&]() { return pushed; });

    // The pull settles after the async setValue → normalization reference;
    // until then getValue() still answers with the untouched placeholder.
    QString value;
    spinUntil([&]() {
        value.clear();
        page.runJavaScript(QStringLiteral("vditor.getValue()"), [&value](const QVariant &v) { value = v.toString(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return value.contains(QLatin1String("Body text"));
    });
    QVERIFY2(value.contains(QLatin1String("Body text")), qPrintable(value));

    // Every top-level markdown block is tagged with its ordinal, so clicks
    // and highlights can map back through blockBoxMap.
    int blocks = 0;
    spinUntil([&]() {
        page.runJavaScript(QStringLiteral("document.querySelectorAll('.vditor-wysiwyg [data-ll-block]').length"), [&blocks](const QVariant &v) { blocks = v.toInt(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return blocks > 0;
    });
    QCOMPARE(blocks, 2);
}

// Undo restores DOM snapshots whose math previews carry the raw source (or
// nothing) instead of the render; refreshMath must turn them back into
// KaTeX output without a full push.
void TestEditorPage::undoBrokenMathPreviewsAreReRendered()
{
    EditorBridge bridge;
    QWebChannel channel;
    channel.registerObject(QStringLiteral("editorBridge"), &bridge);
    QWebEnginePage page;
    page.setWebChannel(&channel);

    bool loaded = false;
    bool loadOk = false;
    bool ready = false;
    QObject::connect(&page, &QWebEnginePage::loadFinished, [&](bool ok) {
        loaded = true;
        loadOk = ok;
    });
    QObject::connect(&bridge, &EditorBridge::pageReady, [&ready]() { ready = true; });

    page.load(QUrl(QStringLiteral("qrc:/editor/editor.html")));
    spinUntil([&]() { return loaded; });
    QVERIFY2(loadOk, "editor.html failed to load");
    spinUntil([&]() { return ready; });

    bool pushed = false;
    page.runJavaScript(QStringLiteral(R"js(pushPage("Body\n\n$$E=mc^2$$", {dark: false, bg: "#ffffff", fg: "#1a1a1a", hl: "rgba(48,140,198,0.2)"});)js"),
                       [&pushed](const QVariant &) { pushed = true; });
    spinUntil([&]() { return pushed; });

    // The pushed formula renders into a .language-math element.
    int rendered = 0;
    spinUntil([&]() {
        page.runJavaScript(QStringLiteral("document.querySelectorAll('.language-math[data-math]').length"), [&rendered](const QVariant &v) { rendered = v.toInt(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return rendered > 0;
    });
    QCOMPARE(rendered, 1);

    // Simulate the undo snapshot: the preview keeps data-math but holds the
    // raw source instead of the KaTeX output.
    bool broken = false;
    page.runJavaScript(QStringLiteral("refreshMath !== undefined && (function(){const el=document.querySelector('.language-math[data-math]');"
                                      "el.innerHTML=el.getAttribute('data-math');return true;})()"),
                       [&broken](const QVariant &v) { broken = v.toBool(); });
    spinUntil([&]() { return broken; });

    // refreshMath repairs it: children (KaTeX spans) come back.
    int children = 0;
    spinUntil([&]() {
        page.runJavaScript(QStringLiteral("refreshMath(); const el=document.querySelector('.language-math[data-math]'); el ? el.childElementCount : -1"),
                           [&children](const QVariant &v) { children = v.toInt(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return children > 0;
    });
    QVERIFY(children > 0);
}

int main(int argc, char *argv[])
{
    // Must run before the QGuiApplication is constructed.
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    TestEditorPage tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_editor_page.moc"
