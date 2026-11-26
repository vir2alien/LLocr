#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QWebEnginePage>

namespace {

class FakeUiController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool dark READ dark CONSTANT)
    Q_PROPERTY(QString language READ language CONSTANT)

public:
    bool dark() const { return false; }
    QString language() const { return QStringLiteral("ru"); }
};

}  // namespace

// The preview page, loaded for real: preview.html must pull the bundled
// marked + KaTeX + DOMPurify from qrc and render the page's markdown into
// structure — headings, tables, typeset formulas, inline images. The preview
// tab stands on this bundle, and the HTML/PDF export renders through the
// same one.
class TestPreviewPage : public QObject
{
    Q_OBJECT

private slots:
    void rendersTheMarkdownStructure();
    void theDarkThemeFollowsTheApp();
    void theWrapperPushesTheMarkdownAndTheTheme();

private:
    void spinUntil(const std::function<bool()> &done, int timeoutMs = 60000);
    void loadPage();
    void render(const QString &markdown, bool dark);
    QString evaluate(const QString &js);

    QWebEnginePage m_page;
    bool m_loaded = false;
    bool m_loadOk = false;
    bool m_pushed = false;
    QString m_value;
    FakeUiController m_uiController;
};

void TestPreviewPage::spinUntil(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    bool ok = false;
    while (!ok && timer.elapsed() < timeoutMs) {
        ok = done();
        if (!ok)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    QVERIFY2(ok, "timed out waiting for the preview page");
}

void TestPreviewPage::loadPage()
{
    m_loaded = false;
    m_loadOk = false;
    QObject::connect(&m_page, &QWebEnginePage::loadFinished, this, [&](bool ok) {
        m_loaded = true;
        m_loadOk = ok;
    });
    m_page.load(QUrl(QStringLiteral("qrc:/preview/preview.html")));
    spinUntil([&]() { return m_loaded; });
    QVERIFY2(m_loadOk, "preview.html failed to load");
}

void TestPreviewPage::render(const QString &markdown, bool dark)
{
    m_pushed = false;
    m_page.runJavaScript(QStringLiteral("render(%1, { dark: %2, bg: \"#123456\", fg: \"#abcdef\" });").arg(QJsonValue(markdown).toJson(), dark ? QStringLiteral("true") : QStringLiteral("false")),
                         [this](const QVariant &) { m_pushed = true; });
    spinUntil([&]() { return m_pushed; });
}

QString TestPreviewPage::evaluate(const QString &js)
{
    m_value.clear();
    m_page.runJavaScript(js, [this](const QVariant &v) { m_value = v.toString(); });
    spinUntil([&]() {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return !m_value.isEmpty();
    });
    return m_value;
}

void TestPreviewPage::rendersTheMarkdownStructure()
{
    loadPage();

    render(QStringLiteral("# Heading\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n\n$$E=mc^2$$\n\n"
                          "inline $x$ and ![crop](data:image/png;base64,AAAA)\n"),
           false);

    QCOMPARE(evaluate(QStringLiteral("document.querySelectorAll('h1').length")), QStringLiteral("1"));
    QCOMPARE(evaluate(QStringLiteral("document.querySelectorAll('table').length")), QStringLiteral("1"));
    QCOMPARE(evaluate(QStringLiteral("document.querySelectorAll('.katex').length > 0 ? 'yes' : 'no'")), QStringLiteral("yes"));
    QCOMPARE(evaluate(QStringLiteral("document.querySelectorAll('img[src^=\"data:image/png;base64,\"]').length")), QStringLiteral("1"));
    QCOMPARE(evaluate(QStringLiteral("document.body.textContent.includes('$$') ? 'yes' : 'no'")), QStringLiteral("no"));
}

void TestPreviewPage::theDarkThemeFollowsTheApp()
{
    loadPage();

    render(QStringLiteral("Body text\n"), true);
    QCOMPARE(evaluate(QStringLiteral("document.body.style.background")), QStringLiteral("rgb(18, 52, 86)"));
    QCOMPARE(evaluate(QStringLiteral("document.body.style.color")), QStringLiteral("rgb(171, 205, 239)"));
}

// The QML wrapper around the page: it must push the markdown and the app's
// colors into the page — the colors as colors, not as an empty object.
void TestPreviewPage::theWrapperPushesTheMarkdownAndTheTheme()
{
    const QString qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!qmlRoot.isEmpty());
    qmlRegisterSingletonType(QUrl::fromLocalFile(qmlRoot + QStringLiteral("/Theme.qml")), "LLocr", 1, 0, "Theme");
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", &m_uiController);

    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(qmlRoot + QStringLiteral("/MainWindow/MarkdownPreview.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));

    object->setProperty("markdown", QStringLiteral("# Title\n\n$$E=mc^2$$\n"));
    object->setProperty("dark", true);
    object->setProperty("bgColor", QColor(QStringLiteral("#123456")));
    object->setProperty("fgColor", QColor(QStringLiteral("#abcdef")));

    const QString script = object->property("_script").toString();
    QVERIFY2(script.startsWith(QStringLiteral("render(")), qPrintable(script));
    QVERIFY2(script.contains(QStringLiteral("# Title")), qPrintable(script));
    QVERIFY2(script.contains(QStringLiteral("\"dark\":true")), qPrintable(script));
    QVERIFY2(script.contains(QStringLiteral("\"bg\":\"#123456\"")), qPrintable(script));
    QVERIFY2(script.contains(QStringLiteral("\"fg\":\"#abcdef\"")), qPrintable(script));

    // The page itself must load from qrc: a wrapper with a broken bundle
    // shows a banner instead of the document.
    spinUntil([&]() {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return object->property("_pageReady").toBool() || !object->property("_loadError").toString().isEmpty();
    });
    QVERIFY2(object->property("_loadError").toString().isEmpty(), qPrintable(object->property("_loadError").toString()));
    QVERIFY(object->property("_pageReady").toBool());
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_MACOS
    // Chromium's multi-process model registers a named Mach rendezvous server
    // in the bootstrap namespace; a restricted session refuses the
    // registration and Chromium aborts with SIGTRAP. The renderer then runs in
    // the browser process, and the assertions stay about the rendered output.
    const QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags + (flags.isEmpty() ? "" : " ") + "--single-process");
#endif

    // Must run before the QGuiApplication is constructed.
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    TestPreviewPage tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_preview_page.moc"
