#include "app/Exporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>

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
    // A plain-text file cannot carry images, so drop the image block refs.
    const QRegularExpression re = imageRefRegex();
    QString out;
    for (const Page& page : pages) {
        out += QStringLiteral("===== Page %1 =====\n").arg(page.number);
        QString text = page.text.trimmed();
        text.remove(re);
        out += text;
        out += QStringLiteral("\n\n");
    }
    return out;
}

QRegularExpression Exporter::imageRefRegex()
{
    // Matches a Markdown image whose source is image://ocr/crop/<boxIndex>
    // (optionally a two-part crop/<page>/<box> form).
    static const QRegularExpression re(
        QStringLiteral(R"(!\[([^\]]*)\]\(image://ocr/crop/(\d+)(?:/(\d+))?\))"));
    return re;
}

// Renders the body of a page: text is HTML-escaped except Markdown image
// references, which become real <img> tags so viewers/PDF show the picture.
static QString htmlFromMarkdown(const QString& markdown)
{
    static const QRegularExpression imageRe(
        QStringLiteral(R"(!\[([^\]]*)\]\(([^)\s]+)\))"));
    QString out;
    int last = 0;
    QRegularExpressionMatchIterator it = imageRe.globalMatch(markdown);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += markdown.mid(last, m.capturedStart() - last).toHtmlEscaped();
        const QString alt = m.captured(1).toHtmlEscaped();
        const QString src = m.captured(2).toHtmlEscaped();
        out += QStringLiteral("<img src=\"%1\" alt=\"%2\" style=\"max-width:100%\">")
                   .arg(src, alt);
        last = m.capturedEnd();
    }
    out += markdown.mid(last).toHtmlEscaped();
    return out;
}

