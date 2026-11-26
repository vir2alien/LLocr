#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QtTest>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QWebChannel>
#include <QWebEnginePage>

#include "app/EditorBridge.h"

using namespace llocr;

namespace {

constexpr const char *kTheme = "{dark: false, bg: \"#ffffff\", fg: \"#1a1a1a\", hl: \"rgba(48,140,198,0.2)\"}";

QString jsString(const QString &text)
{
    QJsonArray array;
    array.append(text);
    const QByteArray json = QJsonDocument(array).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

QString jsStringList(const QStringList &list)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(list)).toJson(QJsonDocument::Compact));
}

// The keystroke lands at the end of the block's last text node — where the
// caret stands after typing a line — the way the user's does.
constexpr const char *kTypeAtTheEndOfBlock = R"js((function(){
    var block = document.querySelector('.vditor-wysiwyg [data-ll-block="%1"]');
    if (!block) return false;
    document.querySelector('.vditor-wysiwyg [contenteditable]').focus();
    var walker = document.createTreeWalker(block, NodeFilter.SHOW_TEXT);
    var node, last = null;
    while ((node = walker.nextNode())) last = node;
    if (!last) return false;
    var sel = window.getSelection();
    var range = document.createRange();
    range.setStart(last, last.length);
    range.collapse(true);
    sel.removeAllRanges();
    sel.addRange(range);
    document.execCommand('insertText', false, %2);
    return true;
})())js";

// Where the caret is: 1 at the end of a text node of block 0 that carries the
// typed text, 2 elsewhere inside a block, 3 outside every block (the root of
// a rebuilt content), 0 no selection. A number: a JS string handed to a
// QVariant in a tight loop corrupts the renderer heap under the offscreen
// platform.
constexpr const char *kCaret = R"js((function(){
    var sel = window.getSelection();
    if (!sel || !sel.anchorNode) return 0;
    var el = sel.anchorNode.nodeType === 1 ? sel.anchorNode : sel.anchorNode.parentElement;
    var block = el.closest('[data-ll-block]');
    if (!block) return 3;
    if (parseInt(block.getAttribute('data-ll-block'), 10) !== 0) return 2;
    var text = sel.anchorNode.nodeValue || '';
    return (text.indexOf(%1) >= 0 && sel.anchorOffset === text.length) ? 1 : 2;
})())js";

}  // namespace

// The untrusted-content gate, the per-block pull's repeated-fragment anchor
// and the push that answers a pull, all on one page. The suite is a separate
// binary from test_editor_page and keeps a single page on purpose: the
// offscreen WebEngine renderer SIGTRAPs when a second page is spawned inside
// the same process (the "Mach rendezvous" failure the existing tests work
// around by keeping their page count low and their order fixed). The page's
// state outlives a test — a dirty window is sticky until its settle — so
// every test opens on its own document through openDocument() and reads the
// page through the spies init() resets.
class TestEditorPull : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void rawHtmlIsInertAndRepeatedFragmentsAnchorLocally();
    void thePushThatAnswersAPullLeavesTheCaretWhereItIs();

private:
    void spinUntil(const std::function<bool()> &done, int timeoutMs = 60000);
    bool evaluate(const QString &script, QVariant *value);
    void pushDocument(const QStringList &fragments);
    void openDocument(const QStringList &fragments);

    EditorBridge m_bridge;
    QWebChannel m_channel;
    QWebEnginePage m_page;

    int m_references = 0;
    qint64 m_token = -1;
    int m_resyncs = 0;
    int m_pulledOrdinal = -1;
    QString m_pulledMarkdown;
    qint64 m_pulledToken = -1;
};

