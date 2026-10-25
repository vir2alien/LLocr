#include "app/DocumentModel.h"

#include "core/BoxTransitions.h"

#include <exception>
#include <memory>
#include <new>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <utility>

namespace llocr {

PageVerification PageStatus::verificationAggregate(const OcrPage &page, const std::function<bool(const QString &label)> &isTypeEnabled)
{
    bool sawSubject = false;
    bool sawError = false;
    for (const BoundingBox &box : std::as_const(page.boxes)) {
        if (!isTypeEnabled(box.label) && !box.duplicateSuspect)
            continue;
        sawSubject = true;
        const BoxCheckStatus status = BoxTransitions::effectiveCheckStatus(box);
        if (status == BoxCheckStatus::NotChecked)
            return PageVerification::Incomplete;
        sawError |= status == BoxCheckStatus::Review || status == BoxCheckStatus::Mismatch || status == BoxCheckStatus::InvalidSyntax;
    }
    return sawSubject ? (sawError ? PageVerification::HasErrors : PageVerification::Clean) : PageVerification::Incomplete;
}

PageRecognitionMode PageStatus::recognitionModeAfterBlockText(PageRecognitionMode mode, const OcrPage &page, const std::function<bool(const BoundingBox &box)> &expectsText)
{
    if (mode != PageRecognitionMode::MarkupOnly)
        return mode;
    for (const BoundingBox &box : std::as_const(page.boxes)) {
        if (expectsText(box) && box.text.isEmpty())
            return PageRecognitionMode::MarkupOnly;
    }
    return PageRecognitionMode::MarkupOcr;
}

int PageStatus::pendingFixCount(const OcrPage &page)
{
    int count = 0;
    for (const BoundingBox &box : std::as_const(page.boxes)) {
        count += BoxTransitions::needsFix(box) ? 1 : 0;
    }
    return count;
}

namespace {

constexpr double kDpi = 300.0;
constexpr int kFullCacheLimit = 4;
constexpr int kThumbMaxWidth = 220;
constexpr int kThumbMaxHeight = 300;
constexpr qint64 kMaxDecodeBytes = 256ll * 1024 * 1024;

QSize fitWithin(const QSize &size, const QSize &bounds)
{
    if (size.isEmpty() || bounds.isEmpty())
        return QSize(200, 260);
    QSize scaled = size.scaled(bounds, Qt::KeepAspectRatio);
    if (scaled.isEmpty())
        return QSize(200, 260);
    return scaled;
}

QSize pdfPixelSize(const QSizeF &pointSize)
{
    return QSize(qRound(pointSize.width() / 72.0 * kDpi), qRound(pointSize.height() / 72.0 * kDpi));
}

}  // namespace

DocumentModel::~DocumentModel()
{
    clear();
}

bool DocumentModel::appendImage(const QString &path)
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

    m_pages.append(page);
    return true;
}

bool DocumentModel::appendPdf(const QString &path)
{
    const std::shared_ptr<PdfDocument> pdf = pdfFor(path);
    if (!pdf || pdf->pageCount() <= 0)
        return false;

    const int count = pdf->pageCount();
    m_pages.reserve(m_pages.size() + count);
    for (int i = 0; i < count; ++i) {
        DocumentPage page;
        page.sourcePath = path;
        page.sourceType = DocumentSource::Pdf;
        page.sourcePageIndex = i;
        page.pixelSize = pdfPixelSize(pdf->pagePointSize(i));
        m_pages.append(page);
    }
    return true;
}

bool DocumentModel::appendFile(const QString &path, QString *error)
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

bool DocumentModel::appendDjVu(const QString &path, QString *error)
{
    const PreparedDjVu prepared = prepareDjVu(path);
    if (error)
        *error = prepared.error;
    if (!prepared.error.isEmpty())
        return false;
    appendPreparedDjVu(prepared);
    return true;
}

