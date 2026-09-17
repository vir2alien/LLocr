#pragma once

#include <functional>

#include <QImage>
#include <QList>
#include <QPageLayout>
#include <QPair>
#include <QString>
#include <QStringList>

class QRegularExpression;

namespace llocr {

class Exporter {
public:
    enum class Format { Markdown, PlainText, Html, Docx, Pdf, Unknown };

    using CropProvider = std::function<QImage(int pageNumber, int boxIndex)>;

    struct ResolvedImages {
        QString processedMarkdown;
    };

    struct Result {
        bool success = false;
        QString message;
        static Result ok(const QString& msg = {})  { return { true,  msg }; }
        static Result fail(const QString& msg)      { return { false, msg }; }
    };

    struct Page {
        int number = 0;
        QString text;
    };

    struct ExportOptions {
        bool splitPages;
        explicit ExportOptions(bool split = true) : splitPages(split) {}
    };

    static Format formatForSuffix(const QString& suffix);
    static bool isPandocAvailable();
    static QString pandocExecutable();
    static QString buildMarkdown(const QList<Page>& pages, bool splitPages = true);
    static QString buildPlainText(const QList<Page>& pages, bool splitPages = true);
    static QString buildHtml(const QList<Page>& pages, bool splitPages = true);
    static QString embedImagesAsDataUrls(
        const QString& markdown, const std::function<QImage(int boxIndex)>& crop);
    static QString katexCssForExport();
    static QString exportStyleSheet(bool splitPages = true);
    static QString assembleHtmlDocument(const QStringList& pageSections);
    static Result writeTextFile(const QString& path, const QString& content);
    static QPageLayout defaultPdfLayout();
    static Result writePdfFallback(const QList<Page> &pages, const QString &path,
                                   const CropProvider &crop,
                                   const QPageLayout &layout = defaultPdfLayout(),
                                   bool splitPages = true);

    static QList<QPair<int, int>> referencedCrops(const QList<Page> &pages);
    Result exportToFile(const QList<Page>& pages, const QString& filePath,
                        const CropProvider& crop = {},
                        const ExportOptions& options = ExportOptions()) const;
    static ResolvedImages resolveImageReferences(
        const QString& markdown, int pageIndex,
        const std::function<QImage(int boxIndex)>& crop,
        const QString& mediaDir,
        const QString& referencePrefix = {});

private:
    static QRegularExpression imageRefRegex();

    QString buildMarkdownResolved(const QList<Page>& pages, const CropProvider& crop,
                                  const QString& mediaDir, const QString& referencePrefix,
                                  bool splitPages) const;
    Result exportViaPandoc(const QList<Page>& pages, const QString& filePath,
                           const CropProvider& crop, const QStringList& extraArgs) const;

    static Result runPandoc(const QString& markdown,
                            const QString& outputPath,
                            const QStringList& extraArgs);
};

} // namespace llocr
