#include <QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include "app/Exporter.h"
#include "app/ExportRenderer.h"

using namespace llocr;

/**
 * @brief Integration test for the headless export renderer.
 *
 * Drives ExportRenderer (off-screen QWebEnginePage + the qrc:/preview bundle)
 * end to end: Markdown in → rendered HTML sections out, and printToPdf to a
 * real file. Requires the WebEngine runtime, so it is slower than the pure
 * unit tests.
 */
class ExportRendererTest : public QObject
{
    Q_OBJECT

private slots:
    void rendersHtmlWithHeadingsTablesAndMath();
    void rendersHtmlWithoutPageHeadingsWhenSplitOff();
    void printsPdfFile();
};

namespace {

void spinUntil(const std::function<bool()> &done, int timeoutMs = 60000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QVERIFY2(done(), "timed out waiting for the render callback");
}

}  // namespace

void ExportRendererTest::rendersHtmlWithHeadingsTablesAndMath()
{
    ExportRenderer renderer;
    QVERIFY(!renderer.isBusy());

    const QString md = QStringLiteral("# Heading\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n\n$$E=mc^2$$\n");

    bool finished = false;
    bool ok = false;
    QString html;
    ExportRenderer::Request request;
    request.output = ExportRenderer::Output::Html;
    request.pages = {{1, md}, {2, QStringLiteral("second page body")}};
    request.styleSheet = Exporter::exportStyleSheet();
    renderer.render(request, [&](bool success, const QString &result, const QString &) {
        ok = success;
        html = result;
        finished = true;
    });
    QVERIFY(renderer.isBusy());
    spinUntil([&]() { return finished; });

    QVERIFY2(ok, qPrintable(html));
    QVERIFY(!renderer.isBusy());
    // Real markdown structure, not escaped <pre> text. Pages are separated by
    // a rule element — no "Page N" labels.
    QVERIFY(html.contains(QStringLiteral("<h1")));
    QVERIFY(html.contains(QStringLiteral("<table")));
    QVERIFY(html.contains(QStringLiteral("export-page")));
    QVERIFY(html.contains(QStringLiteral("page-separator")));
    QVERIFY(html.contains(QStringLiteral("second page body")));
    QVERIFY(!html.contains(QStringLiteral("Page 1")));
    QVERIFY(!html.contains(QStringLiteral("Page 2")));
    // KaTeX rendered the formula.
    QVERIFY(html.contains(QStringLiteral("katex")));
    QVERIFY(html.contains(QStringLiteral("E=mc")));
}

void ExportRendererTest::rendersHtmlWithoutPageHeadingsWhenSplitOff()
{
    ExportRenderer renderer;
    QVERIFY(!renderer.isBusy());

    bool finished = false;
    bool ok = false;
    QString html;
    ExportRenderer::Request request;
    request.output = ExportRenderer::Output::Html;
    request.pages = {{1, QStringLiteral("body text")}};
    request.styleSheet = Exporter::exportStyleSheet(false);
    request.splitPages = false;
    renderer.render(request, [&](bool success, const QString &result, const QString &) {
        ok = success;
        html = result;
        finished = true;
    });
    spinUntil([&]() { return finished; });

    QVERIFY2(ok, qPrintable(html));
    QVERIFY(html.contains(QStringLiteral("body text")));
    QVERIFY(!html.contains(QStringLiteral("Page 1")));
    QVERIFY(!html.contains(QStringLiteral("page-separator")));
    QVERIFY(!request.styleSheet.contains(QStringLiteral("break-before:page")));
}

void ExportRendererTest::printsPdfFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("out.pdf");

    ExportRenderer renderer;
    bool finished = false;
    bool ok = false;
    QString error;
    ExportRenderer::Request request;
    request.output = ExportRenderer::Output::Pdf;
    request.pages = {{1, QStringLiteral("hello **world**")}};
    request.styleSheet = Exporter::exportStyleSheet();
    request.outputPath = path;
    request.pageLayout = Exporter::defaultPdfLayout();
    renderer.render(request, [&](bool success, const QString &, const QString &err) {
        ok = success;
        error = err;
        finished = true;
    });
    spinUntil([&]() { return finished; });

    QVERIFY2(ok, qPrintable(error));
    QFile pdf(path);
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    const QByteArray data = pdf.readAll();
    QVERIFY(data.startsWith("%PDF-"));
    QVERIFY(data.size() > 1000);
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_MACOS
    // Chromium's multi-process model registers a named Mach rendezvous server
    // in the bootstrap namespace; a restricted session (a sandbox, a
    // container, a shell without a GUI session) refuses the registration and
    // Chromium aborts with SIGTRAP — a crash dialog on the desktop, a red test
    // in the suite. The test renders the app's own local bundle, so the
    // renderer runs in the browser process: no rendezvous, no crash, and the
    // assertions stay about the rendered output, not about the process model.
    const QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags + (flags.isEmpty() ? "" : " ") + "--single-process");
#endif

    // Must run before the QGuiApplication is constructed.
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    ExportRendererTest tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_export_renderer.moc"