DocumentModel::PreparedDjVu DocumentModel::prepareDjVu(const QString &path)
{
    PreparedDjVu prepared;
#ifdef LLOCR_HAVE_DJVU
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
                if (page.pixelSize.isEmpty()) {
                    if (pageError.isEmpty())
                        pageError = QCoreApplication::translate("DocumentModel", "Failed to read DjVu %1, page %2.").arg(path).arg(i + 1);
                    page.sourceError = pageError;
                    prepared.warnings.append(pageError);
                    page.pixelSize = QSize(800, 1000);
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
            prepared.error = QCoreApplication::translate("DocumentModel", "Failed to open DjVu %1.").arg(path);
    } catch (const std::bad_alloc &) {
        prepared.error = QStringLiteral("Not enough memory to prepare DjVu document.");
    } catch (const std::exception &exception) {
        prepared.error = QCoreApplication::translate("DocumentModel", "Failed to open DjVu %1: %2").arg(path, QString::fromUtf8(exception.what()));
    } catch (...) {
        prepared.error = QCoreApplication::translate("DocumentModel", "Failed to open DjVu %1.").arg(path);
    }
#else
    Q_UNUSED(path);
    prepared.error = QCoreApplication::translate("DocumentModel",
                                                 "DjVu support is not available in this build. Install DjVuLibre and rebuild "
                                                 "LLocr (see docs/06-dev-setup.md), or convert the document to PDF.");
#endif
    return prepared;
}

void DocumentModel::appendPreparedDjVu(const PreparedDjVu &prepared)
{
    if (!prepared.error.isEmpty() || prepared.pages.isEmpty())
        return;
    if (!prepared.document)
        return;
    const QString &key = prepared.pages.first().sourcePath;
    if (!m_djvus.contains(key))
        m_djvus.insert(key, prepared.document);
    m_pages.reserve(m_pages.size() + prepared.pages.size());
    m_pages.append(prepared.pages);
}

bool DocumentModel::removePage(int index)
{
    if (!isValidIndex(index))
        return false;
    const QString sourcePath = m_pages.at(index).sourcePath;
    m_pages.removeAt(index);
    m_fullCache.clear();
    evictUnusedSourceDocuments(sourcePath);
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

bool DocumentModel::setRotation(int index, int degrees)
{
    if (!isValidIndex(index))
        return false;
    const int turn = normalizeRotation(degrees);
    if (m_pages[index].rotation == turn)
        return true;
    m_pages[index].rotation = turn;
    m_pages[index].image = QImage();
    m_fullCache.removeAll(index);
    return true;
}

void DocumentModel::clear()
{
    m_pages.clear();
    m_fullCache.clear();
    for (const QString &path : m_pdfOrder)
        retirePdf(path);
    m_pdfOrder.clear();
    drainRetiredPdfs();
    m_djvus.clear();
}

void DocumentModel::evictUnusedSourceDocuments(const QString &path)
{
    if (path.isEmpty())
        return;
    for (const DocumentPage &page : m_pages) {
        if (page.sourcePath == path)
            return;
    }
    if (m_pdfs.contains(path)) {
        retirePdf(path);
        drainRetiredPdfs();
        return;
    }
    m_djvus.remove(path);
}

// A render worker may still hold the handle it borrowed from a RenderRequest, so
// dropping the map entry is not enough to promise a GUI-thread destruction: the
// worker could become the last owner. Park the handle and release it later,
// only once nobody else references it.
void DocumentModel::retirePdf(const QString &path)
{
    const std::shared_ptr<PdfDocument> handle = m_pdfs.take(path);
    m_pdfOrder.removeAll(path);
    if (handle)
        m_retiredPdfs.append(handle);
}

void DocumentModel::drainRetiredPdfs()
{
    for (auto it = m_retiredPdfs.begin(); it != m_retiredPdfs.end();) {
        if (it->use_count() == 1)
            it = m_retiredPdfs.erase(it);
        else
            ++it;
    }
}

bool DocumentModel::isValidIndex(int index) const
{
    return index >= 0 && index < m_pages.size();
}

QImage DocumentModel::renderFull(const DocumentPage &page, QString *error)
{
    // The page is turned after the source is rendered: the source keeps its
    // own resolution, and the turned image is what every consumer of the page
    // sees (the preview, the model, the block crops).
    return rotateImage(renderSource(page, error), page.rotation);
}

QImage DocumentModel::renderSource(const DocumentPage &page, QString *error)
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
#ifdef LLOCR_HAVE_DJVU
    if (page.sourceType == DocumentSource::DjVu) {
        const auto document = m_djvus.value(page.sourcePath);
        if (!document) {
            if (error)
                *error = QCoreApplication::translate("DocumentModel", "DjVu document is not open: %1").arg(page.sourcePath);
            return {};
        }
        return document->render(page.sourcePageIndex, page.pixelSize, error);
    }
#endif
    if (page.sourceType == DocumentSource::Pdf) {
        const std::shared_ptr<PdfDocument> pdf = pdfFor(page.sourcePath);
        if (!pdf) {
            if (error)
                *error = QStringLiteral("Failed to open %1 as a PDF document").arg(page.sourcePath);
            return QImage();
        }
        QImage image = pdf->render(page.sourcePageIndex, page.pixelSize);
        if (image.isNull()) {
            image = QImage(page.pixelSize.isEmpty() ? QSize(800, 1000) : page.pixelSize, QImage::Format_ARGB32);
            image.fill(Qt::white);
        }
        return image;
    }

    DocumentPage decoded;
    decoded.sourcePath = page.sourcePath;
    return decodeSourceCopy(decoded, error);
}

