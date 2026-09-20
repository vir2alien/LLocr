#include "app/AppController.h"
#include "app/PageIndex.h"

#include <algorithm>
#include <utility>

#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QTimer>
#include <QHash>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QStringView>
#include <QtConcurrent/QtConcurrentRun>
#include <QVariant>

#include "parsers/DetTokensParser.h"
#include "parsers/ParserFactory.h"

#include "app/RequestProfileStore.h"
#include "models/OcrModelFactory.h"


namespace llocr {

AppController::AppController(SettingsStore &settings, RuntimeController &runtime,
                             RequestProfileStore &requestProfiles,
                             RequestProfileStore &checkRequestProfiles,
                             QObject *parent)
    : m_settings(settings)
    , m_runtime(runtime)
    , m_recognition(
          settings, runtime, requestProfiles,
          [this](int index, QString &error) { return pageImage(index, &error); },
                    nullptr, [this](int index) {
                        QReadLocker locker(&m_documentLock);
                        return m_document.isValidIndex(index) && !m_document.page(index).sourceError.isEmpty();
                    })
    , m_check(checkRequestProfiles, runtime, nullptr)
    , QObject(parent)
{
    connect(&m_recognition, &RecognitionController::busyChanged, this, [this]() {
        emit busyChanged();
    });
    connect(&m_recognition, &RecognitionController::statusRequested, this,
            [this](const QString& message) { setStatus(message); });
    connect(&m_recognition, &RecognitionController::rawResultReady, this,
            &AppController::applyRawResult);

    connect(&m_check, &CheckController::checkFinished, this,
            [this](bool success, const QString &text, const QString &errorMessage) {
                if (m_currentPage != m_checkPage || m_selectedBox != m_checkBox) {
                    m_checkSucceeded = false;
                    m_checkApplied = false;
                    m_checkResultText.clear();
                    m_checkError.clear();
                    m_checkPage = -1;
                    m_checkBox = -1;
                    emit checkStateChanged();
                    return;
                }
                m_checkSucceeded = success;
                m_checkApplied = false;
                m_checkResultText = text;
                m_checkError = errorMessage;
                emit checkStateChanged();
            });
    connect(&m_check, &CheckController::busyChanged, this,
            [this]() { emit checkStateChanged(); });
    connect(&m_check, &CheckController::statusRequested, this,
            [this](const QString &message) { setStatus(message); });

    connect(&m_boxModel, &BoxListModel::boxRemoved, this, &AppController::onBoxRemoved);

    connect(&m_exportRenderer, &ExportRenderer::progress, this,
            [this](int pagesDone, int pagesTotal) {
        if (m_exporting)
            setStatus(tr("Exporting… (%1/%2)").arg(pagesDone).arg(pagesTotal));
    });

    connect(this, &AppController::pageChanged, this, [this]() {
        ++m_imageRevision;
        emit imageRevisionChanged();
    });
    connect(this, &AppController::documentChanged, this, [this]() {
        ++m_docRevision;
        emit docRevisionChanged();
        ++m_imageRevision;
        emit imageRevisionChanged();
    });

    connect(&m_settings, &SettingsStore::modelNameChanged, this, [this]() {
        emit configChanged();
    });
    connect(&m_runtime, &RuntimeController::stateChanged, this, [this]() {
        emit configChanged();
    });
    connect(&m_runtime, &RuntimeController::busyStateChanged, this, [this]() {
        emit configChanged();
    });
    connect(&m_runtime, &RuntimeController::configValidChanged, this, [this]() {
        emit configChanged();
    });
}

QStringList AppController::parserNames() const
{
    return ParserFactory::registeredIds();
}

QStringList AppController::modelNames() const
{
    QStringList names;
    const QStringList ids = OcrModelFactory::registeredIds();
    for (const QString &id : ids)
        names.append(OcrModelFactory::displayNameForId(id));
    return names;
}

QString AppController::modelIdToName(const QString &modelId) const
{
    return OcrModelFactory::displayNameForId(modelId);
}

QString AppController::modelNameToId(const QString &modelName) const
{
    return OcrModelFactory::idForDisplayName(modelName);
}

bool AppController::hasImage() const
{
    return !m_document.isEmpty();
}

bool AppController::hasResult() const
{
    for (int i = 0; i < m_document.pageCount(); ++i) {
        if (m_document.page(i).recognized)
            return true;
    }
    return false;
}

bool AppController::canRecognize() const
{
    if (m_document.isEmpty() || m_recognition.busy() || m_importing
        || m_check.busy())
        return false;
    return m_runtime.canRecognize(true);
}

QString AppController::effectiveText(int index) const
{
    return m_editStore.effectiveText(m_document, index);
}

QString AppController::currentPageWarning() const
{
    QReadLocker locker(&m_documentLock);
    return m_document.isValidIndex(m_currentPage) ? m_document.page(m_currentPage).sourceError
                                                 : QString();
}

QString AppController::resultText() const
{
    return effectiveText(m_currentPage);
}

bool AppController::currentPageEditable() const
{
    return m_document.isValidIndex(m_currentPage) && m_document.page(m_currentPage).recognized;
}

bool AppController::currentPageEdited() const
{
    return m_editStore.isEdited(m_currentPage);
}

QImage AppController::currentImage()
{
    return pageImage(m_currentPage);
}

QImage AppController::pageImage(int index, QString *error)
{
    QWriteLocker locker(&m_documentLock);
    return m_document.fullImage(index, error);
}

QImage AppController::pageThumbnail(int index) const
{
    QReadLocker locker(&m_documentLock);
    return m_document.thumbnail(index);
}

QImage AppController::croppedImage(int pageIndex, int boxIndex)
{
    QWriteLocker locker(&m_documentLock);
    if (!m_document.isValidIndex(pageIndex))
        return {};
    const DocumentPage& page = m_document.page(pageIndex);
    if (!page.recognized || page.result.pages.isEmpty())
        return {};
    const QList<BoundingBox>& boxes = page.result.pages[0].boxes;
    if (boxIndex < 0 || boxIndex >= boxes.size())
        return {};

    const QRectF norm = boxes.at(boxIndex).rect;
    if (norm.width() <= 0.0 || norm.height() <= 0.0)
        return {};

    m_document.fullImage(pageIndex);
    const QImage& img = m_document.page(pageIndex).image;
    if (img.isNull())
        return {};

    QRect px(qRound(norm.x() * img.width()),
             qRound(norm.y() * img.height()),
             qRound(norm.width() * img.width()),
             qRound(norm.height() * img.height()));
    px = px.intersected(img.rect());
    if (px.width() < 1 || px.height() < 1)
        return {};
    return img.copy(px);
}

void AppController::setCurrentPage(int index)
{
    if (!m_document.isValidIndex(index) || index == m_currentPage)
        return;

    m_currentPage = index;
    m_pageModel.setCurrent(index);
    updateBoxesForCurrent();

    notifyPageChanged();
}

void AppController::updateBoxesForCurrent()
{
    if (m_document.isValidIndex(m_currentPage) && m_document.page(m_currentPage).recognized)
        m_boxModel.setFromResult(m_document.page(m_currentPage).result);
    else
        m_boxModel.setBoxes({});
    setSelectedBoxIndex(-1);
}

struct AppController::ImportState {
    QStringList paths;
    int next = 0;
    int addedFiles = 0;
    int addedPages = 0;
    int skipped = 0;
    QString firstError;
    QStringList warnings;
};

void AppController::openFiles(const QVariantList& fileUrls)
{
    if (m_recognition.busy() || m_importing || m_exporting)
        return;

    QStringList paths;
    for (const QVariant& variant : fileUrls) {
        const QUrl url = variant.toUrl();
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (!path.isEmpty())
            paths.append(path);
    }

    if (paths.isEmpty()) {
        setStatus(tr("No files selected."));
        return;
    }

    auto state = std::make_shared<ImportState>();
    state->paths = std::move(paths);
    m_importing = true;
    emit importingChanged();
    emit configChanged();
    importNextFile(state);
}

void AppController::importNextFile(const std::shared_ptr<ImportState>& state)
{
    if (state->next >= state->paths.size()) {
        finishImport(*state);
        return;
    }
    const QString path = state->paths.at(state->next++);
    setStatus(tr("Importing %1 (%2/%3)…")
                  .arg(QFileInfo(path).fileName()).arg(state->next).arg(state->paths.size()));
    const int pagesBefore = m_document.pageCount();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("djvu") || suffix == QStringLiteral("djv")) {
        auto *watcher = new QFutureWatcher<DocumentModel::PreparedDjVu>(this);
        connect(watcher, &QFutureWatcher<DocumentModel::PreparedDjVu>::finished, this,
                [this, watcher, state, pagesBefore]() {
            const auto prepared = watcher->result();
            watcher->deleteLater();
            {
                QWriteLocker locker(&m_documentLock);
                m_document.appendPreparedDjVu(prepared);
            }
            state->warnings.append(prepared.warnings);
            recordImportedFile(state, pagesBefore, prepared.error);
        });
        watcher->setFuture(QtConcurrent::run([path]() {
            return DocumentModel::prepareDjVu(path);
        }));
        return;
    }
    QString error;
    {
        QWriteLocker locker(&m_documentLock);
        m_document.appendFile(path, &error);
    }
    recordImportedFile(state, pagesBefore, error);
}

