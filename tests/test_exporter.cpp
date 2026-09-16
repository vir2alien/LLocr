#include <QtTest>

#include <QImage>

#include "app/Exporter.h"

using namespace llocr;

/**
 * @brief Unit tests for the Exporter rendering + format mapping.
 *
 * These cover the pure, tool-independent parts (Markdown/TXT/HTML rendering
 * and suffix->format mapping). Pandoc-dependent paths are exercised only when
 * a pandoc executable is present on the test machine.
 */
class ExporterTest : public QObject
{
    Q_OBJECT

private slots:
    void suffixMapping_data();
    void suffixMapping();

    void markdownHasPageHeadings();
    void plainTextHasSeparators();
    void htmlEscapesAngleBrackets();
    void htmlIsSelfContained();

    void exportMarkdownFileRoundTrips();
    void unknownSuffixFallsBackToMarkdown();
    void emptyPagesFail();

    void resolveImageReferencesReplacesUrlsAndSavesFiles();
    void resolveImageReferencesKeepsStaleRefs();
    void exportMarkdownEmbedsCroppedImages();
    void exportHtmlRendersImages();
    void plainTextStripsImageReferences();

    void embedImagesAsDataUrlsConvertsRefs();
    void embedImagesAsDataUrlsKeepsNullCrops();
    void assembleHtmlDocumentIsSelfContained();
    void exportStyleSheetHasPrintRules();
};

void ExporterTest::suffixMapping_data()
{
    QTest::addColumn<QString>("suffix");
    QTest::addColumn<int>("format");

    QTest::newRow("md")   << "md"       << int(Exporter::Format::Markdown);
    QTest::newRow("markdown") << "markdown" << int(Exporter::Format::Markdown);
    QTest::newRow("txt")  << "txt"      << int(Exporter::Format::PlainText);
    QTest::newRow("html") << "html"     << int(Exporter::Format::Html);
    QTest::newRow("htm")  << "htm"      << int(Exporter::Format::Html);
    QTest::newRow("docx") << "docx"     << int(Exporter::Format::Docx);
    QTest::newRow("pdf")  << "pdf"      << int(Exporter::Format::Pdf);
    QTest::newRow("upper-PDF") << "PDF" << int(Exporter::Format::Pdf);
    QTest::newRow("bogus") << "xyz"     << int(Exporter::Format::Unknown);
}

void ExporterTest::suffixMapping()
{
    QFETCH(QString, suffix);
    QFETCH(int, format);
    QCOMPARE(int(Exporter::formatForSuffix(suffix)), format);
}

void ExporterTest::markdownHasPageHeadings()
{
    const QList<Exporter::Page> pages = {
        { 1, "First page body" },
        { 2, "Second page body" },
    };
    const QString md = Exporter::buildMarkdown(pages);
    QVERIFY(md.contains("## Page 1"));
    QVERIFY(md.contains("## Page 2"));
    QVERIFY(md.contains("First page body"));
    QVERIFY(md.contains("Second page body"));
    // Page 1 heading must come before page 2 heading.
    QVERIFY(md.indexOf("## Page 1") < md.indexOf("## Page 2"));
}

void ExporterTest::plainTextHasSeparators()
{
    const QList<Exporter::Page> pages = { { 1, "hello" } };
    const QString txt = Exporter::buildPlainText(pages);
    QVERIFY(txt.contains("===== Page 1 ====="));
    QVERIFY(txt.contains("hello"));
    QVERIFY(!txt.contains("## Page"));  // no Markdown syntax leaked in
}

void ExporterTest::htmlEscapesAngleBrackets()
{
    const QList<Exporter::Page> pages = { { 1, "a < b && c > d" } };
    const QString html = Exporter::buildHtml(pages);
    QVERIFY(html.contains("a &lt; b &amp;&amp; c &gt; d"));
    QVERIFY(!html.contains("a < b &&"));  // raw text must not survive
}

void ExporterTest::htmlIsSelfContained()
{
    const QList<Exporter::Page> pages = { { 1, "x" } };
    const QString html = Exporter::buildHtml(pages);
    QVERIFY(html.startsWith("<!DOCTYPE html>"));
    QVERIFY(html.contains("<meta charset=\"utf-8\">"));
    QVERIFY(html.trimmed().endsWith("</html>"));
}

void ExporterTest::exportMarkdownFileRoundTrips()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("out.md");

    Exporter exporter;
    const Exporter::Result r =
        exporter.exportToFile({ { 1, "content here" } }, path);
    QVERIFY2(r.success, qPrintable(r.message));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString written = QString::fromUtf8(f.readAll());
    QVERIFY(written.contains("## Page 1"));
    QVERIFY(written.contains("content here"));
}

void ExporterTest::unknownSuffixFallsBackToMarkdown()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("out.weird");

    Exporter exporter;
    const Exporter::Result r =
        exporter.exportToFile({ { 1, "body" } }, path);
    QVERIFY2(r.success, qPrintable(r.message));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    QVERIFY(QString::fromUtf8(f.readAll()).contains("## Page 1"));
}

void ExporterTest::emptyPagesFail()
{
    Exporter exporter;
    QTemporaryDir dir;
    const Exporter::Result r = exporter.exportToFile({}, dir.filePath("x.md"));
    QVERIFY(!r.success);
}

