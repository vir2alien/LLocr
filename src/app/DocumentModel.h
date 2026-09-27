#pragma once

#include "app/DjVuDocument.h"
#include "core/OcrResult.h"
#include <memory>
#include <QHash>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

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
    QString parseNote;  ///< Non-fatal parser diagnostic for the last recognition.
};

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

    static PreparedDjVu prepareDjVu(const QString &path);
    void appendPreparedDjVu(const PreparedDjVu &prepared);

    struct RenderRequest {
        DocumentPage page;  ///< a copy, image field unused
        std::shared_ptr<DjVuDocument> djvu;
    };

    static QImage renderDetached(const RenderRequest &request, QString *error = nullptr);
    RenderRequest renderRequestFor(int index) const;

    DocumentModel() = default;
    ~DocumentModel();
    Q_DISABLE_COPY_MOVE(DocumentModel)

    bool loadImage(const QString &path);

    bool appendImage(const QString &path);
    bool appendPdf(const QString &path);
    bool appendDjVu(const QString &path, QString *error = nullptr);
    bool appendFile(const QString &path, QString *error = nullptr);

    bool removePage(int index);
    bool movePage(int from, int to);
    void clear();

    int pageCount() const { return m_pages.size(); }
    bool isEmpty() const { return m_pages.isEmpty(); }

    DocumentPage &page(int index) { return m_pages[index]; }
    const DocumentPage &page(int index) const { return m_pages[index]; }

    bool isValidIndex(int index) const;

    QImage fullImage(int index, QString *error = nullptr);
    const QImage &thumbnail(int index) const;

private:
    bool decodeSource(DocumentPage &page, QString *error = nullptr);
    static QImage decodeSourceCopy(const DocumentPage &page, QString *error = nullptr);
    QImage renderFull(const DocumentPage &page, QString *error = nullptr);
    void ensureFullImage(int index, QString *error = nullptr);
    void evictFullImages();
    void evictUnusedSourceDocuments(const QString &path);
    QPdfDocument *pdfFor(const QString &path);

private:
    QList<DocumentPage> m_pages;
    QHash<QString, QPdfDocument *> m_pdfs;
    QHash<QString, std::shared_ptr<DjVuDocument>> m_djvus;
    QList<int> m_fullCache;
};

}  // namespace llocr
