#include "app/Exporter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>

#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QTextDocument>

namespace llocr {


namespace {

QString locatePandoc()
{
    QString exe = QStandardPaths::findExecutable(QStringLiteral("pandoc"));
    if (!exe.isEmpty())
        return exe;

    QStringList candidates;
#ifdef Q_OS_WIN
    const QString relative = QStringLiteral("Pandoc/pandoc.exe");
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty())
        candidates << QDir(localAppData).filePath(relative);
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    if (!programFiles.isEmpty())
        candidates << QDir(programFiles).filePath(relative);
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    if (!programFilesX86.isEmpty())
        candidates << QDir(programFilesX86).filePath(relative);
#elif defined(Q_OS_MAC)
    candidates << QStringLiteral("/usr/local/bin/pandoc")
               << QStringLiteral("/opt/homebrew/bin/pandoc")
               << QStringLiteral("/opt/local/bin/pandoc");
#else
    candidates << QStringLiteral("/usr/local/bin/pandoc");
#endif

    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }
    return {};
}

}  // namespace

namespace {

QString markdownPageRule()
{
    return QStringLiteral("\n\n---\n\n");
}

QString openXmlPageBreak()
{
    return QStringLiteral("\n\n```{=openxml}\n"
                          "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>\n"
                          "```\n\n");
}

QString plainTextPageRule()
{
    return QStringLiteral("\n----------------------------------------\n\n");
}

}  // namespace

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
    static const QString exe = locatePandoc();
    return exe;
}

bool Exporter::isPandocAvailable()
{
    return !pandocExecutable().isEmpty();
}

QString Exporter::joinPages(const QList<Page>& pages, const QString& pageBreak)
{
    QString out;
    bool first = true;
    for (const Page& page : pages) {
        if (!first)
            out += pageBreak.isEmpty() ? QStringLiteral("\n\n") : pageBreak;
        first = false;
        out += page.text.trimmed();
        out += QChar('\n');
    }
    return out;
}

QString Exporter::buildMarkdown(const QList<Page>& pages, bool splitPages)
{
    return joinPages(pages, splitPages ? markdownPageRule() : QString());
}

QString Exporter::buildPlainText(const QList<Page>& pages, bool splitPages)
{
    const QRegularExpression re = imageRefRegex();
    QString out;
    bool first = true;
    for (const Page& page : pages) {
        if (!first)
            out += splitPages ? plainTextPageRule() : QStringLiteral("\n");
        first = false;
        QString text = page.text.trimmed();
        text.remove(re);
        out += text;
        out += QStringLiteral("\n\n");
    }
    return out;
}

QRegularExpression Exporter::imageRefRegex()
{
    // image://ocr/crop/<boxIndex>
    static const QRegularExpression re(
        QStringLiteral(R"(!\[([^\]]*)\]\(image://ocr/crop/(\d+)(?:/(\d+))?\))"));
    return re;
}

QList<QPair<int, int>> Exporter::referencedCrops(const QList<Page>& pages)
{
    const QRegularExpression re = imageRefRegex();
    QList<QPair<int, int>> refs;
    QSet<QPair<int, int>> seen;
    for (const Page& page : pages) {
        QRegularExpressionMatchIterator it = re.globalMatch(page.text);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            bool ok = false;
            const int box = m.captured(2).toInt(&ok);
            if (!ok)
                continue;
            const QPair<int, int> key{page.number, box};
            if (!seen.contains(key)) {
                seen.insert(key);
                refs.append(key);
            }
        }
    }
    return refs;
}

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

