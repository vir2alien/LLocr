#include "app/DocumentModel.h"
#include "app/DjVuDocument.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <exception>
#include <memory>
#include <new>
#include <utility>

#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>

namespace llocr {

namespace {

constexpr double kDpi = 150.0;
constexpr int kFullCacheLimit = 4;
constexpr int kThumbMaxWidth = 220;
constexpr int kThumbMaxHeight = 300;

QSize fitWithin(const QSize& size, const QSize& bounds)
{
    if (size.isEmpty() || bounds.isEmpty())
        return QSize(200, 260);
    QSize scaled = size.scaled(bounds, Qt::KeepAspectRatio);
    if (scaled.isEmpty())
        return QSize(200, 260);
    return scaled;
}

QSize pdfPixelSize(const QSizeF& pointSize)
{
    // 1/72 inch units -> pixels at ~150 DPI for a good OCR/quality trade-off.
    return QSize(qRound(pointSize.width() / 72.0 * kDpi),
                 qRound(pointSize.height() / 72.0 * kDpi));
}

}  // namespace

DocumentModel::~DocumentModel()
{
    clear();
}

bool DocumentModel::loadImage(const QString& path)
{
    clear();
    return appendImage(path);
}

bool DocumentModel::appendImage(const QString& path)
{
    DocumentPage page;
    page.sourcePath = path;
    page.sourceType = DocumentSource::Image;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (full.isEmpty())
        return false;
    page.pixelSize = full;

    QImageReader thumbReader(path);
    thumbReader.setAutoTransform(true);
    thumbReader.setScaledSize(fitWithin(full, QSize(kThumbMaxWidth, kThumbMaxHeight)));
    page.thumb = thumbReader.read();

    m_pages.append(page);
    return true;
}

bool DocumentModel::appendPdf(const QString& path)
{
    QPdfDocument* pdf = pdfFor(path);
    if (!pdf || pdf->pageCount() <= 0)
        return false;

    const int count = pdf->pageCount();
    for (int i = 0; i < count; ++i) {
        const QSize pixelSize = pdfPixelSize(pdf->pagePointSize(i));

        DocumentPage page;
        page.sourcePath = path;
        page.sourceType = DocumentSource::Pdf;
        page.sourcePageIndex = i;
        page.pixelSize = pixelSize;

        QPdfDocumentRenderOptions options;
        QImage image = pdf->render(i, fitWithin(pixelSize, QSize(kThumbMaxWidth, kThumbMaxHeight)),
                                   options);
        if (image.isNull()) {
            image = QImage(fitWithin(pixelSize, QSize(kThumbMaxWidth, kThumbMaxHeight)),
                           QImage::Format_ARGB32);
            image.fill(Qt::white);
        }
        page.thumb = image;

        m_pages.append(page);
    }
    return true;
}

bool DocumentModel::appendFile(const QString& path, QString* error)
{
    if (error)
        error->clear();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("djvu") || suffix == QStringLiteral("djv"))
        return appendDjVu(path, error);
    const bool ok = suffix == QStringLiteral("pdf") ? appendPdf(path) : appendImage(path);
    if (!ok && error)
        *error = QCoreApplication::translate("DocumentModel", "Failed to open %1.").arg(path);
    return ok;
}

DocumentModel::PreparedDjVu DocumentModel::prepareDjVu(const QString& path)
{
    PreparedDjVu prepared;
    try {
        const QString key = QFileInfo(path).absoluteFilePath();
        auto document = std::make_shared<DjVuDocument>();
        QList<DocumentPage> pages;
        if (document->open(key, &prepared.error)) {
            const int count = document->pageCount();
            pages.reserve(count);
            for (int i = 0; i < count; ++i) {
                DocumentPage page;
                page.sourcePath = key;
                page.sourceType = DocumentSource::DjVu;
                page.sourcePageIndex = i;
                QString pageError;
                page.pixelSize = document->pageSize(i, &pageError);
                if (!page.pixelSize.isEmpty())
                    page.thumb = document->render(i, fitWithin(page.pixelSize,
                                                 QSize(kThumbMaxWidth, kThumbMaxHeight)),
                                                 &pageError);
                if (page.thumb.isNull()) {
                    if (pageError.isEmpty())
                        pageError = QCoreApplication::translate("DocumentModel",
                            "Failed to read DjVu %1, page %2.").arg(path).arg(i + 1);
                    page.sourceError = pageError;
                    prepared.warnings.append(pageError);
                    if (page.pixelSize.isEmpty())
                        page.pixelSize = QSize(800, 1000);
                    page.thumb = QImage(fitWithin(page.pixelSize,
                                        QSize(kThumbMaxWidth, kThumbMaxHeight)), QImage::Format_RGB32);
                    if (page.thumb.isNull())
                        throw std::bad_alloc();
                    page.thumb.fill(Qt::white);
                }
                pages.append(page);
            }
            if (count > 0 && pages.size() == count && prepared.error.isEmpty()) {
                prepared.pages = std::move(pages);
                prepared.document = std::move(document);
                return prepared;
            }
        }
        if (prepared.error.isEmpty())
            prepared.error = QCoreApplication::translate("DocumentModel", "Failed to open DjVu %1.")
                                 .arg(path);
    } catch (const std::bad_alloc&) {
        // A literal avoids allocating again while reporting an allocation failure.
        prepared.error = QStringLiteral("Not enough memory to prepare DjVu document.");
    } catch (const std::exception& exception) {
        prepared.error = QCoreApplication::translate("DocumentModel", "Failed to open DjVu %1: %2")
                             .arg(path, QString::fromUtf8(exception.what()));
    } catch (...) {
        prepared.error = QCoreApplication::translate("DocumentModel", "Failed to open DjVu %1.")
                             .arg(path);
    }
    // Only the success path publishes pages or a decoder, even after a late failure.
    return prepared;
}

