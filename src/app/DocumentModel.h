#pragma once

#include "app/DjVuDocument.h"
#include "app/PageRotation.h"
#include "app/PdfDocument.h"
#include "core/OcrResult.h"
#include <functional>
#include <memory>
#include <QHash>
#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

namespace llocr {

enum class DocumentSource { Image, Pdf, DjVu };

// How the page's structured result came to be: the page OCR task returns the
// blocks and their text in one reply, the layout task returns the blocks only
// (the text arrives block by block), the raw task returns text without blocks.
enum class PageRecognitionMode : int { None = 0, MarkupOnly = 1, RawOcr = 2, MarkupOcr = 3 };

// The verification aggregate over the page's subject blocks: a verdict on the
// effective layer of every block the check would ask. Incomplete = at least
// one subject block is unanswered, or there are none.
enum class PageVerification : int { Incomplete = 0, HasErrors = 1, Clean = 2 };

// What the orientation check found about the page. None = the picture read the
// axis of the text and the page lies straight. Sideways = the page lies on its
// side: the picture cannot tell which way it has to be turned, so the user
// turns it by hand. Unclear = the picture cannot read the page at all.
enum class PageOrientationProblem : int { None = 0, Sideways = 1, Unclear = 2 };

// Pure derivations over a recognized page (the rework plan's §6).
namespace PageStatus {

// Subjects = the enabled block types ∪ duplicate suspects. A Review or a
// Mismatch verdict on the effective layer makes the page erroneous; one
// unanswered subject leaves it Incomplete.
PageVerification verificationAggregate(const OcrPage &page, const std::function<bool(const QString &label)> &isTypeEnabled);

// MarkupOnly → MarkupOcr once every block that is expected to carry text does;
// the predicate decides which blocks those are (image placeholders never do).
PageRecognitionMode recognitionModeAfterBlockText(PageRecognitionMode mode, const OcrPage &page, const std::function<bool(const BoundingBox &box)> &expectsText);

// The blocks that still wait for the fixer (ADR 188): the decision model left
// them open and the fixer has not answered them yet — the same set the fix
// queue picks up, so the toolbar badge matches its work.
int pendingFixCount(const OcrPage &page);

}  // namespace PageStatus

struct DocumentPage {
    QImage image;
    OcrResult result;
    bool recognized = false;
    PageRecognitionMode recognitionMode = PageRecognitionMode::None;
    QString sourcePath;
    DocumentSource sourceType = DocumentSource::Image;
    int sourcePageIndex = -1;
    QSize pixelSize;
    QString sourceError;
    QString parseNote;                                                         ///< Non-fatal parser diagnostic for the last recognition.
    int rotation = 0;                                                          ///< The clockwise turn the renderer applies to the source (0, 90, 180, 270).
    double skewDegrees = 0.0;                                                  ///< The straightening the renderer applies after the turn: the skew the orientation check measured.
    bool userOriented = false;                                                 ///< The user turned this page by hand: the orientation check leaves it alone.
    PageOrientationProblem orientationProblem = PageOrientationProblem::None;  ///< What the orientation check found: the page needs a turn by hand.

    // The size the page is shown in: the source size after the turn and the
    // straightening. The model's geometry is read against the rendered page,
    // so this is the aspect the parsers need.
    QSize displaySize() const { return deskewedSize(rotatedSize(pixelSize, rotation), skewDegrees); }
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
        std::shared_ptr<PdfDocument> pdf;
    };

    static QImage renderDetached(const RenderRequest &request, QString *error = nullptr);
    RenderRequest renderRequestFor(int index) const;
    RenderRequest thumbnailRequestFor(int index) const;

    DocumentModel() = default;
    ~DocumentModel();
    Q_DISABLE_COPY_MOVE(DocumentModel)

    bool appendImage(const QString &path);
    bool appendPdf(const QString &path);
    bool appendDjVu(const QString &path, QString *error = nullptr);
    bool appendFile(const QString &path, QString *error = nullptr);

    bool removePage(int index);
    bool movePage(int from, int to);
    void clear();

    // The page's turn changes: the rendered copy is dropped, so the next
    // render draws the page turned.
    bool setRotation(int index, int degrees);
    bool setSkew(int index, double degrees);

    int pageCount() const { return m_pages.size(); }
    bool isEmpty() const { return m_pages.isEmpty(); }

    DocumentPage &page(int index) { return m_pages[index]; }
    const DocumentPage &page(int index) const { return m_pages[index]; }

    bool isValidIndex(int index) const;

    QImage fullImage(int index, QString *error = nullptr);

    static QSize thumbnailSizeFor(const QSize &pixelSize, int rotation = 0);

private:
    static std::shared_ptr<PdfDocument> pdfForCurrentThread(const QString &path);
    static std::shared_ptr<DjVuDocument> djvuForCurrentThread(const QString &path, QString *error);
    static QImage decodeSourceCopy(const DocumentPage &page, QString *error = nullptr);
    QImage renderSource(const DocumentPage &page, QString *error);
    static QImage renderDetachedSource(const RenderRequest &request, QString *error);
    QImage renderFull(const DocumentPage &page, QString *error = nullptr);
    void ensureFullImage(int index, QString *error = nullptr);
    void evictFullImages();
    void evictUnusedSourceDocuments(const QString &path);
    std::shared_ptr<PdfDocument> pdfFor(const QString &path);
    void retirePdf(const QString &path);
    void drainRetiredPdfs();

private:
    static constexpr int kResidentPdfLimit = 2;

    QList<DocumentPage> m_pages;
    QHash<QString, std::shared_ptr<PdfDocument>> m_pdfs;
    QList<QString> m_pdfOrder;
    QList<std::shared_ptr<PdfDocument>> m_retiredPdfs;
    QHash<QString, std::shared_ptr<DjVuDocument>> m_djvus;
    QList<int> m_fullCache;
};

}  // namespace llocr