QString Exporter::buildHtml(const QList<Page>& pages, bool splitPages)
{
    QString body;
    bool first = true;
    for (const Page& page : pages) {
        if (!first && splitPages)
            body += QStringLiteral("<hr>\n");
        first = false;
        body += QStringLiteral("<section>\n<pre>");
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

QString Exporter::embedImagesAsDataUrls(
    const QString& markdown, const std::function<QImage(int boxIndex)>& crop)
{
    const QRegularExpression re = imageRefRegex();
    QString out;
    QStringView view(markdown);
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(markdown);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        bool okIndex = false;
        const int boxIndex = m.captured(2).toInt(&okIndex);
        QImage image;
        if (okIndex && crop)
            image = crop(boxIndex);
        if (image.isNull())
            continue;  // keep the reference as-is (stale crop)

        QByteArray png;
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
            continue;

        out += view.sliced(last, m.capturedStart() - last);
        out += QStringLiteral("![%1](data:image/png;base64,%2)")
                   .arg(m.captured(1), QString::fromLatin1(png.toBase64()));
        last = m.capturedEnd();
    }
    out += view.sliced(last);
    return out;
}

QString Exporter::katexCssForExport()
{
    QFile cssFile(QStringLiteral(":/preview/katex.min.css"));
    if (!cssFile.open(QIODevice::ReadOnly))
        return {};
    QString css = QString::fromUtf8(cssFile.readAll());

    static const QRegularExpression legacySources(QStringLiteral(
        R"(,\s*url\(fonts/[^)]+?\.woff\)\s*format\(["']woff["']\)|)"
        R"(,\s*url\(fonts/[^)]+?\.ttf\)\s*format\(["']truetype["']\))"));
    css.remove(legacySources);

    static const QRegularExpression woff2Ref(
        QStringLiteral(R"(url\((fonts/[^)]+?\.woff2)\))"));
    QString out;
    QStringView view(css);
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = woff2Ref.globalMatch(css);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        QFile font(QStringLiteral(":/preview/") + m.captured(1));
        if (!font.open(QIODevice::ReadOnly))
            continue;  // leave the original reference untouched
        out += view.sliced(last, m.capturedStart() - last);
        out += QStringLiteral("url(data:font/woff2;base64,%1)")
                   .arg(QString::fromLatin1(font.readAll().toBase64()));
        last = m.capturedEnd();
    }
    out += view.sliced(last);
    return out;
}

QString Exporter::exportStyleSheet(bool splitPages)
{
    QString css = QStringLiteral(
        "body{margin:0;padding:24px 28px;background:#fff;color:#1a1a1a;"
        "font-family:-apple-system,\"Segoe UI\",Roboto,sans-serif;"
        "font-size:11pt;line-height:1.5;word-wrap:break-word;}"
        "h2{border-bottom:1px solid #ccc;padding-bottom:.2em;}"
        "table{border-collapse:collapse;margin:12px 0;}"
        "th,td{border:1px solid #999;padding:5px 9px;text-align:left;}"
        "th{background:rgba(0,0,0,0.06);}"
        "pre{white-space:pre-wrap;background:rgba(128,128,128,0.12);"
        "padding:9px 11px;border-radius:4px;}"
        "code{font-family:\"SF Mono\",Consolas,monospace;font-size:0.9em;}"
        "blockquote{margin:12px 0;padding-left:12px;"
        "border-left:3px solid #ccc;color:#666;}"
        "img{max-width:100%;height:auto;}"
        ".katex-display{overflow-x:auto;overflow-y:hidden;padding:4px 0;}"
        ".page-separator{border:none;border-top:1px solid #ccc;margin:28px 0;}"
        "@media print{"
        "body{padding:0;}"
        ".page-separator{display:none;}");
    if (splitPages)
        css += QStringLiteral(".export-page{break-before:page;}"
                              ".export-page:first-child{break-before:auto;}");
    css += QStringLiteral("h2{break-after:avoid;}"
                          "pre,table,blockquote,.katex-display{break-inside:avoid;}"
                          "}");
    return css;
}

QString Exporter::assembleHtmlDocument(const QStringList& pageSections)
{
    QString html;
    html += QStringLiteral("<!DOCTYPE html>\n<html>\n<head>\n"
                           "<meta charset=\"utf-8\">\n<title>OCR result</title>\n");
    html += QLatin1String("<style>") + katexCssForExport() + QLatin1String("</style>\n");
    html += QLatin1String("<style>") + exportStyleSheet() + QLatin1String("</style>\n");
    html += QLatin1String("</head>\n<body>\n");
    html += pageSections.join(QLatin1Char('\n'));
    html += QLatin1String("\n</body>\n</html>\n");
    return html;
}

