#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
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
    void blockEditReportsThePullAndKeepsTheDomClean();
    void blockWithInnerBlankLineKeepsThePullAlive();
    void toolbarActionAsksTheModelInsteadOfMutating();
    void htmlTableMathRendersInThePreviewOnly();
    void theMenuWearsTheAppsToolSizes();

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
        page.runJavaScript(QStringLiteral("refreshMath(); (function(){ var el = document.querySelector('.language-math[data-math]'); return el ? el.childElementCount : -1; })()"),
                           [&children](const QVariant &v) { children = v.toInt(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return children > 0;
    });
    QVERIFY(children > 0);
}

// The user's flow: a keystroke in a block pulls the block's markdown, and
// the model's answering push leaves the content exactly the user's text —
// nothing injects itself into the blocks.
void TestEditorPage::blockEditReportsThePullAndKeepsTheDomClean()
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

    // The app-level push: the document plus the per-block fragments.
    bool pushed = false;
    page.runJavaScript(QStringLiteral(R"js(pushDocument("Body\n\nSecond", {dark: false, bg: "#ffffff", fg: "#1a1a1a", hl: "rgba(48,140,198,0.2)"}, ["Body", "Second"]);)js"),
                       [&pushed](const QVariant &) { pushed = true; });
    spinUntil([&]() { return pushed; });

    // Type into the second block like the user does: caret at its end, one
    // keystroke through execCommand, which fires the input events Vditor
    // listens to.
    bool typed = false;
    page.runJavaScript(QStringLiteral(R"js((function(){
        var blocks = document.querySelectorAll('.vditor-wysiwyg [data-ll-block]');
        var target = blocks[1];
        target.focus();
        var sel = window.getSelection();
        var range = document.createRange();
        range.selectNodeContents(target);
        range.collapse(false);
        sel.removeAllRanges();
        sel.addRange(range);
        document.execCommand('insertText', false, 'x');
        return true;
    })())js"),
                       [&typed](const QVariant &v) { typed = v.toBool(); });
    spinUntil([&]() { return typed; });

    // The pull reports the edited block within a pull timer's beat.
    int reportedOrdinal = -1;
    QString reportedMarkdown;
    qint64 reportedToken = -1;
    QObject::connect(&bridge, &EditorBridge::editorBlockChanged, [&](int ordinal, const QString &md, qint64 token) {
        reportedOrdinal = ordinal;
        reportedMarkdown = md;
        reportedToken = token;
    });
    spinUntil([&]() { return reportedOrdinal >= 0; }, 5000);
    QCOMPARE(reportedOrdinal, 1);
    QCOMPARE(reportedToken, 1);
    // The pull's slice uses the editor's serialization on both sides: a raw
    // push and getValue() disagree in normalization details (the trailing
    // newline), and a delta across them would steal a tail into the fragment.
    QCOMPARE(reportedMarkdown, QStringLiteral("Secondx"));

    // The echo push answers the layer. The DOM is read as text: running the
    // lute serializer in a loop crashes the renderer under the offscreen
    // platform.
    bool echoed = false;
    page.runJavaScript(QStringLiteral(R"js(pushDocument("Body\n\nSecondx", {dark: false, bg: "#ffffff", fg: "#1a1a1a", hl: "rgba(48,140,198,0.2)"}, ["Body", "Secondx"]);)js"),
                       [&echoed](const QVariant &) { echoed = true; });
    spinUntil([&]() { return echoed; });

    // The verdict travels as a number: a JS string handed to a QVariant in a
    // tight loop corrupts the renderer heap under the offscreen platform.
    int verdict = 0;
    spinUntil([&]() {
        page.runJavaScript(QStringLiteral("(function(){"
                                          " var c = document.querySelector('.vditor-wysiwyg [contenteditable]');"
                                          " if (!c) return 0;"
                                          " var t = c.textContent;"
                                          " if (t.indexOf('Secondx') < 0) return 0;"
                                          " return (t.indexOf('ll-ai-badge') >= 0 || t.indexOf('AI') >= 0) ? 2 : 1;"
                                          "})()"),
                           [&verdict](const QVariant &v) { verdict = v.toInt(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return verdict > 0;
    });
    QCOMPARE(verdict, 1);
}

// A canonical fragment may carry a blank line inside itself (a multi-paragraph
// block, an HTML table): then one model fragment renders as several DOM
// blocks. The pull must survive that — it anchors on the DOM tags and the
// fragment list length must not gate it away.
void TestEditorPage::blockWithInnerBlankLineKeepsThePullAlive()
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

    // The push: three page segments, one model fragment spanning the last two.
    bool pushed = false;
    page.runJavaScript(QStringLiteral(R"js(pushDocument("First\n\nA\n\nB", {dark: false, bg: "#ffffff", fg: "#1a1a1a", hl: "rgba(48,140,198,0.2)"}, ["First", "A", "B"]);)js"),
                       [&pushed](const QVariant &) { pushed = true; });
    spinUntil([&]() { return pushed; });

    // The DOM renders three blocks; the pull walks the same segmentation.
    int children = 0;
    spinUntil([&]() {
        page.runJavaScript(QStringLiteral("(function(){ var c = document.querySelector('.vditor-wysiwyg [contenteditable]'); return c ? c.children.length : -1; })()"),
                           [&children](const QVariant &v) { children = v.toInt(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return children > 0;
    });
    QCOMPARE(children, 3);

    // Type into the third DOM block ("B") like the user does.
    bool typed = false;
    page.runJavaScript(QStringLiteral(R"js((function(){
        var blocks = document.querySelectorAll('.vditor-wysiwyg [data-ll-block]');
        var target = blocks[blocks.length - 1];
        target.focus();
        var sel = window.getSelection();
        var range = document.createRange();
        range.selectNodeContents(target);
        range.collapse(false);
        sel.removeAllRanges();
        sel.addRange(range);
        document.execCommand('insertText', false, 'x');
        return true;
    })())js"),
                       [&typed](const QVariant &v) { typed = v.toBool(); });
    spinUntil([&]() { return typed; });

    // The pull must still report the edit — the fragment spanning several
    // DOM blocks must not gate it away.
    int reportedOrdinal = -1;
    QString reportedMarkdown;
    QObject::connect(&bridge, &EditorBridge::editorBlockChanged, [&](int ordinal, const QString &md, qint64) {
        reportedOrdinal = ordinal;
        reportedMarkdown = md;
    });
    spinUntil([&]() { return reportedOrdinal >= 0; }, 5000);
    QCOMPARE(reportedOrdinal, 2);
    QCOMPARE(reportedMarkdown, QStringLiteral("Bx"));
}

// The hover toolbar (up/down/remove) is Vditor's own; a click must reach
// the model as a structure request instead of mutating the DOM — the settle
// would throw a mutated document away seconds later.
void TestEditorPage::toolbarActionAsksTheModelInsteadOfMutating()
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
    page.runJavaScript(QStringLiteral(R"js(pushDocument("First\n\nSecond\n\nThird", {dark: false, bg: "#ffffff", fg: "#1a1a1a", hl: "rgba(48,140,198,0.2)"}, ["First", "Second", "Third"]);)js"),
                       [&pushed](const QVariant &) { pushed = true; });
    spinUntil([&]() { return pushed; });

    // The report must listen before the click: the request travels while
    // the staging script is still running.
    int reportedOrdinal = -1;
    QString reportedAction;
    QObject::connect(&bridge, &EditorBridge::editorBlockStructureRequested, [&](int ordinal, const QString &action) {
        reportedOrdinal = ordinal;
        reportedAction = action;
    });

    // The caret into the middle block, a toolbar button into the popover —
    // the same setup Vditor builds, with the same data-type.
    bool staged = false;
    page.runJavaScript(QStringLiteral(R"js((function(){
        var blocks = document.querySelectorAll('.vditor-wysiwyg [data-ll-block]');
        var target = blocks[1];
        var sel = window.getSelection();
        var range = document.createRange();
        range.selectNodeContents(target);
        range.collapse(false);
        sel.removeAllRanges();
        sel.addRange(range);
        try {
            target.focus();
            var popover = document.querySelector('.vditor-wysiwyg .vditor-panel--none');
            var button = document.createElement('button');
            button.setAttribute('type', 'button');
            button.setAttribute('data-type', 'down');
            popover.innerHTML = '';
            popover.appendChild(button);
            button.click();
            return JSON.stringify({
                blocks: document.querySelectorAll('.vditor-wysiwyg [data-ll-block]').length,
                inPopover: popover.contains(button)
            });
        } catch (e) { return 'ERR: ' + e.message; }
    })())js"),
                       [&staged](const QVariant &v) {
                           staged = v.toString().contains(QLatin1String("\"blocks\":3"));
                           qWarning() << "STAGE" << qPrintable(v.toString());
                       });
    spinUntil([&]() { return staged; });

    spinUntil([&]() { return reportedOrdinal >= 0; }, 5000);
    QCOMPARE(reportedOrdinal, 1);
    QCOMPARE(reportedAction, QStringLiteral("down"));

    // The DOM stayed untouched: the button never mutated the blocks.
    int blocks = 0;
    spinUntil([&]() {
        page.runJavaScript(QStringLiteral("document.querySelectorAll('.vditor-wysiwyg [data-ll-block]').length"), [&blocks](const QVariant &v) { blocks = v.toInt(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return blocks == 3;
    });
    QCOMPARE(blocks, 3);
}

// "Keep tables as HTML" pushes tables through as html-blocks: the preview
// shows the raw HTML where $…$ stays literal. The formulas render with KaTeX
// inside the preview, and the serialization — which reads the block's hidden
// source — carries none of it.
void TestEditorPage::htmlTableMathRendersInThePreviewOnly()
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
    bool rendererGone = false;
    QObject::connect(&page, &QWebEnginePage::renderProcessTerminated, [&rendererGone]() { rendererGone = true; });

    page.load(QUrl(QStringLiteral("qrc:/editor/editor.html")));
    spinUntil([&]() { return loaded; });
    QVERIFY2(loadOk, "editor.html failed to load");
    spinUntil([&]() { return ready; });
    QVERIFY2(!rendererGone, "the renderer died during page load");

    bool pushed = false;
    page.runJavaScript(
        QStringLiteral(
            R"js(pushDocument("<table><tr><th>a</th><th>b</th></tr><tr><td>$x^2$</td><td>ok</td></tr></table>", {dark: false, bg: "#ffffff", fg: "#1a1a1a", hl: "rgba(48,140,198,0.2)"}, ["t"]);)js"),
        [&pushed](const QVariant &) { pushed = true; });
    spinUntil([&]() { return pushed; });

    // The preview holds the KaTeX output inside our marker span; the render
    // settles within the MutationObserver's debounce beat.
    QString state;
    spinUntil([&]() {
        state.clear();
        page.runJavaScript(QStringLiteral("JSON.stringify({err: window.__err || null, rendered: document.querySelectorAll('.ll-math .katex').length})"),
                           [&state](const QVariant &v) { state = v.toString(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return state.contains(QLatin1String("\"rendered\":1"));
    });
    QVERIFY2(!state.contains(QLatin1String("err\":\"")), qPrintable(state));

    // And the serialization carries the dollar source, not the render: the
    // hidden <pre> source of the html-block is what getValue() speaks.
    QString value;
    spinUntil([&]() {
        value.clear();
        page.runJavaScript(QStringLiteral("vditor.getValue()"), [&value](const QVariant &v) { value = v.toString(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return value.contains(QLatin1String("$x^2$"));
    });
    QVERIFY2(!value.contains(QLatin1String("katex")), qPrintable(value));
    QVERIFY2(!value.contains(QLatin1String("ll-math")), qPrintable(value));
}

// The editor's menu is the app's menu: the buttons are 32×28 with an 18 px
// glyph, the first button stands 8 px from the edge, every button says what
// it does, and the zoom at the right end scales the text (ADR 302).
void TestEditorPage::theMenuWearsTheAppsToolSizes()
{
    EditorBridge bridge;
    QWebChannel channel;
    channel.registerObject(QStringLiteral("editorBridge"), &bridge);
    QWebEnginePage page;
    page.setWebChannel(&channel);

    bool loaded = false;
    bool ready = false;
    QObject::connect(&page, &QWebEnginePage::loadFinished, [&](bool ok) { loaded = ok; });
    QObject::connect(&bridge, &EditorBridge::pageReady, [&ready]() { ready = true; });

    page.load(QUrl(QStringLiteral("qrc:/editor/editor.html")));
    spinUntil([&]() { return loaded; });
    QVERIFY(loaded);
    spinUntil([&]() { return ready; });

    QString info;
    spinUntil([&]() {
        info.clear();
        page.runJavaScript(QStringLiteral(R"js(
            (function () {
                const toolbar = document.querySelector(".vditor-toolbar");
                const button = document.querySelector(".vditor-toolbar__item .vditor-tooltipped");
                const rect = button.getBoundingClientRect();
                return JSON.stringify({
                    buttonWidth: rect.width,
                    buttonHeight: rect.height,
                    iconSize: getComputedStyle(document.querySelector(".vditor-toolbar__item svg")).width,
                    leftPadding: getComputedStyle(toolbar).paddingLeft,
                    tipContent: getComputedStyle(button, "::after").content,
                    tipLabel: button.getAttribute("aria-label"),
                    zoomButtons: document.querySelectorAll(".ll-zoom [data-ll-zoom]").length,
                });
            })()
        )js"),
                           [&info](const QVariant &v) { info = v.toString(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return !info.isEmpty();
    });

    const QJsonObject menu = QJsonDocument::fromJson(info.toUtf8()).object();
    QCOMPARE(menu.value(QStringLiteral("buttonWidth")).toDouble(), 32.0);
    QCOMPARE(menu.value(QStringLiteral("buttonHeight")).toDouble(), 28.0);
    QCOMPARE(menu.value(QStringLiteral("iconSize")).toString(), QStringLiteral("18px"));
    QCOMPARE(menu.value(QStringLiteral("leftPadding")).toString(), QStringLiteral("8px"));
    // The tooltip says what the button does — Vditor switches its tooltips off
    // in a narrow pane, and the preview pane is narrow.
    const QString tipLabel = menu.value(QStringLiteral("tipLabel")).toString();
    QVERIFY2(!tipLabel.isEmpty(), qPrintable(info));
    QVERIFY2(menu.value(QStringLiteral("tipContent")).toString().contains(tipLabel), qPrintable(info));
    QCOMPARE(menu.value(QStringLiteral("zoomButtons")).toInt(), 3);

    // The zoom scales the text and the label follows: the menu itself keeps
    // its size. The click stands outside the poll — a click inside it would
    // land again on every attempt.
    page.runJavaScript(QStringLiteral(R"js(
        (function () {
            const content = document.querySelector(".vditor-wysiwyg [contenteditable]");
            if (content)
                content.style.zoom = "";
            document.querySelectorAll(".ll-zoom [data-ll-zoom]")[2].click();
            return "clicked";
        })()
    )js"),
                       [](const QVariant &) {});

    QString zoom;
    spinUntil([&]() {
        zoom.clear();
        page.runJavaScript(QStringLiteral(R"js(
            (function () {
                const content = document.querySelector(".vditor-wysiwyg [contenteditable]");
                const label = document.querySelector(".ll-zoom__label span");
                return JSON.stringify({zoom: content ? content.style.zoom : "",
                                       label: label ? label.textContent : ""});
            })()
        )js"),
                           [&zoom](const QVariant &v) { zoom = v.toString(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return !zoom.isEmpty();
    });

    const QJsonObject state = QJsonDocument::fromJson(zoom.toUtf8()).object();
    QCOMPARE(state.value(QStringLiteral("zoom")).toString(), QStringLiteral("1.25"));
    QCOMPARE(state.value(QStringLiteral("label")).toString(), QStringLiteral("125%"));

    // A model push rebuilds the content — the zoom must survive it.
    bool pushed = false;
    page.runJavaScript(QStringLiteral(R"js(pushPage("Body\n\nMore text", {dark: false, bg: "#ffffff", fg: "#1a1a1a", hl: "rgba(48,140,198,0.2)"});)js"),
                       [&pushed](const QVariant &) { pushed = true; });
    spinUntil([&]() { return pushed; });
    QString pushedText;
    spinUntil([&]() {
        pushedText.clear();
        page.runJavaScript(QStringLiteral("document.querySelector('.vditor-wysiwyg [contenteditable]').innerText"), [&pushedText](const QVariant &v) { pushedText = v.toString(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return pushedText.contains(QLatin1String("More text"));
    });

    QString afterPush;
    spinUntil([&]() {
        afterPush.clear();
        page.runJavaScript(QStringLiteral(R"js(
            (function () {
                const content = document.querySelector(".vditor-wysiwyg [contenteditable]");
                const label = document.querySelector(".ll-zoom__label span");
                return JSON.stringify({zoom: content ? content.style.zoom : "",
                                       label: label ? label.textContent : ""});
            })()
        )js"),
                           [&afterPush](const QVariant &v) { afterPush = v.toString(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return !afterPush.isEmpty();
    });
    const QJsonObject kept = QJsonDocument::fromJson(afterPush.toUtf8()).object();
    QCOMPARE(kept.value(QStringLiteral("zoom")).toString(), QStringLiteral("1.25"));
    QCOMPARE(kept.value(QStringLiteral("label")).toString(), QStringLiteral("125%"));
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