void AppController::recordImportedFile(const std::shared_ptr<ImportState>& state,
                                       int pagesBefore, const QString& error)
{
    const int added = m_document.pageCount() - pagesBefore;
    if (added > 0) {
        ++state->addedFiles;
        state->addedPages += added;
        if (pagesBefore == 0)
            m_currentPage = 0;
        m_pageModel.appendPages(added);
        m_pageModel.setCurrent(m_currentPage);
        updateBoxesForCurrent();
        notifyDocumentChanged();
    } else {
        ++state->skipped;
        if (state->firstError.isEmpty())
            state->firstError = error;
    }
    QTimer::singleShot(0, this, [this, state]() { importNextFile(state); });
}

void AppController::finishImport(const ImportState& state)
{
    if (state.addedPages == 0) {
        setStatus(state.firstError.isEmpty() ? tr("None of the selected files could be added.")
                                            : state.firstError);
    } else if (state.skipped > 0) {
        setStatus(tr("Added %1 file(s), %2 page(s); %3 file(s) skipped.")
                      .arg(state.addedFiles).arg(state.addedPages).arg(state.skipped));
    } else {
        setStatus(tr("Added %1 file(s), %2 page(s).")
                      .arg(state.addedFiles).arg(state.addedPages));
    }
    if (!state.warnings.isEmpty())
        setStatus(m_statusMessage + QStringLiteral("\n")
                  + tr("Warning: %1 page(s) replaced with blank pages. %2")
                        .arg(state.warnings.size()).arg(state.warnings.first()));
    m_importing = false;
    emit importingChanged();
    emit configChanged();
}