void TestEditorPull::initTestCase()
{
    m_channel.registerObject(QStringLiteral("editorBridge"), &m_bridge);
    m_page.setWebChannel(&m_channel);

    bool loaded = false;
    bool loadOk = false;
    bool ready = false;
    QObject::connect(&m_page, &QWebEnginePage::loadFinished, [&](bool ok) {
        loaded = true;
        loadOk = ok;
    });
    QObject::connect(&m_bridge, &EditorBridge::pageReady, [&ready]() { ready = true; });
    // The spies capture this: they stay connected for the whole run, and a
    // lambda that captured a test's locals would dangle after it returned.
    QObject::connect(&m_bridge, &EditorBridge::editorReferenceChanged, [this](const QString &, qint64 pushToken) {
        ++m_references;
        m_token = pushToken;
    });
    QObject::connect(&m_bridge, &EditorBridge::editorResyncRequested, [this]() { ++m_resyncs; });
    QObject::connect(&m_bridge, &EditorBridge::editorBlockChanged, [this](int blockOrdinal, const QString &markdown, qint64 pushToken) {
        m_pulledOrdinal = blockOrdinal;
        m_pulledMarkdown = markdown;
        m_pulledToken = pushToken;
    });

    m_page.load(QUrl(QStringLiteral("qrc:/editor/editor.html")));
    spinUntil([&]() { return loaded; });
    QVERIFY2(loadOk, "editor.html failed to load");
    spinUntil([&]() { return ready; });
}

void TestEditorPull::init()
{
    m_references = 0;
    m_token = -1;
    m_resyncs = 0;
    m_pulledOrdinal = -1;
    m_pulledMarkdown.clear();
    m_pulledToken = -1;
}

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

