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
    void exportDocxEmbedsCroppedImages();
    void plainTextStripsImageReferences();

    void embedImagesAsDataUrlsConvertsRefs();
    void embedImagesAsDataUrlsKeepsNullCrops();
    void assembleHtmlDocumentIsSelfContained();
    void exportStyleSheetHasPrintRules();
    void pandocDetectionConsistent();
    void splitPagesOffOmitsPageLabels();
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
    // Pages are separated by a horizontal rule, with no "Page N" labels.
    QVERIFY(!md.contains("Page"));
    QVERIFY(md.contains("\n---\n"));
    QVERIFY(md.contains("First page body"));
    QVERIFY(md.contains("Second page body"));
    // Page 1 body must come before the rule, page 2 body after it.
    QVERIFY(md.indexOf("First page body") < md.indexOf("\n---\n"));
    QVERIFY(md.indexOf("\n---\n") < md.indexOf("Second page body"));
}

void ExporterTest::plainTextHasSeparators()
{
    const QList<Exporter::Page> pages = { { 1, "hello" } };
    const QString txt = Exporter::buildPlainText(pages);
    // A single page exports as-is: no separators and no page labels.
    QVERIFY(!txt.contains("Page"));
    QVERIFY(txt.contains("hello"));
    QVERIFY(!txt.contains("## Page"));  // no Markdown syntax leaked in

    // Several pages get a plain dash rule between them (no "Page N" labels).
    const QString multi = Exporter::buildPlainText({ { 1, "one" }, { 2, "two" } });
    QVERIFY(!multi.contains("Page"));
    QVERIFY(multi.contains("--------"));
    QVERIFY(multi.indexOf("one") < multi.indexOf("--------"));
    QVERIFY(multi.indexOf("--------") < multi.indexOf("two"));
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
    QVERIFY(QString::fromUtf8(f.readAll()).contains("body"));
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

void ExporterTest::exportDocxEmbedsCroppedImages()
{
    if (!Exporter::isPandocAvailable())
        QSKIP("pandoc is not available on this machine");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("result.docx");

    QImage img(40, 20, QImage::Format_RGB32);
    img.fill(Qt::blue);
    const auto crop = [&img](int, int) { return img; };

    Exporter exporter;
    const Exporter::Result r =
        exporter.exportToFile({ { 1, "![Image](image://ocr/crop/0)" } }, path, crop);
    QVERIFY2(r.success, qPrintable(r.message));

    // A .docx is a ZIP; entry names are stored uncompressed, so the embedded
    // crop must be visible as a word/media/ entry in the raw bytes.
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray raw = f.readAll();
    QVERIFY2(raw.contains("word/media/"),
             "docx contains no embedded images (pandoc did not resolve the crops)");
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

void ExporterTest::pandocDetectionConsistent()
{
    const QString exe = Exporter::pandocExecutable();
    QCOMPARE(Exporter::isPandocAvailable(), !exe.isEmpty());
    if (exe.isEmpty())
        QSKIP("pandoc is not installed on this machine");

    // Whatever the discovery path (PATH or the well-known install dirs),
    // the result must point at a real executable file.
    const QFileInfo info(exe);
    QVERIFY2(info.isFile(), qPrintable(exe));
    QVERIFY(info.exists());
}

void ExporterTest::splitPagesOffOmitsPageLabels()
{
    const QList<Exporter::Page> pages = {
        { 1, "first" },
        { 2, "second" },
    };

    // Split ON (default): pages are separated by a markdown rule…
    const QString md = Exporter::buildMarkdown(pages);
    QVERIFY(!md.contains("Page"));
    QVERIFY(md.contains("\n---\n"));
    QVERIFY(md.contains("first"));
    QVERIFY(md.contains("second"));

    // …a plain dash rule for TXT…
    const QString txt = Exporter::buildPlainText(pages);
    QVERIFY(!txt.contains("Page"));
    QVERIFY(txt.contains("--------"));

    // …and an <hr> for the basic HTML writer.
    const QString html = Exporter::buildHtml(pages);
    QVERIFY(!html.contains("<h2>Page"));
    QVERIFY(html.contains("<hr>"));
    QVERIFY(html.contains("first"));

    // Split OFF: pages flow continuously without any separators.
    const QString mdJoined = Exporter::buildMarkdown(pages, false);
    QVERIFY(!mdJoined.contains("---"));
    QVERIFY(mdJoined.contains("first"));
    QVERIFY(mdJoined.contains("second"));
    QVERIFY(!Exporter::buildPlainText(pages, false).contains("--------"));
    QVERIFY(!Exporter::buildHtml(pages, false).contains("<hr>"));

    // The stylesheet only forces a page break per section while splitting.
    QVERIFY(Exporter::exportStyleSheet(true).contains("break-before:page"));
    QVERIFY(!Exporter::exportStyleSheet(false).contains("break-before:page"));
}

QTEST_MAIN(ExporterTest)
#include "test_exporter.moc"