bool AppController::removePage(int index)
{
    if (m_recognition.busy() || m_importing)
        return false;
    if (!m_document.isValidIndex(index))
        return false;

    {
        QWriteLocker locker(&m_documentLock);
        m_document.removePage(index);
    }

    if (m_document.isEmpty()) {
        m_editStore.clear();
        m_currentPage = 0;
        m_pageModel.clear();
        m_boxModel.setBoxes({});

        setStatus(tr("Page %1 deleted.").arg(index + 1));

        notifyDocumentChanged();
        return true;
    }

    m_editStore.remapAfterRemove(index);

    if (m_currentPage > index)
        --m_currentPage;
    else if (m_currentPage == index)
        m_currentPage = (std::min)(m_currentPage, m_document.pageCount() - 1);

    m_pageModel.removePage(index);
    m_pageModel.setCurrent(m_currentPage);

    updateBoxesForCurrent();

    setStatus(tr("Page %1 deleted.").arg(index + 1));

    notifyDocumentChanged();
    return true;
}

bool AppController::movePage(int from, int to)
{
    if (m_recognition.busy() || m_importing)
        return false;
    if (!m_document.isValidIndex(from) || !m_document.isValidIndex(to))
        return false;
    if (from == to)
        return true;

    {
        QWriteLocker locker(&m_documentLock);
        m_document.movePage(from, to);
    }
    m_pageModel.movePage(from, to);

    m_editStore.remapAfterMove(from, to);

    m_currentPage = remapIndexAfterMove(m_currentPage, from, to);

    m_pageModel.setCurrent(m_currentPage);
    updateBoxesForCurrent();

    setStatus(tr("Moved page %1 to position %2.").arg(from + 1).arg(to + 1));

    notifyDocumentChanged();
    return true;
}

