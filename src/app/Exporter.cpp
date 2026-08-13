#include "app/Exporter.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>

#include <QPageSize>
#include <QPdfWriter>
#include <QTextDocument>

namespace llocr {


Exporter::Format Exporter::formatForSuffix(const QString& suffix)
{
    const QString s = suffix.toLower();
    if (s == QStringLiteral("md") || s == QStringLiteral("markdown"))
        return Format::Markdown;
    if (s == QStringLiteral("txt") || s == QStringLiteral("text"))
        return Format::PlainText;
    if (s == QStringLiteral("html") || s == QStringLiteral("htm"))
        return Format::Html;
    if (s == QStringLiteral("docx"))
        return Format::Docx;
    if (s == QStringLiteral("pdf"))
        return Format::Pdf;
    return Format::Unknown;
}

QStringList Exporter::nativeSuffixes()
{
    return { QStringLiteral("txt"), QStringLiteral("md"),
             QStringLiteral("html"), QStringLiteral("pdf") };
}

QStringList Exporter::pandocSuffixes()
{
    return { QStringLiteral("docx") };
}

QString Exporter::pandocExecutable()
{
    static const QString exe = QStandardPaths::findExecutable(QStringLiteral("pandoc"));
    return exe;
}

bool Exporter::isPandocAvailable()
{
    return !pandocExecutable().isEmpty();
}

QString Exporter::buildMarkdown(const QList<Page>& pages)
{
    QString out;
    bool first = true;
    for (const Page& page : pages) {
        if (!first)
            out += QStringLiteral("\n\n");
        first = false;
        out += QStringLiteral("## Page %1\n\n").arg(page.number);
        out += page.text.trimmed();
        out += QChar('\n');
    }
    return out;
}

QString Exporter::buildPlainText(const QList<Page>& pages)
{
    QString out;
    for (const Page& page : pages) {
        out += QStringLiteral("===== Page %1 =====\n").arg(page.number);
        out += page.text.trimmed();
        out += QStringLiteral("\n\n");
    }
    return out;
}

QString Exporter::buildHtml(const QList<Page>& pages)
{
    QString body;
    for (const Page& page : pages) {
        body += QStringLiteral("<section>\n<h2>Page %1</h2>\n<pre>%2</pre>\n</section>\n")
                    .arg(page.number)
                    .arg(page.text.toHtmlEscaped());
    }
    return QStringLiteral(
               "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
               "<title>OCR result</title>\n"
               "<style>body{font-family:sans-serif;margin:2em;}"
               "pre{white-space:pre-wrap;font-family:inherit;}"
               "h2{border-bottom:1px solid #ccc;padding-bottom:.2em;}</style>\n"
               "</head>\n<body>\n%1</body>\n</html>\n")
        .arg(body);
}

Exporter::Result Exporter::exportToFile(const QList<Page>& pages, const QString& filePath) const
{
    if (pages.isEmpty())
        return Result::fail(QCoreApplication::translate("Exporter", "Nothing to export."));

    if (filePath.isEmpty())
        return Result::fail(QCoreApplication::translate("Exporter", "No output path."));

    const Format format = formatForSuffix(QFileInfo(filePath).suffix());

    switch (format) {
    case Format::Markdown:
        return writeTextFile(filePath, buildMarkdown(pages));
    case Format::PlainText:
        return writeTextFile(filePath, buildPlainText(pages));
    case Format::Html:
        return writeTextFile(filePath, buildHtml(pages));

    case Format::Docx: {
        if (!isPandocAvailable())
            return Result::fail(QCoreApplication::translate("Exporter",
                "DOCX export requires Pandoc, which was not found on PATH. "
                "Install it from pandoc.org, or export to Markdown/HTML instead."));
        return runPandoc(buildMarkdown(pages), filePath, {});
    }

    case Format::Pdf: {
        if (isPandocAvailable()) {
            const Result r = runPandoc(buildMarkdown(pages), filePath, {});
            if (r.success)
                return r;
            const Result fb = writePdfFallback(pages, filePath);
            if (fb.success)
                return Result::ok(QCoreApplication::translate("Exporter",
                    "Exported PDF using the built-in writer "
                    "(Pandoc failed: %1).").arg(r.message));
            return fb;
        }
        return writePdfFallback(pages, filePath);
    }

    case Format::Unknown:
    default:
        return writeTextFile(filePath, buildMarkdown(pages));
    }
}

Exporter::Result Exporter::writeTextFile(const QString& path, const QString& content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return Result::fail(QCoreApplication::translate("Exporter", "Cannot write file: %1").arg(path));

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    file.close();
    return Result::ok(QCoreApplication::translate("Exporter", "Exported to %1").arg(QFileInfo(path).fileName()));
}

Exporter::Result Exporter::runPandoc(const QString& markdown,
                                     const QString& outputPath,
                                     const QStringList& extraArgs)
{
    const QString exe = pandocExecutable();
    if (exe.isEmpty())
        return Result::fail(QCoreApplication::translate("Exporter", "Pandoc not found."));

    QStringList args;
    args << QStringLiteral("--from=markdown")
         << QStringLiteral("--standalone")
         << QStringLiteral("--output") << outputPath
         << extraArgs;

    QProcess process;
    process.start(exe, args);
    if (!process.waitForStarted(5000))
        return Result::fail(QCoreApplication::translate("Exporter", "Failed to start Pandoc."));

    process.write(markdown.toUtf8());
    process.closeWriteChannel();

    if (!process.waitForFinished(120000)) {
        process.kill();
        return Result::fail(QCoreApplication::translate("Exporter", "Pandoc timed out."));
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString err = QString::fromUtf8(process.readAllStandardError()).trimmed();
        return Result::fail(err.isEmpty()
                                ? QCoreApplication::translate("Exporter", "Pandoc failed (exit %1).").arg(process.exitCode())
                                : err);
    }

    return Result::ok(QCoreApplication::translate("Exporter", "Exported to %1").arg(QFileInfo(outputPath).fileName()));
}

Exporter::Result Exporter::writePdfFallback(const QList<Page>& pages, const QString& path)
{
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(300);

    QTextDocument doc;
    doc.setDefaultStyleSheet(QStringLiteral(
        "h2{font-size:14pt;margin-top:16pt;} pre{white-space:pre-wrap;}"));
    doc.setHtml(buildHtml(pages));
    doc.print(&writer);

    QFileInfo info(path);
    if (!info.exists() || info.size() == 0)
        return Result::fail(QCoreApplication::translate("Exporter", "Failed to write PDF: %1").arg(path));

    return Result::ok(QCoreApplication::translate("Exporter", "Exported to %1").arg(info.fileName()));
}

} // namespace llocr
