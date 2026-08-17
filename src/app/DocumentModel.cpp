#include "app/DocumentModel.h"

#include <QFileInfo>
#include <QImageReader>
#include <QPainter>

#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>

namespace llocr {

namespace {

bool loadImageFile(const QString& path, DocumentPage &page)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
        return false;

    if (image.format() != QImage::Format_RGB32
        && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }

    page.image = image;
    return true;
}

}  // namespace

bool DocumentModel::loadImage(const QString& path)
{
    clear();

    DocumentPage page;
    if (!loadImageFile(path, page))
        return false;

    m_pages.append(page);
    return true;
}

bool DocumentModel::loadImages(const QStringList& paths)
{
    clear();

    for (const QString& path : paths) {
        DocumentPage page;
        if (loadImageFile(path, page))
            m_pages.append(page);
    }

    return !m_pages.isEmpty();
}

bool DocumentModel::loadPdf(const QString& path)
{
    QPdfDocument pdf;
    if (pdf.load(path) != QPdfDocument::Error::None)
        return false;

    const int count = pdf.pageCount();
    if (count <= 0)
        return false;

    clear();

    // Render each page at ~150 DPI for a good OCR/quality trade-off.
    // TODO DPI value into settings
    constexpr double dpi = 150.0;
    for (int i = 0; i < count; ++i) {
        const QSizeF pointSize = pdf.pagePointSize(i); // 1/72 inch units
        const QSize pixelSize(qRound(pointSize.width()  / 72.0 * dpi),
                              qRound(pointSize.height() / 72.0 * dpi));

        QPdfDocumentRenderOptions options;
        QImage image = pdf.render(i, pixelSize, options);
        if (image.isNull()) {
            image = QImage(pixelSize.isEmpty() ? QSize(800, 1000) : pixelSize,
                           QImage::Format_ARGB32);
            image.fill(Qt::white);
        }

        DocumentPage page;
        page.image = image;
        m_pages.append(page);
    }

    return !m_pages.isEmpty();
}

bool DocumentModel::removePage(int index)
{
    if (!isValidIndex(index))
        return false;
    m_pages.removeAt(index);
    return true;
}

void DocumentModel::clear()
{
    m_pages.clear();
}

bool DocumentModel::isValidIndex(int index) const
{
    return index >= 0 && index < m_pages.size();
}

} // namespace llocr
