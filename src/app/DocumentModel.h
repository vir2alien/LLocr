#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <memory>
#include "core/OcrResult.h"

class QPdfDocument;

namespace llocr {

#ifdef LLOCR_HAVE_DJVU
class DjVuDocument;
#endif

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
};

class DocumentModel
{
public:
    struct PreparedDjVu {
        QList<DocumentPage> pages;
#ifdef LLOCR_HAVE_DJVU
        std::shared_ptr<DjVuDocument> document;
#endif
        QString error;
        QStringList warnings;
    };

    // Preparation owns an independent decoder and may run without a live model.
    static PreparedDjVu prepareDjVu(const QString& path);
    // Call on the owner thread under the same lock as other model mutations.
    // Shared decoder access must remain serialized after committing.
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
    QPdfDocument* pdfFor(const QString& path);

private:
    QList<DocumentPage> m_pages;
    QHash<QString, QPdfDocument*> m_pdfs;
#ifdef LLOCR_HAVE_DJVU
    QHash<QString, std::shared_ptr<DjVuDocument>> m_djvus;
#endif
    QList<int> m_fullCache;
};

} // namespace llocr
