#include <QtTest>

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

QTEST_MAIN(ExporterTest)
#include "ExporterTest.moc"