void AppController::recognizeCurrent()
{
    if (m_recognition.busy() || m_importing || m_document.isEmpty())
        return;
    if (!canRecognize()) {
        setStatus(tr("Set a model name in Settings first."));
        return;
    }

    m_recognition.startCurrent(m_currentPage, m_document.pageCount());
}

void AppController::recognizeAll()
{
    if (m_recognition.busy() || m_importing || m_document.isEmpty())
        return;
    if (!canRecognize()) {
        setStatus(tr("Set a model name in Settings first."));
        return;
    }

    m_recognition.startAll(m_document.pageCount());
}

void AppController::applyRawResult(int index, const OcrResult& rawResult)
{
    if (!m_document.isValidIndex(index))
        return;

    OcrResult parsed = rawResult;
    if (auto parser = ParserFactory::create(m_settings.parserId())) {
        if (auto det = dynamic_cast<DetTokensParser *>(parser.get()))
            det->setKeepPageNumbers(m_settings.keepPageNumbers());
        parsed = parser->parse(rawResult.text);
    }

    DocumentPage& page = m_document.page(index);
    page.result = parsed;
    page.recognized = true;

    m_pageModel.setRecognized(index, true);

    const bool hadDups = !parsed.pages.isEmpty() && parsed.pages.first().hasDuplicates;
    if (hadDups)
        m_pageModel.setHasDuplicates(index, true);

    const bool droppedEdit = m_editStore.revert(index);
    if (droppedEdit)
        m_pageModel.setEdited(index, false);

    if (index == m_currentPage) {
        updateBoxesForCurrent();
        emit resultChanged();
        emit boxesChanged();
        if (droppedEdit)
            emit editStateChanged();
    } else {
        emit resultChanged();
    }
}

void AppController::stop()
{
    m_recognition.stop();
}

void AppController::setCurrentPageText(const QString& text)
{
    if (!currentPageEditable())
        return;

    const int index = m_currentPage;
    const QString original = m_document.page(index).result.text;

    switch (m_editStore.setText(index, original, text)) {
    case PageEditStore::Change::NowEdited:
        m_pageModel.setEdited(index, true);
        emit editStateChanged();
        break;
    case PageEditStore::Change::NowClean:
        m_pageModel.setEdited(index, false);
        emit editStateChanged();
        break;
    case PageEditStore::Change::None:
        break;
    }
}

void AppController::revertCurrentPageEdits()
{
    const int index = m_currentPage;
    if (m_editStore.revert(index)) {
        m_pageModel.setEdited(index, false);
        emit resultChanged();
        emit editStateChanged();
    }
}

void AppController::onBoxRectChanged(int boxIndex, qreal x, qreal y,
                                     qreal width, qreal height)
{
    if (!m_document.isValidIndex(m_currentPage))
        return;
    DocumentPage& page = m_document.page(m_currentPage);
    if (!page.recognized || page.result.pages.isEmpty())
        return;
    QList<BoundingBox>& boxes = page.result.pages[0].boxes;
    if (boxIndex < 0 || boxIndex >= boxes.size())
        return;
    boxes[boxIndex].rect = QRectF(x, y, width, height);
    ++m_cropRevision;

    m_boxModel.updateBoxRect(boxIndex, x, y, width, height);

    emit boxesChanged();
    emit imageChanged();
}