Exporter::Result Exporter::exportToFile(const QList<Page>& pages, const QString& filePath,
                                        const CropProvider& crop,
                                        const ExportOptions& options) const
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
    const bool splitPages = options.splitPages;

    switch (format) {
    case Format::Markdown: {
        const QString md = crop
            ? buildMarkdownResolved(pages, crop, mediaDir, mediaPrefix,
                                    splitPages ? markdownPageRule() : QString())
            : buildMarkdown(pages, splitPages);
        return writeTextFile(filePath, md);
    }
    case Format::PlainText:
        return writeTextFile(filePath, buildPlainText(pages, splitPages));

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
        return writeTextFile(filePath, buildHtml(rendered, splitPages));
    }

    case Format::Docx: {
        if (!isPandocAvailable())
            return Result::fail(QCoreApplication::translate("Exporter",
                "DOCX export requires Pandoc, which was not found on PATH. "
                "Install it from pandoc.org, or export to Markdown/HTML instead."));
        return exportViaPandoc(pages, filePath, crop, {}, splitPages);
    }

    case Format::Pdf: {
        if (isPandocAvailable()) {
            const Result r = exportViaPandoc(pages, filePath, crop, {}, splitPages);
            if (r.success)
                return r;
            const Result fb = writePdfFallback(pages, filePath, crop,
                                               defaultPdfLayout(), splitPages);
            if (fb.success)
                return Result::ok(QCoreApplication::translate("Exporter",
                    "Exported PDF using the built-in writer (%1).").arg(r.message));
            return fb;
        }
        return writePdfFallback(pages, filePath, crop, defaultPdfLayout(), splitPages);
    }

    case Format::Unknown:
    default: {
        const QString md = crop
            ? buildMarkdownResolved(pages, crop, mediaDir, mediaPrefix,
                                    splitPages ? markdownPageRule() : QString())
            : buildMarkdown(pages, splitPages);
        return writeTextFile(filePath, md);
    }
    }
}

QString Exporter::buildMarkdownResolved(const QList<Page>& pages,
                                        const CropProvider& crop,
                                        const QString& mediaDir,
                                        const QString& referencePrefix,
                                        const QString& pageBreak) const
{
    QString out;
    bool first = true;
    for (int i = 0; i < pages.size(); ++i) {
        const Page& page = pages.at(i);
        if (!first)
            out += pageBreak.isEmpty() ? QStringLiteral("\n\n") : pageBreak;
        first = false;

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
                                           const QStringList& extraArgs,
                                           bool splitPages) const
{
    QString markdown;
    QStringList extra = extraArgs;

    const QString pageBreak = splitPages ? openXmlPageBreak() : QString();

    QTemporaryDir tmp;
    if (crop) {
        if (!tmp.isValid())
            return Result::fail(QCoreApplication::translate("Exporter",
                "Cannot create a temporary directory for images."));
        markdown = buildMarkdownResolved(pages, crop, tmp.path(), QString(),
                                         pageBreak);
        extra << QStringLiteral("--resource-path=%1").arg(tmp.path());
    } else {
        markdown = joinPages(pages, pageBreak);
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
    QStringView markdownView(markdown);
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
            processed += markdownView.sliced(last, m.capturedEnd() - last);
        } else {
            processed += markdownView.sliced(last, m.capturedStart() - last);
            processed += QStringLiteral("![%1](%2%3)").arg(alt, referencePrefix, fileName);
        }
        last = m.capturedEnd();
    }
    processed += markdownView.sliced(last);

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

QPageLayout Exporter::defaultPdfLayout()
{
    return QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait,
                       QMarginsF(15, 15, 15, 15), QPageLayout::Millimeter);
}

Exporter::Result Exporter::writePdfFallback(const QList<Page>& pages, const QString& path,
                                            const CropProvider& crop,
                                            const QPageLayout& layout, bool splitPages)
{
    QPdfWriter writer(path);
    writer.setPageLayout(layout);
    writer.setResolution(300);

    QTextDocument doc;
    doc.setDefaultStyleSheet(QStringLiteral(
        "h2{font-size:14pt;margin-top:16pt;} pre{white-space:pre-wrap;}"
        "img{max-width:100%;}"));

    QList<Page> rendered = pages;
    if (crop) {
        const QRegularExpression re = imageRefRegex();
        for (int i = 0; i < pages.size(); ++i) {
            const QStringView original = pages.at(i).text;
            QString out;
            int last = 0;
            QRegularExpressionMatchIterator it = re.globalMatchView(original);
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                out += original.sliced(last, m.capturedStart() - last);
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
            out += original.sliced(last);
            rendered[i].text = out;
        }
    }

    doc.setHtml(buildHtml(rendered, splitPages));
    doc.print(&writer);

    QFileInfo info(path);
    if (!info.exists() || info.size() == 0)
        return Result::fail(QCoreApplication::translate("Exporter", "Failed to write PDF: %1").arg(path));

    return Result::ok(QCoreApplication::translate("Exporter", "Exported to %1").arg(info.fileName()));
}

} // namespace llocr