DocumentModel::RenderRequest DocumentModel::renderRequestFor(int index) const
{
    RenderRequest request;
    if (!isValidIndex(index))
        return request;
    const DocumentPage &source = m_pages[index];
    // The cached full image is not part of the request: the worker produces it.
    request.page = source;
    request.page.image = QImage();
    if (source.sourceType == DocumentSource::DjVu)
        request.djvu = m_djvus.value(source.sourcePath);
    else if (source.sourceType == DocumentSource::Pdf)
        request.pdf = m_pdfs.value(source.sourcePath);
    return request;
}

DocumentModel::RenderRequest DocumentModel::thumbnailRequestFor(int index) const
{
    RenderRequest request = renderRequestFor(index);
    if (isValidIndex(index))
        request.page.pixelSize = thumbnailSizeFor(m_pages[index].pixelSize, m_pages[index].rotation);
    return request;
}

QImage DocumentModel::renderDetached(const RenderRequest &request, QString *error)
{
    return rotateImage(renderDetachedSource(request, error), request.page.rotation);
}

QImage DocumentModel::renderDetachedSource(const RenderRequest &request, QString *error)
{
    const DocumentPage &page = request.page;
    if (error)
        error->clear();

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
#ifdef LLOCR_HAVE_DJVU
    if (page.sourceType == DocumentSource::DjVu) {
        // The DjVuLibre context belongs to the thread that opened it: a
        // worker thread renders with its own handle (ADR 209).
        std::shared_ptr<DjVuDocument> djvu = request.djvu;
        if (!djvu || !djvu->ownedByCurrentThread())
            djvu = djvuForCurrentThread(page.sourcePath, error);
        if (!djvu) {
            if (error && error->isEmpty()) {
                *error = QCoreApplication::translate("DocumentModel", "DjVu document is not open: %1").arg(page.sourcePath);
            }
            return {};
        }
        return djvu->render(page.sourcePageIndex, page.pixelSize, error);
    }
#endif
    if (page.sourceType == DocumentSource::Pdf) {
        // The request carries the already-parsed handle when the model has one
        // resident and the calling thread owns it. A worker thread opens its
        // own handle: a QPdfDocument must not be touched by another thread
        // than the one that created it (ADR 209).
        std::shared_ptr<PdfDocument> pdf = request.pdf;
        if (!pdf || !pdf->ownedByCurrentThread())
            pdf = pdfForCurrentThread(page.sourcePath);
        if (!pdf) {
            if (error)
                *error = QStringLiteral("Failed to open %1 as a PDF document").arg(page.sourcePath);
            return QImage();
        }
        QImage image = pdf->render(page.sourcePageIndex, page.pixelSize);
        if (image.isNull()) {
            image = QImage(page.pixelSize.isEmpty() ? QSize(800, 1000) : page.pixelSize, QImage::Format_ARGB32);
            image.fill(Qt::white);
        }
        return image;
    }

    return decodeSourceCopy(page, error);
}