void AppController::onBoxRemoved(int boxIndex)
{
    if (!m_document.isValidIndex(m_currentPage))
        return;
    DocumentPage& page = m_document.page(m_currentPage);
    if (!page.recognized || page.result.pages.isEmpty())
        return;
    QList<BoundingBox>& boxes = page.result.pages[0].boxes;
    if (boxIndex < 0 || boxIndex >= boxes.size())
        return;

    boxes.removeAt(boxIndex);
    ++m_cropRevision;

    m_editStore.replace(m_currentPage,
                        rebuildPageText(page.result.pages[0],
                                        m_settings.keepPageNumbers()));
    m_pageModel.setEdited(m_currentPage, true);

    if (m_selectedBox == boxIndex)
        setSelectedBoxIndex(-1);
    else if (m_selectedBox > boxIndex)
        setSelectedBoxIndex(m_selectedBox - 1);

    emit boxesChanged();
    emit resultChanged();
    emit editStateChanged();
}

const BoundingBox *AppController::selectedBox() const
{
    if (m_selectedBox < 0 || !m_document.isValidIndex(m_currentPage))
        return nullptr;
    const DocumentPage &page = m_document.page(m_currentPage);
    if (!page.recognized || page.result.pages.isEmpty())
        return nullptr;
    const QList<BoundingBox> &boxes = page.result.pages[0].boxes;
    if (m_selectedBox >= boxes.size())
        return nullptr;
    return &boxes.at(m_selectedBox);
}

QString AppController::selectedBlockText() const
{
    const BoundingBox *box = selectedBox();
    return box ? box->text : QString();
}

QString AppController::selectedBlockLabel() const
{
    const BoundingBox *box = selectedBox();
    return box ? box->label : QString();
}

void AppController::setSelectedBoxIndex(int index)
{
    int clamped = index;
    if (!m_document.isValidIndex(m_currentPage)
        || !m_document.page(m_currentPage).recognized) {
        clamped = -1;
    } else if (index != -1) {
        const int size = m_document.page(m_currentPage).result.pages.isEmpty()
                             ? 0
                             : m_document.page(m_currentPage).result.pages[0].boxes.size();
        if (index < 0 || index >= size)
            clamped = -1;
    }
    if (m_selectedBox == clamped)
        return;
    m_selectedBox = clamped;
    emit selectedBoxChanged();

    if (m_checkSucceeded || m_checkApplied || !m_checkResultText.isEmpty()
        || !m_checkError.isEmpty()) {
        m_checkSucceeded = false;
        m_checkApplied = false;
        m_checkResultText.clear();
        m_checkError.clear();
        m_checkPage = -1;
        m_checkBox = -1;
        emit checkStateChanged();
    }
}

void AppController::checkSelectedBlock(const QString &prompt)
{
    if (m_recognition.busy() || m_check.busy())
        return;
    if (!m_document.isValidIndex(m_currentPage)
        || !m_document.page(m_currentPage).recognized
        || m_selectedBox < 0)
        return;

    const QString text = selectedBlockText();
    if (text.isEmpty())
        return;
    const QImage crop = croppedImage(m_currentPage, m_selectedBox);
    if (crop.isNull())
        return;

    m_checkSucceeded = false;
    m_checkApplied = false;
    m_checkResultText.clear();
    m_checkError.clear();
    m_checkPage = m_currentPage;
    m_checkBox = m_selectedBox;
    emit checkStateChanged();

    m_check.checkBlock(crop, text, prompt);
}

