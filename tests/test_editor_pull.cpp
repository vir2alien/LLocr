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

// The untrusted-content gate and the per-block pull's repeated-fragment
// anchor, both on one page. The suite is a separate binary from
// test_editor_page and keeps a single page on purpose: the offscreen
// WebEngine renderer SIGTRAPs when a second page is spawned inside the same
// process (the "Mach rendezvous" failure the existing tests work around by
// keeping their page count low and their order fixed).
class TestEditorPull : public QObject
{
    Q_OBJECT

private slots:
    void rawHtmlIsInertAndRepeatedFragmentsAnchorLocally();

private:
    void spinUntil(const std::function<bool()> &done, int timeoutMs = 60000);
};

void TestEditorPull::spinUntil(const std::function<bool()> &done, int timeoutMs)
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

void TestEditorPull::rawHtmlIsInertAndRepeatedFragmentsAnchorLocally()
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

    // --- The untrusted-content gate ---------------------------------------
    //
    // The markdown the page renders comes from an OCR model: raw HTML inside
    // it is untrusted input. Whatever the document carries, no inline handler
    // may run and no payload may survive on the elements the sanitizer lets
    // through — the bridge the page owns writes, moves and removes blocks.
    const char *document =
        R"js(Before <img src=x onerror=\"window.__xss=1\"> after\n\n<script>window.__xssScript = 1;</script>\n\n<iframe srcdoc=\"&lt;script&gt;parent.__xssDoc = 1;&lt;/script&gt;\"></iframe>\n\n<table><tr><td onclick=\"window.__xssClick = 1\">cell</td></tr></table>\n\n<a href=\"javascript:window.__xssHref = 1\">link</a>)js";
    bool pushed = false;
    page.runJavaScript(QStringLiteral(R"js(pushDocument("%1", {dark: false, bg: "#ffffff", fg: "#1a1a1a", hl: "rgba(48,140,198,0.2)"}, ["t"]);)js").arg(QLatin1String(document)),
                       [&pushed](const QVariant &) { pushed = true; });
    spinUntil([&]() { return pushed; });

    // The verdict waits for the pushed content in the DOM: a push that failed
    // to run answers with an empty editor, and that must fail the test, not
    // pass it.
    QString state;
    spinUntil([&]() {
        state.clear();
        page.runJavaScript(QStringLiteral("JSON.stringify({xss: window.__xss || null, xssScript: window.__xssScript || null,"
                                          " xssClick: window.__xssClick || null, xssHref: window.__xssHref || null, xssDoc: window.__xssDoc || null,"
                                          " text: (document.querySelector('.vditor-wysiwyg [contenteditable]') || {}).textContent || '',"
                                          " onattrs: document.querySelectorAll('.vditor-wysiwyg [contenteditable] [onclick]').length,"
                                          " liveHtml: Array.from(document.querySelectorAll('.vditor-wysiwyg [contenteditable] script,"
                                          " .vditor-wysiwyg [contenteditable] iframe, .vditor-wysiwyg [contenteditable] object,"
                                          " .vditor-wysiwyg [contenteditable] embed')).map(function(e){return e.outerHTML;}).join('|')})"),
                           [&state](const QVariant &v) { state = v.toString(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return state.contains(QLatin1String("Before"));
    });
    const QJsonDocument probe = QJsonDocument::fromJson(state.toUtf8());
    QVERIFY2(probe.isObject(), qPrintable(state));
    const QJsonObject fields = probe.object();
    QVERIFY2(fields.value("text").toString().contains(QLatin1String("Before")), qPrintable(state));
    // A payload that ran left a 1 behind; an absent or null flag reads 0.
    QCOMPARE(fields.value("xss").toInt(0), 0);
    QCOMPARE(fields.value("xssScript").toInt(0), 0);
    QCOMPARE(fields.value("xssClick").toInt(0), 0);
    QCOMPARE(fields.value("xssHref").toInt(0), 0);
    QCOMPARE(fields.value("xssDoc").toInt(0), 0);
    QCOMPARE(fields.value("onattrs").toInt(-1), 0);
    const QString liveHtml = fields.value("liveHtml").toString();
    QVERIFY2(!liveHtml.contains(QLatin1String("srcdoc")), qPrintable(liveHtml));
    QVERIFY2(!liveHtml.contains(QLatin1String("onerror")) && !liveHtml.contains(QLatin1String("onload")), qPrintable(liveHtml));
    QVERIFY2(!liveHtml.contains(QLatin1String("javascript:")), qPrintable(liveHtml));

    // --- The repeated-fragment anchor --------------------------------------
    //
    // A document that repeats a fragment (a running header, a copy of a
    // table) must not point the pull's search at the earlier twin — the delta
    // slice would assemble that twin's tail into the edited block's markdown,
    // and the model would layer it there. The push applies directly: the
    // probe typed nothing, so no dirty window parks it.
    bool pushedDups = false;
    page.runJavaScript(QStringLiteral(R"js(pushDocument("Intro\n\nIntro", {dark: false, bg: "#ffffff", fg: "#1a1a1a", hl: "rgba(48,140,198,0.2)"}, ["Intro", "Intro"]);)js"),
                       [&pushedDups](const QVariant &) { pushedDups = true; });
    spinUntil([&]() { return pushedDups; });

    // The dup content must be in the DOM before the caret goes looking for
    // its block: the push applies asynchronously.
    QString dupText;
    spinUntil([&]() {
        dupText.clear();
        page.runJavaScript(QStringLiteral("(document.querySelector('.vditor-wysiwyg [contenteditable]') || {}).textContent || ''"), [&dupText](const QVariant &v) { dupText = v.toString(); });
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return dupText.contains(QLatin1String("Intro"));
    });

    int reportedOrdinal = -1;
    QString reportedMarkdown;
    QObject::connect(&bridge, &EditorBridge::editorBlockChanged, [&](int ordinal, const QString &md, qint64) {
        reportedOrdinal = ordinal;
        reportedMarkdown = md;
    });

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

    spinUntil([&]() { return reportedOrdinal >= 0; }, 5000);
    QCOMPARE(reportedOrdinal, 1);
    // The slice belongs to the second block: its own text plus the keystroke,
    // not the first twin's tail.
    QCOMPARE(reportedMarkdown, QStringLiteral("Introx"));
}

int main(int argc, char *argv[])
{
    // Must run before the QGuiApplication is constructed.
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    TestEditorPull tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_editor_pull.moc"
