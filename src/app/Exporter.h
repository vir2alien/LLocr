#pragma once

#include <functional>

#include <QImage>
#include <QString>
#include <QStringList>

#include "core/OcrResult.h"

class QRegularExpression;

namespace llocr {

/**
 * @brief Turns recognized pages into an exported file.
 *
 * Formats are derived from that Markdown:
 *   - md   : the Markdown itself;
 *   - txt  : Markdown with the lightest markers stripped;
 *   - html : rendered directly;
 *   - docx : via Pandoc (Markdown on stdin -> .docx);
 *   - pdf  : via Pandoc, falling back to a built-in Qt writer when Pandoc
 *            (or a LaTeX engine) is unavailable.
 *
 * Markdown image blocks print as `![alt](image://ocr/crop/<boxIndex>)`. Those
 * references have no meaning outside the app, so every export path resolves
 * them to real image files (or embedded document resources) before writing.
 */
class Exporter {
public:
    enum class Format { Markdown, PlainText, Html, Docx, Pdf, Unknown };

    /// Crops the image-block at `boxIndex` of the given page. `pageNumber` is
    /// the 1-based page number carried by Exporter::Page.
    using CropProvider = std::function<QImage(int pageNumber, int boxIndex)>;

    /// A page's markdown with `image://ocr/crop` refs replaced by local files.
    struct ResolvedImages {
        QString processedMarkdown;   ///< markdown referencing local files.
        QStringList savedFiles;      ///< file names written under mediaDir.
    };

    struct Result {
        bool success = false;
        QString message;   ///< Human-readable status or error.
        static Result ok(const QString& msg = {})  { return { true,  msg }; }
        static Result fail(const QString& msg)      { return { false, msg }; }
    };

    struct Page {
        int number = 0;
        QString text;
    };

    static Format formatForSuffix(const QString& suffix);

    static QStringList nativeSuffixes();

    static QStringList pandocSuffixes();

    static bool isPandocAvailable();

    static QString pandocExecutable();

    static QString buildMarkdown(const QList<Page>& pages);
    static QString buildPlainText(const QList<Page>& pages);
    static QString buildHtml(const QList<Page>& pages);

    Result exportToFile(const QList<Page>& pages, const QString& filePath,
                        const CropProvider& crop = {}) const;

    /// Replaces every `![alt](image://ocr/crop/<box>)` in one page's markdown
    /// with a local file saved under mediaDir (page_<pageIndex>_img_<box>.png).
    /// The replacement reference is `referencePrefix + fileName` (empty prefix
    /// means the bare file name). `crop` extracts the source pixels for a box.
    /// Failed crops leave the original reference untouched.
    static ResolvedImages resolveImageReferences(
        const QString& markdown, int pageIndex,
        const std::function<QImage(int boxIndex)>& crop,
        const QString& mediaDir,
        const QString& referencePrefix = {});

private:
    static QRegularExpression imageRefRegex();

    QString buildMarkdownResolved(const QList<Page>& pages, const CropProvider& crop,
                                  const QString& mediaDir, const QString& referencePrefix,
                                  QStringList* savedFiles) const;
    Result exportViaPandoc(const QList<Page>& pages, const QString& filePath,
                           const CropProvider& crop, const QStringList& extraArgs) const;

    static Result writeTextFile(const QString& path, const QString& content);

    static Result runPandoc(const QString& markdown,
                            const QString& outputPath,
                            const QStringList& extraArgs);

    // pdf when Pandoc is not available
    static Result writePdfFallback(const QList<Page>& pages, const QString& path,
                                   const CropProvider& crop);
};

} // namespace llocr
