#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <memory>
#include "app/DjVuDocument.h"
#include "core/OcrResult.h"

class QPdfDocument;

namespace llocr {

enum class DocumentSource { Image, Pdf, DjVu };

struct DocumentPage {
    QImage thumb;
    QImage image;
    OcrResult result;
    bool recognized = false;
    QString sourcePath;
    DocumentSource sourceType = DocumentSource::Image;
    int sourcePageIndex = -1;
    QSize pixelSize;
    QString sourceError;
    QString parseNote;   ///< Non-fatal parser diagnostic for the last recognition.
};

// The in-memory document: the page list, the per-page images and the OCR
// results. GUI-thread resident (ADR 104).
//
// Threading contract: the *page list and the results* may only be touched from
// the GUI thread. Two places cross that line and both are deliberate:
//   * the export pipeline resolves block crops from a worker thread, holding
//     AppController's document lock for the duration, so a render triggered by
//     the GUI thread cannot pull a half-written image cache out from under it;
//   * DjVuDocument/QPdfDocument are read (never written) while a page is
//     rendered, which currently happens on the calling thread.
// The lock is therefore only as good as its call sites — keep every access to
// m_pages/m_pdfs/m_djvus inside AppController (or another owner) rather than
// handing the model out.
class DocumentModel
{
public:
    // DjVuDocument.h is header-only w.r.t. the DjVuLibre C API (it forward
    // declares the ddjvu_* structs), so the DjVu members below are declared
    // unconditionally: LLOCR_HAVE_DJVU changes only which implementation the
    // .cpp compiles, never the class layout. A build-time #ifdef in this header
    // would give every translation unit a different DocumentModel.
    struct PreparedDjVu {
        QList<DocumentPage> pages;
        std::shared_ptr<DjVuDocument> document;
        QString error;
        QStringList warnings;
    };

    static PreparedDjVu prepareDjVu(const QString& path);
    void appendPreparedDjVu(const PreparedDjVu& prepared);

    DocumentModel() = default;
    ~DocumentModel();
    Q_DISABLE_COPY_MOVE(DocumentModel)

    bool loadImage(const QString& path);

    bool appendImage(const QString& path);
    bool appendPdf(const QString& path);
    bool appendDjVu(const QString& path, QString* error = nullptr);
    bool appendFile(const QString& path, QString* error = nullptr);

    bool removePage(int index);
    bool movePage(int from, int to);
    void clear();

    int pageCount() const { return m_pages.size(); }
    bool isEmpty() const { return m_pages.isEmpty(); }

    DocumentPage& page(int index) { return m_pages[index]; }
    const DocumentPage& page(int index) const { return m_pages[index]; }

    bool isValidIndex(int index) const;

    QImage fullImage(int index, QString *error = nullptr);
    const QImage& thumbnail(int index) const;

private:
    bool decodeSource(DocumentPage& page, QString *error = nullptr);
    QImage renderFull(const DocumentPage& page, QString *error = nullptr);
    void ensureFullImage(int index, QString *error = nullptr);
    void evictFullImages();
    void evictUnusedSourceDocuments(const QString& path);
    QPdfDocument* pdfFor(const QString& path);

private:
    QList<DocumentPage> m_pages;
    QHash<QString, QPdfDocument*> m_pdfs;
    QHash<QString, std::shared_ptr<DjVuDocument>> m_djvus;
    QList<int> m_fullCache;
};

} // namespace llocr
