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

    const QString md = QStringLiteral(
        "# Heading\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n\n$$E=mc^2$$\n");

    bool finished = false;
    bool ok = false;
    QString html;
    ExportRenderer::Request request;
    request.output = ExportRenderer::Output::Html;
    request.pages = { { 1, md }, { 2, QStringLiteral("second page body") } };
    request.styleSheet = Exporter::exportStyleSheet();
    renderer.render(request,
                    [&](bool success, const QString &result, const QString &) {
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
    request.pages = { { 1, QStringLiteral("body text") } };
    request.styleSheet = Exporter::exportStyleSheet(false);
    request.splitPages = false;
    renderer.render(request,
                    [&](bool success, const QString &result, const QString &) {
        ok = success;
        html = result;
        finished = true;
    });
    spinUntil([&]() { return finished; });

    QVERIFY2(ok, qPrintable(html));
    QVERIFY(html.contains(QStringLiteral("body text")));
    QVERIFY(!html.contains(QStringLiteral("Page 1")));
    // Split off: no rule elements and no forced page breaks.
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
    request.pages = { { 1, QStringLiteral("hello **world**") } };
    request.styleSheet = Exporter::exportStyleSheet();
    request.outputPath = path;
    request.pageLayout = Exporter::defaultPdfLayout();
    renderer.render(request,
                    [&](bool success, const QString &, const QString &err) {
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
    // Must run before the QGuiApplication is constructed.
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);

    ExportRendererTest tc;
    return QTest::qExec(&tc, argc, argv);
}
#include "test_export_renderer.moc"