void DocumentModel::appendPreparedDjVu(const PreparedDjVu& prepared)
{
    if (!prepared.error.isEmpty() || prepared.pages.isEmpty() || !prepared.document)
        return;
    const QString& key = prepared.pages.first().sourcePath;
    m_pages.reserve(m_pages.size() + prepared.pages.size());
    // Existing pages must keep their decoder, even if the same source is imported again.
    if (!m_djvus.contains(key))
        m_djvus.insert(key, prepared.document);
    m_pages.append(prepared.pages);
}

bool DocumentModel::appendDjVu(const QString& path, QString* error)
{
    const PreparedDjVu prepared = prepareDjVu(path);
    if (error)
        *error = prepared.error;
    if (!prepared.error.isEmpty())
        return false;
    appendPreparedDjVu(prepared);
    return true;
}

bool DocumentModel::removePage(int index)
{
    if (!isValidIndex(index))
        return false;
    m_pages.removeAt(index);
    m_fullCache.clear();
    return true;
}

bool DocumentModel::movePage(int from, int to)
{
    if (!isValidIndex(from) || !isValidIndex(to) || from == to)
        return false;
    m_pages.move(from, to);
    m_fullCache.clear();
    return true;
}

void DocumentModel::clear()
{
    m_pages.clear();
    m_fullCache.clear();
    qDeleteAll(m_pdfs);
    m_pdfs.clear();
    m_djvus.clear();
}

bool DocumentModel::isValidIndex(int index) const
{
    return index >= 0 && index < m_pages.size();
}

bool DocumentModel::decodeSource(DocumentPage& page, QString *error)
{
    QImageReader reader(page.sourcePath);
    reader.setAutoTransform(true);
    reader.setAllocationLimit(0);
    QImage image = reader.read();
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("Failed to read %1: %2")
                         .arg(page.sourcePath,
                              reader.errorString().isEmpty()
                                  ? QStringLiteral("unknown error")
                                  : reader.errorString());
        return false;
    }

    if (image.format() != QImage::Format_RGB32
        && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }

    page.image = image;
    return true;
}

QImage DocumentModel::renderFull(const DocumentPage& page, QString *error)
{
    if (!page.sourceError.isEmpty()) {
        QImage placeholder(page.pixelSize, QImage::Format_RGB32);
        if (placeholder.isNull()) {
            if (error)
                *error = page.sourceError;
            return {};
        }
        placeholder.fill(Qt::white);
        return placeholder;
    }
    if (page.sourceType == DocumentSource::DjVu) {
        const auto document = m_djvus.value(page.sourcePath);
        if (!document) {
            if (error)
                *error = QCoreApplication::translate("DocumentModel", "DjVu document is not open: %1")
                             .arg(page.sourcePath);
            return {};
        }
        return document->render(page.sourcePageIndex, page.pixelSize, error);
    }
    if (page.sourceType == DocumentSource::Pdf) {
        QPdfDocument* pdf = pdfFor(page.sourcePath);
        if (!pdf) {
            if (error)
                *error = QStringLiteral("Failed to open %1 as a PDF document")
                             .arg(page.sourcePath);
            return QImage();
        }
        QPdfDocumentRenderOptions options;
        QImage image = pdf->render(page.sourcePageIndex, page.pixelSize, options);
        if (image.isNull()) {
            image = QImage(page.pixelSize.isEmpty() ? QSize(800, 1000) : page.pixelSize,
                           QImage::Format_ARGB32);
            image.fill(Qt::white);
        }
        return image;
    }

    DocumentPage decoded;
    decoded.sourcePath = page.sourcePath;
    if (!decodeSource(decoded, error))
        return QImage();
    return decoded.image;
}

void DocumentModel::ensureFullImage(int index, QString *error)
{
    if (!isValidIndex(index))
        return;
    if (!m_pages[index].image.isNull())
        return;
    m_pages[index].image = renderFull(m_pages[index], error);
    m_fullCache.removeAll(index);
    m_fullCache.prepend(index);
    evictFullImages();
}

void DocumentModel::evictFullImages()
{
    while (m_fullCache.size() > kFullCacheLimit) {
        const int evicted = m_fullCache.takeLast();
        if (isValidIndex(evicted))
            m_pages[evicted].image = QImage();
    }
}

QPdfDocument* DocumentModel::pdfFor(const QString& path)
{
    QPdfDocument* pdf = m_pdfs.value(path, nullptr);
    if (pdf)
        return pdf;
    pdf = new QPdfDocument();
    if (pdf->load(path) != QPdfDocument::Error::None) {
        delete pdf;
        return nullptr;
    }
    m_pdfs.insert(path, pdf);
    return pdf;
}

QImage DocumentModel::fullImage(int index, QString *error)
{
    if (error)
        error->clear();
    if (!isValidIndex(index)) {
        if (error)
            *error = QStringLiteral("Invalid page index %1").arg(index + 1);
        return QImage();
    }
    ensureFullImage(index, error);
    return m_pages[index].image;
}

const QImage& DocumentModel::thumbnail(int index) const
{
    static const QImage null;
    if (!isValidIndex(index))
        return null;
    return m_pages[index].thumb;
}

} // namespace llocr
