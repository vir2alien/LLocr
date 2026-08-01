#pragma once

#include <QString>
#include <QStringList>

#include "core/OcrResult.h"

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
 */
class Exporter {
public:
    enum class Format { Markdown, PlainText, Html, Docx, Pdf, Unknown };

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

    Result exportToFile(const QList<Page>& pages, const QString& filePath) const;

private:
    static Result writeTextFile(const QString& path, const QString& content);

    static Result runPandoc(const QString& markdown,
                            const QString& outputPath,
                            const QStringList& extraArgs);

    // pdf when Pandoc is not available
    static Result writePdfFallback(const QList<Page>& pages, const QString& path);
};

} // namespace llocr
