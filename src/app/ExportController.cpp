#include "app/ExportController.h"

#include <algorithm>
#include <utility>

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QMarginsF>
#include <QPageSize>
#include <QtConcurrent/QtConcurrentRun>

namespace llocr {

ExportController::ExportController(Deps deps, QObject *parent)
    : QObject(parent)
    , m_deps(std::move(deps))
{
    connect(&m_exportRenderer, &ExportRenderer::progress, this,
            [this](int pagesDone, int pagesTotal) {
        if (m_exporting)
            emit statusRequested(
                tr("Exporting… (%1/%2)").arg(pagesDone).arg(pagesTotal));
    });
}

QList<Exporter::Page> ExportController::collectPages(int scope, int currentPage,
                                                     int fromPage,
                                                     int toPage) const
{
    int lo = 0;
    int hi = m_deps.document.pageCount() - 1;

    switch (scope) {
    case ExportCurrent:
        lo = hi = currentPage;
        break;
    case ExportRange:
        lo = fromPage - 1;
        hi = toPage - 1;
        if (lo > hi)
            std::swap(lo, hi);
        lo = (std::max)(0, lo);
        hi = (std::min)(m_deps.document.pageCount() - 1, hi);
        break;
    case ExportAll:
        break;
    }

    QList<Exporter::Page> pages;
    for (int i = lo; i <= hi; ++i) {
        if (!m_deps.document.isValidIndex(i)
            || !m_deps.document.page(i).recognized)
            continue;
        Exporter::Page p;
        p.number = i + 1;
        p.text = m_deps.editStore.effectiveText(m_deps.document, i);
        pages.append(p);
    }
    return pages;
}

bool ExportController::exportPages(const QUrl &fileUrl, int scope,
                                   int currentPage, int fromPage, int toPage)
{
    if (m_deps.importingBusy())
        return false;
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile()
                                               : fileUrl.toString();
    if (path.isEmpty()) {
        emit statusRequested(tr("No output path."));
        return false;
    }

    if (m_exporting) {
        emit statusRequested(tr("An export is already in progress."));
        return false;
    }

    const QList<Exporter::Page> pages =
        collectPages(scope, currentPage, fromPage, toPage);
    if (pages.isEmpty()) {
        emit statusRequested(tr("Nothing to export for the selected pages "
                                "(no recognized pages in that selection)."));
        return false;
    }

    QHash<QPair<int, int>, QImage> crops;
    const QList<QPair<int, int>> refs = Exporter::referencedCrops(pages);
    for (const auto &ref : refs)
        crops.insert(ref, m_deps.cropProvider(ref.first - 1, ref.second));

    const Exporter::CropProvider cropProvider = [crops](int pageNumber, int boxIndex) {
        return crops.value({pageNumber, boxIndex});
    };

    const Exporter::ExportOptions options{ m_deps.settings.splitPages() };
    const QPageLayout pdfLayout = pdfPageLayout();

    m_exporting = true;
    emit exportingChanged();
    emit statusRequested(tr("Exporting…"));

    const Exporter::Format format =
        Exporter::formatForSuffix(QFileInfo(path).suffix());

    if (format == Exporter::Format::Html || format == Exporter::Format::Pdf) {
        QtConcurrent::run([pages, cropProvider]() {
            QList<ExportRenderer::PageInput> embedded;
            embedded.reserve(pages.size());
            for (const Exporter::Page &page : pages) {
                embedded.append(
                    { page.number, Exporter::embedImagesAsDataUrls(
                                       page.text,
                                       [&page, cropProvider](int boxIndex) {
                                           return cropProvider(page.number, boxIndex);
                                       }) });
            }
            return embedded;
        })
            .then(this,
                  [this, pages, path, format, cropProvider, options, pdfLayout](
                      QList<ExportRenderer::PageInput> embedded) {
                      const ExportRenderer::Output output =
                          format == Exporter::Format::Pdf
                              ? ExportRenderer::Output::Pdf
                              : ExportRenderer::Output::Html;
                      ExportRenderer::Request request;
                      request.output = output;
                      request.pages = embedded;
                      request.styleSheet =
                          Exporter::exportStyleSheet(options.splitPages);
                      request.outputPath = path;
                      request.splitPages = options.splitPages;
                      request.pageLayout = pdfLayout;
                      m_exportRenderer.render(
                          request,
                          [this, pages, path, format, cropProvider, options,
                           pdfLayout](bool ok, const QString &html,
                                      const QString &error) {
                              const Exporter::Result result = finalizeRenderedExport(
                                  format, path, pages, cropProvider, options,
                                  pdfLayout, ok, html, error);
                              finishExport(result, pages.size());
                          });
                  });
        return true;
    }

    QtConcurrent::run(
        [exporter = m_exporter, pages, path, cropProvider, options]() {
            return exporter.exportToFile(pages, path, cropProvider, options);
        })
        .then(this, [this, pageCount = pages.size()](const Exporter::Result &result) {
            finishExport(result, pageCount);
        });
    return true;
}

void ExportController::finishExport(const Exporter::Result &result,
                                    int pageCount)
{
    m_exporting = false;
    emit exportingChanged();
    emit statusRequested(result.success
                             ? tr("%1 (%2 page(s)).").arg(result.message).arg(pageCount)
                             : result.message);
}

QPageLayout ExportController::pdfPageLayout() const
{
    const int marginMm = qBound(0, m_deps.settings.pdfMarginMm(), 50);
    return QPageLayout(QPageSize(QPageSize::A4),
                       m_deps.settings.pdfLandscape() ? QPageLayout::Landscape
                                                      : QPageLayout::Portrait,
                       QMarginsF(marginMm, marginMm, marginMm, marginMm),
                       QPageLayout::Millimeter);
}

Exporter::Result ExportController::finalizeRenderedExport(
    Exporter::Format format, const QString &path,
    const QList<Exporter::Page> &pages, const Exporter::CropProvider &crop,
    const Exporter::ExportOptions &options, const QPageLayout &pdfLayout,
    bool renderOk, const QString &renderedHtml,
    const QString &renderError) const
{
    if (format == Exporter::Format::Pdf) {
        if (renderOk)
            return Exporter::Result::ok(
                QCoreApplication::translate("Exporter", "Exported to %1")
                    .arg(QFileInfo(path).fileName()));
        const Exporter::Result fb =
            Exporter::writePdfFallback(pages, path, crop, pdfLayout,
                                       options.splitPages);
        if (fb.success)
            return Exporter::Result::ok(
                QCoreApplication::translate(
                    "Exporter", "Exported PDF using the built-in writer (%1).")
                    .arg(renderError));
        return fb;
    }

    if (renderOk)
        return Exporter::writeTextFile(
            path, Exporter::assembleHtmlDocument({renderedHtml}));

    const Exporter::Result fb =
        m_exporter.exportToFile(pages, path, crop, options);
    if (fb.success)
        return Exporter::Result::ok(
            QCoreApplication::translate(
                "Exporter", "Exported HTML using the basic writer (%1).")
                .arg(renderError));
    return fb;
}

}  // namespace llocr