bool TestEditorPull::evaluate(const QString &script, QVariant *value)
{
    bool done = false;
    m_page.runJavaScript(script, [value, &done](const QVariant &result) {
        *value = result;
        done = true;
    });
    QElapsedTimer timer;
    timer.start();
    while (!done && timer.elapsed() < 60000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return done;
}

void TestEditorPull::pushDocument(const QStringList &fragments)
{
    bool pushed = false;
    m_page.runJavaScript(QStringLiteral("pushDocument(%1, %2, %3);").arg(jsString(fragments.join(QStringLiteral("\n\n"))), QLatin1String(kTheme), jsStringList(fragments)),
                         [&pushed](const QVariant &) { pushed = true; });
    spinUntil([&]() { return pushed; });
}

// The document a test opens on. A page the previous test left dirty parks
// the push and asks again at its settle: the asked-for push is the one the
// QML wrapper answers, and it is the one that builds the DOM.
void TestEditorPull::openDocument(const QStringList &fragments)
{
    const int resyncsBefore = m_resyncs;
    pushDocument(fragments);
    QElapsedTimer timer;
    timer.start();
    while (m_token < 0 && m_resyncs == resyncsBefore && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (m_token < 0 && m_resyncs > resyncsBefore)
        pushDocument(fragments);
    spinUntil([&]() { return m_token >= 0; }, 5000);
}

void TestEditorPull::rawHtmlIsInertAndRepeatedFragmentsAnchorLocally()
{
    // --- The untrusted-content gate ---------------------------------------
    //
    // The markdown the page renders comes from an OCR model: raw HTML inside
    // it is untrusted input. Whatever the document carries, no inline handler
    // may run and no payload may survive on the elements the sanitizer lets
    // through — the bridge the page owns writes, moves and removes blocks.
    const char *document =
        R"js(Before <img src=x onerror=\"window.__xss=1\"> after\n\n<script>window.__xssScript = 1;</script>\n\n<iframe srcdoc=\"&lt;script&gt;parent.__xssDoc = 1;&lt;/script&gt;\"></iframe>\n\n<table><tr><td onclick=\"window.__xssClick = 1\">cell</td></tr></table>\n\n<a href=\"javascript:window.__xssHref = 1\">link</a>)js";
    bool pushed = false;
    m_page.runJavaScript(QStringLiteral(R"js(pushDocument("%1", {dark: false, bg: "#ffffff", fg: "#1a1a1a", hl: "rgba(48,140,198,0.2)"}, ["t"]);)js").arg(QLatin1String(document)),
                         [&pushed](const QVariant &) { pushed = true; });
    spinUntil([&]() { return pushed; });

    // The verdict waits for the pushed content in the DOM: a push that failed
    // to run answers with an empty editor, and that must fail the test, not
    // pass it.
    QString state;
    spinUntil([&]() {
        state.clear();
        m_page.runJavaScript(QStringLiteral("JSON.stringify({xss: window.__xss || null, xssScript: window.__xssScript || null,"
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
    // and the model would layer it there.
    openDocument({QStringLiteral("Intro"), QStringLiteral("Intro")});

    bool typed = false;
    m_page.runJavaScript(QStringLiteral(R"js((function(){
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

    spinUntil([&]() { return m_pulledOrdinal >= 0; }, 5000);
    QCOMPARE(m_pulledOrdinal, 1);
    // The slice belongs to the second block: its own text plus the keystroke,
    // not the first twin's tail.
    QCOMPARE(m_pulledMarkdown, QStringLiteral("Introx"));
}

// The user's flow on a page the editor does not serialize back verbatim — a
// display formula closes it, and the editor's serialization ends with a
// newline the model's join lacks. A keystroke pulls the block; the model's
// answer parks while the page is dirty and comes back at the settle. That
// push carries the page's own text and must leave the DOM alone: rebuilt, it
// drops the caret to the start of the content — the next keystroke lands in
// the first block.
void TestEditorPull::thePushThatAnswersAPullLeavesTheCaretWhereItIs()
{
    const QStringList fragments = {QStringLiteral("First"), QStringLiteral("$$\nE = mc^2\n$$")};
    openDocument(fragments);
    const qint64 token = m_token;

    // The premise: the editor's serialization of the pushed document is not
    // the document. One read of the serializer is safe; in a loop it crashes
    // the renderer under the offscreen platform.
    QVariant gap;
    QVERIFY(evaluate(QStringLiteral("vditor.getValue().length - %1").arg(fragments.join(QStringLiteral("\n\n")).size()), &gap));
    QVERIFY2(gap.toInt() != 0, "the document must be one the editor re-serializes differently");

    QVariant typed;
    QVERIFY(evaluate(QString::fromUtf8(kTypeAtTheEndOfBlock).arg(0).arg(jsString(QStringLiteral("x"))), &typed));
    QVERIFY(typed.toBool());
    spinUntil([&]() { return m_pulledOrdinal >= 0; }, 5000);
    QCOMPARE(m_pulledOrdinal, 0);
    QCOMPARE(m_pulledMarkdown, QStringLiteral("Firstx"));
    QCOMPARE(m_pulledToken, token);

    // The model writes the layer and its join answers while the page is still
    // dirty: the push parks, and the settle asks for it again.
    const QStringList answered = {QStringLiteral("Firstx"), fragments.last()};
    const int resyncsBefore = m_resyncs;
    pushDocument(answered);
    spinUntil([&]() { return m_resyncs > resyncsBefore; }, 5000);

    // The asked-for push: what the QML wrapper sends back.
    const int referencesBefore = m_references;
    pushDocument(answered);
    QTest::qWait(600);

    // A rebuild answers with a new reference; the caret tells the same story.
    QCOMPARE(m_references, referencesBefore);
    QVariant caret;
    QVERIFY(evaluate(QString::fromUtf8(kCaret).arg(jsString(QStringLiteral("Firstx"))), &caret));
    QCOMPARE(caret.toInt(), 1);

    // The typing session goes on in the same block, on the same push.
    m_pulledOrdinal = -1;
    QVERIFY(evaluate(QString::fromUtf8(kTypeAtTheEndOfBlock).arg(0).arg(jsString(QStringLiteral("y"))), &typed));
    QVERIFY(typed.toBool());
    spinUntil([&]() { return m_pulledOrdinal >= 0; }, 5000);
    QCOMPARE(m_pulledMarkdown, QStringLiteral("Firstxy"));
    QCOMPARE(m_pulledToken, token);
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