void AppController::applyCheckedText()
{
    if (!m_checkSucceeded || m_checkApplied
        || m_recognition.busy()  // never mutate the document under a running OCR job
        || !m_document.isValidIndex(m_currentPage)
        || m_selectedBox < 0)
        return;
    if (m_currentPage != m_checkPage || m_selectedBox != m_checkBox)
        return;

    QString rebuilt;
    {
        QWriteLocker locker(&m_documentLock);
        DocumentPage &page = m_document.page(m_currentPage);
        if (!page.recognized || page.result.pages.isEmpty())
            return;
        QList<BoundingBox> &boxes = page.result.pages[0].boxes;
        if (m_selectedBox >= boxes.size())
            return;

        boxes[m_selectedBox].text = m_checkResultText;
        ++m_cropRevision;
        rebuilt = rebuildPageText(page.result.pages[0],
                                  m_settings.keepPageNumbers());
    }

    m_editStore.replace(m_currentPage, rebuilt);
    m_pageModel.setEdited(m_currentPage, true);
    m_boxModel.updateBoxText(m_selectedBox, m_checkResultText);

    m_checkApplied = true;

    emit boxesChanged();
    emit resultChanged();
    emit editStateChanged();
    emit selectedBoxChanged();
    emit checkStateChanged();
}

QList<Exporter::Page> AppController::collectPages(int scope, int fromPage, int toPage) const
{
    int lo = 0;
    int hi = m_document.pageCount() - 1;

    switch (scope) {
    case ExportCurrent:
        lo = hi = m_currentPage;
        break;
    case ExportRange:
        lo = fromPage - 1;
        hi = toPage - 1;
        if (lo > hi)
            std::swap(lo, hi);
        lo = (std::max)(0, lo);
        hi = (std::min)(m_document.pageCount() - 1, hi);
        break;
    case ExportAll:
        break;
    }

    QList<Exporter::Page> pages;
    for (int i = lo; i <= hi; ++i) {
        if (!m_document.isValidIndex(i) || !m_document.page(i).recognized)
            continue;
        Exporter::Page p;
        p.number = i + 1;
        p.text = effectiveText(i);
        pages.append(p);
    }
    return pages;
}

bool AppController::exportPages(const QUrl& fileUrl, int scope, int fromPage, int toPage)
{
    if (m_importing)
        return false;
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        setStatus(tr("No output path."));
        return false;
    }

    if (m_exporting) {
        setStatus(tr("An export is already in progress."));
        return false;
    }

    const QList<Exporter::Page> pages = collectPages(scope, fromPage, toPage);
    if (pages.isEmpty()) {
        setStatus(tr("Nothing to export for the selected pages "
                     "(no recognized pages in that selection)."));
        return false;
    }

    QHash<QPair<int, int>, QImage> crops;
    const QList<QPair<int, int>> refs = Exporter::referencedCrops(pages);
    for (const auto& ref : refs)
        crops.insert(ref, croppedImage(ref.first - 1, ref.second));

    const Exporter::CropProvider cropProvider = [crops](int pageNumber, int boxIndex) {
        return crops.value({pageNumber, boxIndex});
    };

    const Exporter::ExportOptions options{ m_settings.splitPages() };
    const QPageLayout pdfLayout = pdfPageLayout();

    m_exporting = true;
    emit exportingChanged();
    setStatus(tr("Exporting…"));

    const Exporter::Format format =
        Exporter::formatForSuffix(QFileInfo(path).suffix());

    if (format == Exporter::Format::Html || format == Exporter::Format::Pdf) {
        QtConcurrent::run([pages, cropProvider]() {
            QList<ExportRenderer::PageInput> embedded;
            embedded.reserve(pages.size());
            for (const Exporter::Page& page : pages) {
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
                    format == Exporter::Format::Pdf ? ExportRenderer::Output::Pdf
                                                    : ExportRenderer::Output::Html;
                ExportRenderer::Request request;
                request.output = output;
                request.pages = embedded;
                request.styleSheet = Exporter::exportStyleSheet(options.splitPages);
                request.outputPath = path;
                request.splitPages = options.splitPages;
                request.pageLayout = pdfLayout;
                m_exportRenderer.render(
                    request,
                    [this, pages, path, format, cropProvider, options, pdfLayout](
                        bool ok, const QString& html, const QString& error) {
                        const Exporter::Result result = finalizeRenderedExport(
                            format, path, pages, cropProvider, options, pdfLayout,
                            ok, html, error);
                        finishExport(result, pages.size());
                    });
            });
        return true;
    }

    QtConcurrent::run(
        [exporter = m_exporter, pages, path, cropProvider, options]() {
            return exporter.exportToFile(pages, path, cropProvider, options);
        })
        .then(this, [this, pageCount = pages.size()](const Exporter::Result& result) {
            finishExport(result, pageCount);
        });
    return true;
}