namespace {

// The handles a thread uses to render. A QPdfDocument belongs to the thread
// that created it, and a DjVuLibre context is no better, so a worker thread
// keeps its own handle for the document it renders. One document at a time
// is enough: the app shows one document, and the model keeps its own handles
// for the rest (ADR 209).
struct ThreadDocuments {
    QString path;
    std::shared_ptr<PdfDocument> pdf;
    std::shared_ptr<DjVuDocument> djvu;
};

thread_local ThreadDocuments t_threadDocuments;

}  // namespace

std::shared_ptr<PdfDocument> DocumentModel::pdfForCurrentThread(const QString &path)
{
    if (t_threadDocuments.path != path) {
        t_threadDocuments.pdf.reset();
        t_threadDocuments.djvu.reset();
        t_threadDocuments.path = path;
    }
    if (t_threadDocuments.pdf)
        return t_threadDocuments.pdf;

    auto pdf = std::make_shared<PdfDocument>();
    if (!pdf->open(path))
        return {};
    t_threadDocuments.pdf = pdf;
    return pdf;
}

std::shared_ptr<DjVuDocument> DocumentModel::djvuForCurrentThread(const QString &path, QString *error)
{
    if (t_threadDocuments.path != path) {
        t_threadDocuments.pdf.reset();
        t_threadDocuments.djvu.reset();
        t_threadDocuments.path = path;
    }
    if (t_threadDocuments.djvu)
        return t_threadDocuments.djvu;

    auto djvu = std::make_shared<DjVuDocument>();
    if (!djvu->open(path, error))
        return {};
    t_threadDocuments.djvu = djvu;
    return djvu;
}

QImage DocumentModel::decodeSourceCopy(const DocumentPage &page, QString *error)
{
    QImageReader reader(page.sourcePath);
    reader.setAutoTransform(true);
    reader.setAllocationLimit(kMaxDecodeBytes);
    QImage image = reader.read();
    if (image.isNull()) {
        if (error) {
            *error = QStringLiteral("Failed to read %1: %2").arg(page.sourcePath, reader.errorString().isEmpty() ? QStringLiteral("unknown error") : reader.errorString());
        }
        return {};
    }
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32) {
        image = image.convertToFormat(QImage::Format_ARGB32);
    }
    return image;
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

std::shared_ptr<PdfDocument> DocumentModel::pdfFor(const QString &path)
{
    drainRetiredPdfs();
    const std::shared_ptr<PdfDocument> resident = m_pdfs.value(path);
    if (resident) {
        m_pdfOrder.removeAll(path);
        m_pdfOrder.prepend(path);
        return resident;
    }
    auto pdf = std::make_shared<PdfDocument>();
    if (!pdf->open(path))
        return nullptr;
    m_pdfs.insert(path, pdf);
    m_pdfOrder.prepend(path);
    while (m_pdfOrder.size() > kResidentPdfLimit) {
        const QString evicted = m_pdfOrder.takeLast();
        if (evicted != path)
            retirePdf(evicted);
    }
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

QSize DocumentModel::thumbnailSizeFor(const QSize &pixelSize, int rotation)
{
    // The bound belongs to the page as it is shown, so the source is fitted to
    // the bound turned back: the source keeps its own aspect and the turned
    // thumbnail still fits the box.
    const QSize display = fitWithin(rotatedSize(pixelSize, rotation), QSize(kThumbMaxWidth, kThumbMaxHeight));
    return rotatedSize(display, -rotation);
}

}  // namespace llocr