void ExporterTest::resolveImageReferencesReplacesUrlsAndSavesFiles()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const auto crop = [](int boxIndex) {
        Q_UNUSED(boxIndex);
        QImage img(40, 20, QImage::Format_RGB32);
        img.fill(Qt::blue);
        return img;
    };
    const QString md = QStringLiteral(
        "Intro\n\n![Figure 1](image://ocr/crop/3)\n\nOutro");

    const Exporter::ResolvedImages r =
        Exporter::resolveImageReferences(md, 0, crop, dir.path());

    QVERIFY(r.processedMarkdown.contains("![Figure 1](page_0_img_3.png)"));
    QVERIFY(!r.processedMarkdown.contains("image://ocr"));
    QVERIFY(r.processedMarkdown.contains("Intro"));
    QVERIFY(r.processedMarkdown.contains("Outro"));
    QVERIFY(QFile::exists(dir.filePath("page_0_img_3.png")));
}

void ExporterTest::resolveImageReferencesKeepsStaleRefs()
{
    QTemporaryDir dir;
    // The crop provider always returns a null image (e.g. box was removed).
    const auto crop = [](int) { return QImage(); };
    const Exporter::ResolvedImages r =
        Exporter::resolveImageReferences("![X](image://ocr/crop/5)", 0, crop, dir.path());

    QVERIFY(r.processedMarkdown.contains("![X](image://ocr/crop/5)"));
}

void ExporterTest::exportMarkdownEmbedsCroppedImages()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("result.md");

    QImage img(40, 20, QImage::Format_RGB32);
    img.fill(Qt::red);
    const auto crop = [&img](int, int) { return img; };

    Exporter exporter;
    const Exporter::Result r =
        exporter.exportToFile({ { 1, "![Image](image://ocr/crop/0)" } }, path, crop);
    QVERIFY2(r.success, qPrintable(r.message));

    // Crops are saved into <output>_media/ next to the markdown file.
    const QString mediaFile = dir.filePath("result_media/page_0_img_0.png");
    QVERIFY(QFile::exists(mediaFile));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(f.readAll());
    QVERIFY(content.contains("![Image](result_media/page_0_img_0.png)"));
    QVERIFY(!content.contains("image://ocr"));
}

void ExporterTest::exportHtmlRendersImages()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("result.html");

    QImage img(40, 20, QImage::Format_RGB32);
    img.fill(Qt::green);
    const auto crop = [&img](int, int) { return img; };

    Exporter exporter;
    const Exporter::Result r =
        exporter.exportToFile({ { 1, "![Image](image://ocr/crop/0)" } }, path, crop);
    QVERIFY2(r.success, qPrintable(r.message));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString content = QString::fromUtf8(f.readAll());
    // The markdown image becomes a real <img> pointing at the saved crop.
    QVERIFY(content.contains("<img"));
    QVERIFY(content.contains("result_media/page_0_img_0.png"));
    QVERIFY(!content.contains("image://ocr"));
}

void ExporterTest::plainTextStripsImageReferences()
{
    const QString txt = Exporter::buildPlainText(
        { { 1, "![Figure](image://ocr/crop/0)\nBody text" } });
    QVERIFY(!txt.contains("image://ocr"));
    QVERIFY(!txt.contains("![Figure]"));
    QVERIFY(txt.contains("Body text"));
}

void ExporterTest::embedImagesAsDataUrlsConvertsRefs()
{
    QImage img(4, 2, QImage::Format_RGB32);
    img.fill(Qt::red);
    const QString md = QStringLiteral("A\n\n![Fig](image://ocr/crop/2)\n\nB");
    const QString out = Exporter::embedImagesAsDataUrls(md, [&img](int boxIndex) {
        return boxIndex == 2 ? img : QImage();
    });

    QVERIFY(out.contains(QStringLiteral("A")));
    QVERIFY(out.contains(QStringLiteral("B")));
    QVERIFY(!out.contains(QStringLiteral("image://ocr")));
    QVERIFY(out.contains(QStringLiteral("![Fig](data:image/png;base64,")));
}

void ExporterTest::embedImagesAsDataUrlsKeepsNullCrops()
{
    const QString md = QStringLiteral("![X](image://ocr/crop/5)");
    const QString out =
        Exporter::embedImagesAsDataUrls(md, [](int) { return QImage(); });
    QCOMPARE(out, md);
}

void ExporterTest::assembleHtmlDocumentIsSelfContained()
{
    const QString html = Exporter::assembleHtmlDocument(
        { QStringLiteral("<section><h2>Page 1</h2><p>hi</p></section>") });

    QVERIFY(html.startsWith(QStringLiteral("<!DOCTYPE html>")));
    QVERIFY(html.contains(QStringLiteral("<meta charset=\"utf-8\">")));
    QVERIFY(html.contains(QStringLiteral("<section><h2>Page 1</h2><p>hi</p></section>")));
    // KaTeX CSS is inlined with the bundled font as a data: URI…
    QVERIFY(html.contains(QStringLiteral("data:font/woff2;base64,")));
    QVERIFY(!html.contains(QStringLiteral("url(fonts/")));
    // …and the woff/ttf sources are dropped.
    QVERIFY(!html.contains(QStringLiteral("truetype")));
    // The shared stylesheet (with the print rules) is embedded as well.
    QVERIFY(html.contains(QStringLiteral("@media print")));
    QVERIFY(html.trimmed().endsWith(QStringLiteral("</html>")));
}

void ExporterTest::exportStyleSheetHasPrintRules()
{
    const QString css = Exporter::exportStyleSheet();
    QVERIFY(css.contains(QStringLiteral("@media print")));
    QVERIFY(css.contains(QStringLiteral(".export-page")));
    QVERIFY(css.contains(QStringLiteral("break-inside:avoid")));
}

QTEST_MAIN(ExporterTest)
#include "test_exporter.moc"