QString Exporter::buildHtml(const QList<Page>& pages)
{
    QString body;
    for (const Page& page : pages) {
        body += QStringLiteral("<section>\n<h2>Page %1</h2>\n<pre>").arg(page.number);
        body += htmlFromMarkdown(page.text);
        body += QStringLiteral("</pre>\n</section>\n");
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

Exporter::Result Exporter::exportToFile(const QList<Page>& pages, const QString& filePath,
                                        const CropProvider& crop) const
{
    if (pages.isEmpty())
        return Result::fail(QCoreApplication::translate("Exporter", "Nothing to export."));

    if (filePath.isEmpty())
        return Result::fail(QCoreApplication::translate("Exporter", "No output path."));

    const QFileInfo info(filePath);
    const Format format = formatForSuffix(info.suffix());
    const QString mediaDir = info.absolutePath() + QLatin1Char('/')
                           + info.completeBaseName() + QStringLiteral("_media");
    const QString mediaPrefix = info.completeBaseName() + QStringLiteral("_media/");

    switch (format) {
    case Format::Markdown: {
        const QString md = crop
            ? buildMarkdownResolved(pages, crop, mediaDir, mediaPrefix)
            : buildMarkdown(pages);
        return writeTextFile(filePath, md);
    }
    case Format::PlainText:
        return writeTextFile(filePath, buildPlainText(pages));

    case Format::Html: {
        QList<Page> rendered = pages;
        if (crop) {
            for (int i = 0; i < pages.size(); ++i) {
                const ResolvedImages r = resolveImageReferences(
                    pages.at(i).text, i,
                    [&](int boxIndex) { return crop(pages.at(i).number, boxIndex); },
                    mediaDir, mediaPrefix);
                rendered[i].text = r.processedMarkdown;
            }
        }
        return writeTextFile(filePath, buildHtml(rendered));
    }

    case Format::Docx: {
        if (!isPandocAvailable())
            return Result::fail(QCoreApplication::translate("Exporter",
                "DOCX export requires Pandoc, which was not found on PATH. "
                "Install it from pandoc.org, or export to Markdown/HTML instead."));
        return exportViaPandoc(pages, filePath, crop, {});
    }

    case Format::Pdf: {
        if (isPandocAvailable()) {
            const Result r = exportViaPandoc(pages, filePath, crop, {});
            if (r.success)
                return r;
            const Result fb = writePdfFallback(pages, filePath, crop);
            if (fb.success)
                return Result::ok(QCoreApplication::translate("Exporter",
                    "Exported PDF using the built-in writer "
                    "(Pandoc failed: %1).").arg(r.message));
            return fb;
        }
        return writePdfFallback(pages, filePath, crop);
    }

    case Format::Unknown:
    default: {
        const QString md = crop
            ? buildMarkdownResolved(pages, crop, mediaDir, mediaPrefix)
            : buildMarkdown(pages);
        return writeTextFile(filePath, md);
    }
    }
}

QString Exporter::buildMarkdownResolved(const QList<Page>& pages,
                                        const CropProvider& crop,
                                        const QString& mediaDir,
                                        const QString& referencePrefix) const
{
    QString out;
    bool first = true;
    for (int i = 0; i < pages.size(); ++i) {
        const Page& page = pages.at(i);
        if (!first)
            out += QStringLiteral("\n\n");
        first = false;
        out += QStringLiteral("## Page %1\n\n").arg(page.number);

        QString text = page.text.trimmed();
        const ResolvedImages r = resolveImageReferences(
            text, i,
            [&page, &crop](int boxIndex) { return crop(page.number, boxIndex); },
            mediaDir, referencePrefix);
        text = r.processedMarkdown.trimmed();
        out += text;
        out += QChar('\n');
    }
    return out;
}

Exporter::Result Exporter::exportViaPandoc(const QList<Page>& pages,
                                           const QString& filePath,
                                           const CropProvider& crop,
                                           const QStringList& extraArgs) const
{
    QString markdown;
    QStringList extra = extraArgs;

    if (crop) {
        QTemporaryDir tmp;
        if (!tmp.isValid())
            return Result::fail(QCoreApplication::translate("Exporter",
                "Cannot create a temporary directory for images."));
        markdown = buildMarkdownResolved(pages, crop, tmp.path(), QString());
        extra << QStringLiteral("--resource-path=%1").arg(tmp.path());
    } else {
        markdown = buildMarkdown(pages);
    }

    return runPandoc(markdown, filePath, extra);
}

Exporter::ResolvedImages Exporter::resolveImageReferences(
    const QString& markdown, int pageIndex,
    const std::function<QImage(int boxIndex)>& crop,
    const QString& mediaDir, const QString& referencePrefix)
{
    ResolvedImages result;
    const QRegularExpression re = imageRefRegex();

    QString processed;
    int last = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(markdown);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString alt = m.captured(1);
        bool okIndex = false;
        const int boxIndex = m.captured(2).toInt(&okIndex);

        QImage image;
        if (okIndex && crop)
            image = crop(boxIndex);

        QString fileName;
        if (!image.isNull()) {
            fileName = QStringLiteral("page_%1_img_%2.png").arg(pageIndex).arg(boxIndex);
            const QString fullPath = QDir(mediaDir).filePath(fileName);
            if (!QDir().mkpath(mediaDir)
                || !image.save(fullPath, "PNG")) {
                fileName.clear();
            }
        }

        if (fileName.isEmpty()) {
            // Keep the original reference (e.g. stale box index).
            processed += markdown.mid(last, m.capturedEnd() - last);
        } else {
            processed += markdown.mid(last, m.capturedStart() - last);
            processed += QStringLiteral("![%1](%2%3)").arg(alt, referencePrefix, fileName);
        }
        last = m.capturedEnd();
    }
    processed += markdown.mid(last);

    result.processedMarkdown = processed;
    return result;
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

Exporter::Result Exporter::writePdfFallback(const QList<Page>& pages, const QString& path,
                                            const CropProvider& crop)
{
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(300);

    QTextDocument doc;
    doc.setDefaultStyleSheet(QStringLiteral(
        "h2{font-size:14pt;margin-top:16pt;} pre{white-space:pre-wrap;}"
        "img{max-width:100%;}"));

    // Replace every image ref with a unique local name and embed the cropped
    // pixels directly as a document resource (works for any custom scheme).
    QList<Page> rendered = pages;
    if (crop) {
        const QRegularExpression re = imageRefRegex();
        for (int i = 0; i < pages.size(); ++i) {
            const QString original = pages.at(i).text;
            QString out;
            int last = 0;
            QRegularExpressionMatchIterator it = re.globalMatch(original);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                out += original.mid(last, m.capturedStart() - last);
                const QString alt = m.captured(1);
                const int boxIndex = m.captured(2).toInt();
                const QImage img = crop(pages.at(i).number, boxIndex);
                if (!img.isNull()) {
                    const QString key =
                        QStringLiteral("media://page%1img%2").arg(i).arg(boxIndex);
                    doc.addResource(QTextDocument::ImageResource, QUrl(key), img);
                    out += QStringLiteral("![%1](%2)").arg(alt, key);
                } else {
                    out += m.captured(0);
                }
                last = m.capturedEnd();
            }
            out += original.mid(last);
            rendered[i].text = out;
        }
    }

    doc.setHtml(buildHtml(rendered));
    doc.print(&writer);

    QFileInfo info(path);
    if (!info.exists() || info.size() == 0)
        return Result::fail(QCoreApplication::translate("Exporter", "Failed to write PDF: %1").arg(path));

    return Result::ok(QCoreApplication::translate("Exporter", "Exported to %1").arg(info.fileName()));
}

} // namespace llocr