void AppController::finishExport(const Exporter::Result& result, int pageCount)
{
    m_exporting = false;
    emit exportingChanged();
    setStatus(result.success
                  ? tr("%1 (%2 page(s)).").arg(result.message).arg(pageCount)
                  : result.message);
}

QPageLayout AppController::pdfPageLayout() const
{
    const int marginMm = qBound(0, m_settings.pdfMarginMm(), 50);
    return QPageLayout(QPageSize(QPageSize::A4),
                       m_settings.pdfLandscape() ? QPageLayout::Landscape
                                                 : QPageLayout::Portrait,
                       QMarginsF(marginMm, marginMm, marginMm, marginMm),
                       QPageLayout::Millimeter);
}

Exporter::Result AppController::finalizeRenderedExport(
    Exporter::Format format, const QString& path, const QList<Exporter::Page>& pages,
    const Exporter::CropProvider& crop, const Exporter::ExportOptions& options,
    const QPageLayout& pdfLayout, bool renderOk, const QString& renderedHtml,
    const QString& renderError) const
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
            path, Exporter::assembleHtmlDocument({ renderedHtml }));

    const Exporter::Result fb = m_exporter.exportToFile(pages, path, crop, options);
    if (fb.success)
        return Exporter::Result::ok(
            QCoreApplication::translate(
                "Exporter", "Exported HTML using the basic writer (%1).")
                .arg(renderError));
    return fb;
}

QStringList AppController::exportNameFilters() const
{
    QStringList filters;
    filters << tr("Markdown (*.md)") << tr("Plain text (*.txt)") << tr("HTML (*.html)");
    if (Exporter::isPandocAvailable())
        filters << tr("Word document (*.docx)");
    filters << tr("PDF (*.pdf)");
    return filters;
}

void AppController::setStatus(const QString& message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusChanged();
}

void AppController::notifyDocumentChanged()
{
    emit documentChanged();
    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
    emit boxesChanged();
    emit editStateChanged();
}

void AppController::notifyPageChanged()
{
    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
    emit boxesChanged();
    emit editStateChanged();
}

QString AppController::resolveImagesForPreview(const QString& markdown)
{
    if (m_previewCacheRevision == m_imageRevision
        && m_previewCacheCropRevision == m_cropRevision
        && m_previewCacheText == markdown)
        return m_previewCacheResult;

    static const QRegularExpression re(
        QStringLiteral(R"(!\[([^\]]*)\]\(image://ocr/crop/(\d+)\))"));

    QString result;
    qsizetype last = 0;
    auto it = re.globalMatch(markdown);

    QStringView markdownView(markdown);
    while (it.hasNext()) {
        const auto m = it.next();
        const qsizetype start = m.capturedStart();
        result += markdownView.sliced(last, start - last);

        const int boxIndex = m.captured(2).toInt();
        const QImage img = croppedImage(m_currentPage, boxIndex);
        if (!img.isNull()) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            if (buffer.open(QIODevice::WriteOnly)) {
                img.save(&buffer, "PNG");
                result += QStringLiteral("![%1](data:image/png;base64,%2)")
                              .arg(m.captured(1), QString::fromLatin1(bytes.toBase64()));
            } else {
                result += m.capturedView();
            }
        } else {
            result += m.capturedView();
        }
        last = m.capturedEnd();
    }
    result += markdownView.sliced(last);
    m_previewCacheRevision = m_imageRevision;
    m_previewCacheCropRevision = m_cropRevision;
    m_previewCacheText = markdown;
    m_previewCacheResult = result;
    return result;
}

}  // namespace llocr
