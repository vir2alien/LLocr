#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include "core/OcrResult.h"

class QPdfDocument;

namespace llocr {

/**
 * @brief Holds the opened document
 */

struct DocumentPage {
    QImage thumb;
    QImage image;
    OcrResult result;
    bool recognized = false;
    QString sourcePath;
    int pdfIndex = -1;
    QSize pixelSize;
};

class DocumentModel
{
public:
    DocumentModel() = default;
    ~DocumentModel();

    bool loadImage(const QString& path);
    bool loadImages(const QStringList& paths);
    bool loadPdf(const QString& path);

    bool appendImage(const QString& path);
    bool appendPdf(const QString& path);

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

    QList<DocumentPage> m_pages;
    QHash<QString, QPdfDocument*> m_pdfs;
    QList<int> m_fullCache;
};

} // namespace llocr
