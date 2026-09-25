#pragma once

#include <QImage>
#include <QObject>
#include <QPageLayout>
#include <QUrl>
#include <functional>

#include "app/DocumentModel.h"
#include "app/Exporter.h"
#include "app/ExportRenderer.h"
#include "app/PageEditStore.h"
#include "app/SettingsStore.h"

namespace llocr {

// Owns the export pipeline: page collection, the pandoc/built-in writer path
// and the rendered HTML/PDF path with fallback. Reads the document through
// injected references; the owner supplies crop images and the importing flag.
class ExportController : public QObject
{
    Q_OBJECT

public:
    struct Deps {
        DocumentModel &document;
        PageEditStore &editStore;
        SettingsStore &settings;
        // 0-based page index + box index -> cropped image.
        std::function<QImage(int pageIndex, int boxIndex)> cropProvider;
        std::function<bool()> importingBusy;
    };

    explicit ExportController(Deps deps, QObject *parent = nullptr);

    bool exporting() const { return m_exporting; }

    bool exportPages(const QUrl &fileUrl, int scope, int currentPage,
                     int fromPage, int toPage);

signals:
    void exportingChanged();
    void statusRequested(const QString &message);

private:
    enum ExportScope : int {
        ExportAll = 0,
        ExportCurrent = 1,
        ExportRange = 2,
    };

    QList<Exporter::Page> collectPages(int scope, int currentPage,
                                       int fromPage, int toPage) const;
    QPageLayout pdfPageLayout() const;
    Exporter::Result finalizeRenderedExport(
        Exporter::Format format, const QString &path,
        const QList<Exporter::Page> &pages, const Exporter::CropProvider &crop,
        const Exporter::ExportOptions &options, const QPageLayout &pdfLayout,
        bool renderOk, const QString &renderedHtml,
        const QString &renderError) const;
    void finishExport(const Exporter::Result &result, int pageCount);

    Deps m_deps;
    Exporter m_exporter;
    ExportRenderer m_exportRenderer;
    bool m_exporting = false;
};

}  // namespace llocr
