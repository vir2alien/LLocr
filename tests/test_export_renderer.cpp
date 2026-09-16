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
    renderer.render(ExportRenderer::Output::Html,
                    { { 1, md } }, Exporter::exportStyleSheet(), QString(),
                    [&](bool success, const QString &result, const QString &) {
                        ok = success;
                        html = result;
                        finished = true;
                    });
    QVERIFY(renderer.isBusy());
    spinUntil([&]() { return finished; });

    QVERIFY2(ok, qPrintable(html));
    QVERIFY(!renderer.isBusy());
    // Real markdown structure, not escaped <pre> text.
    QVERIFY(html.contains(QStringLiteral("<h1")));
    QVERIFY(html.contains(QStringLiteral("<table")));
    QVERIFY(html.contains(QStringLiteral("export-page")));
    // KaTeX rendered the formula.
    QVERIFY(html.contains(QStringLiteral("katex")));
    QVERIFY(html.contains(QStringLiteral("E=mc")));
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
    renderer.render(ExportRenderer::Output::Pdf,
                    { { 1, QStringLiteral("hello **world**") } },
                    Exporter::exportStyleSheet(), path,
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
