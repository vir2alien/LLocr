#include "app/AppController.h"
#include "app/PageIndex.h"

#include <algorithm>
#include <utility>

#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QMarginsF>
#include <QMutex>
#include <QPageLayout>
#include <QPageSize>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QStringView>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>
#include <QVariant>

#include "parsers/ParserFactory.h"

#include "config/RequestProfileStore.h"
#include "core/StatusMessage.h"
#include "models/OcrModelFactory.h"

namespace llocr {
namespace {

constexpr qint64 kPreviewCacheBudgetBytes = 64ll * 1024 * 1024;
constexpr qint64 kThumbnailCacheBudgetBytes = 48ll * 1024 * 1024;

}  // namespace

AppController::AppController(
    SettingsStore &settings, RuntimeController &runtime, RequestProfileStore &requestProfiles, RequestProfileStore &checkRequestProfiles, VerificationPromptStore &verification, QObject *parent)
    : m_settings(settings), m_runtime(runtime), m_recognition(
                                                    settings,
                                                    runtime,
                                                    requestProfiles,
                                                    [this](int index, QString &error) { return pageImage(index, &error); },
                                                    nullptr,
                                                    [this](int index) {
                                                        QReadLocker locker(&m_documentLock);
                                                        return m_document.isValidIndex(index) && !m_document.page(index).sourceError.isEmpty();
                                                    }),
      m_verify({m_document, verification, checkRequestProfiles, runtime, [this](int pageIndex, int boxIndex) { return croppedImage(pageIndex, boxIndex); }, [this]() { return m_recognition.busy(); }},
               this),
      m_export({m_document, m_settings, [this](int pageIndex, int boxIndex) { return croppedImage(pageIndex, boxIndex); }, [this]() { return m_importing; }}, this), QObject(parent)
{
    connect(&m_recognition, &RecognitionController::busyChanged, this, [this]() {
        if (!m_recognition.busy() && !m_recognitionStopped && m_settings.autoCheck())
            QTimer::singleShot(0, this, [this]() { checkAllEnabledBlocks(true); });
        emit busyChanged();
    });
    connect(&m_recognition, &RecognitionController::statusRequested, this, [this](const StatusMessage &message) { setStatus(message); });
    connect(&m_recognition, &RecognitionController::rawResultReady, this, &AppController::applyRawResult);

    connect(&m_verify, &VerificationQueueController::stateChanged, this, &AppController::checkStateChanged);
    connect(&m_verify, &VerificationQueueController::statusRequested, this, [this](const StatusMessage &message) { setStatus(message); });
    connect(&m_verify, &VerificationQueueController::blockChecked, this, &AppController::applyCheckResultToBox);
    connect(&m_verify, &VerificationQueueController::problemReported, this, [this](const StatusMessage &message) { reportProblem(message, ProblemLog::Error); });

    connect(&m_export, &ExportController::exportingChanged, this, &AppController::exportingChanged);
    connect(&m_export, &ExportController::statusRequested, this, [this](const StatusMessage &message) { setStatus(message); });

    connect(this, &AppController::pageChanged, this, [this]() {
        ++m_imageRevision;
        emit imageRevisionChanged();
    });
    connect(this, &AppController::documentChanged, this, [this]() {
        ++m_docRevision;
        emit docRevisionChanged();
        if (m_importing)
            return;
        ++m_imageRevision;
        emit imageRevisionChanged();
    });

    connect(&m_settings, &SettingsStore::modelNameChanged, this, [this]() { emit configChanged(); });
    connect(&m_runtime, &RuntimeController::stateChanged, this, [this]() { emit configChanged(); });
    connect(&m_runtime, &RuntimeController::busyStateChanged, this, [this]() { emit configChanged(); });
    connect(&m_runtime, &RuntimeController::configValidChanged, this, [this]() { emit configChanged(); });
}

AppController::~AppController() = default;

QStringList AppController::parserNames() const
{
    return ParserFactory::selectableIds();
}

QStringList AppController::parserLabels() const
{
    return ParserFactory::selectableDisplayNames();
}

QString AppController::effectiveParserId() const
{
    const QString configured = m_settings.parserId();
    if (!configured.isEmpty() && configured != ParserFactory::kAutoId)
        return configured;
    return OcrModelFactory::create(m_settings.modelRecipeId())->defaultParserId();
}

ParserOptions AppController::parserOptions() const
{
    ParserOptions options;
    options.modelId = m_settings.modelRecipeId();
    options.keepPageNumbers = m_settings.keepPageNumbers();
    options.tablesAsHtml = m_settings.tablesAsHtml();
    return options;
}

std::unique_ptr<IOutputParser> AppController::makeParser() const
{
    return ParserFactory::create(effectiveParserId(), parserOptions());
}

QString AppController::rebuildPageText(const OcrPage &page) const
{
    const auto parser = makeParser();
    return parser ? parser->rebuildText(page) : page.text;
}

void AppController::setPageText(int index, const QString &text)
{
    if (!m_document.isValidIndex(index))
        return;
    OcrResult &result = m_document.page(index).result;
    if (!result.pages.isEmpty())
        result.pages[0].text = text;
    result.text = text;
}

QString AppController::pageText(int index) const
{
    return m_document.isValidIndex(index) ? m_document.page(index).result.text : QString();
}

QObject *AppController::ocrModels() const
{
    return const_cast<OcrModelListModel *>(&m_ocrModels);
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
    if (m_document.isEmpty() || m_recognition.busy() || m_importing || m_verify.checkBusy())
        return false;
    return m_runtime.canRecognize(true);
}

QString AppController::currentPageWarning() const
{
    QReadLocker locker(&m_documentLock);
    return m_document.isValidIndex(m_currentPage) ? m_document.page(m_currentPage).sourceError : QString();
}

QString AppController::parseWarning() const
{
    QReadLocker locker(&m_documentLock);
    return m_document.isValidIndex(m_currentPage) ? m_document.page(m_currentPage).parseNote : QString();
}

QString AppController::resultText() const
{
    return pageText(m_currentPage);
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

QImage AppController::pageThumbnail(int index)
{
    {
        QMutexLocker locker(&m_thumbnailMutex);
        const auto it = m_thumbnailCache.constFind(index);
        if (it != m_thumbnailCache.constEnd()) {
            m_thumbnailOrder.removeAll(index);
            m_thumbnailOrder.prepend(index);
            return it.value();
        }
    }

    DocumentModel::RenderRequest request;
    {
        QReadLocker locker(&m_documentLock);
        if (!m_document.isValidIndex(index))
            return {};
        request = m_document.thumbnailRequestFor(index);
    }

    QImage image = DocumentModel::renderDetached(request);
    if (image.isNull()) {
        QImage placeholder(DocumentModel::thumbnailSizeFor(request.page.pixelSize), QImage::Format_RGB32);
        if (placeholder.isNull())
            return {};
        placeholder.fill(Qt::white);
        image = placeholder;
        const QString key = request.page.sourcePath + QLatin1Char('#') + QString::number(index);
        QMetaObject::invokeMethod(
            this,
            [this, index, key]() {
                if (m_reportedUnrenderablePages.contains(key))
                    return;
                m_reportedUnrenderablePages.insert(key);
                reportProblem(StatusMessage::translate("AppController", "Page %1 could not be rendered and is shown blank — see the problem log.").arg(index + 1), ProblemLog::Warning);
            },
            Qt::QueuedConnection);
    }

    QMutexLocker locker(&m_thumbnailMutex);
    const auto existing = m_thumbnailCache.constFind(index);
    if (existing != m_thumbnailCache.constEnd())
        m_thumbnailBytes -= existing->sizeInBytes();
    m_thumbnailCache.insert(index, image);
    m_thumbnailBytes += image.sizeInBytes();
    m_thumbnailOrder.removeAll(index);
    m_thumbnailOrder.prepend(index);
    while (m_thumbnailOrder.size() > 1 && m_thumbnailBytes > kThumbnailCacheBudgetBytes) {
        const auto victim = m_thumbnailCache.find(m_thumbnailOrder.takeLast());
        if (victim == m_thumbnailCache.end())
            continue;
        m_thumbnailBytes -= victim->sizeInBytes();
        m_thumbnailCache.erase(victim);
    }
    return image;
}

bool AppController::previewRendering(int index) const
{
    return m_previewRendering == index;
}

void AppController::cachePreview(int index, const QImage &image)
{
    const auto existing = m_previewCache.constFind(index);
    if (existing != m_previewCache.constEnd())
        m_previewCacheBytes -= existing->sizeInBytes();
    m_previewCache.insert(index, image);
    m_previewCacheBytes += image.sizeInBytes();
    m_previewCacheOrder.removeAll(index);
    m_previewCacheOrder.prepend(index);
    evictPreviewCache();
}

void AppController::evictPreviewCache()
{
    while (m_previewCacheOrder.size() > 1 && m_previewCacheBytes > kPreviewCacheBudgetBytes) {
        const auto it = m_previewCache.find(m_previewCacheOrder.takeLast());
        if (it == m_previewCache.end())
            continue;
        m_previewCacheBytes -= it->sizeInBytes();
        m_previewCache.erase(it);
    }
}

void AppController::clearPreviewCache()
{
    m_previewCache.clear();
    m_previewCacheOrder.clear();
    m_previewCacheBytes = 0;
}

QImage AppController::previewImage(int index)
{
    DocumentModel::RenderRequest request;
    bool cached = false;
    {
        QReadLocker locker(&m_documentLock);
        if (!m_document.isValidIndex(index))
            return {};
        const auto it = m_previewCache.constFind(index);
        if (it != m_previewCache.constEnd()) {
            cached = true;
        } else {
            request = m_document.renderRequestFor(index);
        }
    }
    if (cached) {
        m_previewCacheOrder.removeAll(index);
        m_previewCacheOrder.prepend(index);
        return m_previewCache.value(index);
    }

    if (m_previewRendering == index)
        return {};
    m_previewRendering = index;
    const quint64 generation = m_previewGeneration;

    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, index, generation]() {
        const QImage rendered = watcher->result();
        watcher->deleteLater();
        if (generation != m_previewGeneration)
            return;
        if (m_previewRendering == index)
            m_previewRendering = -1;
        if (rendered.isNull())
            return;
        cachePreview(index, rendered);
        emit pageImageReady(index);
        ++m_imageRevision;
        emit imageRevisionChanged();
    });
    watcher->setFuture(QtConcurrent::run([request]() { return DocumentModel::renderDetached(request); }));
    return {};
}

QImage AppController::croppedImage(int pageIndex, int boxIndex)
{
    QWriteLocker locker(&m_documentLock);
    if (!m_document.isValidIndex(pageIndex))
        return {};
    const DocumentPage &page = m_document.page(pageIndex);
    if (!page.recognized || page.result.pages.isEmpty())
        return {};
    const QList<BoundingBox> &boxes = page.result.pages[0].boxes;
    if (boxIndex < 0 || boxIndex >= boxes.size())
        return {};

    const QRectF norm = boxes.at(boxIndex).rect;
    if (norm.width() <= 0.0 || norm.height() <= 0.0)
        return {};

    m_document.fullImage(pageIndex);
    const QImage &img = m_document.page(pageIndex).image;
    if (img.isNull())
        return {};

    QRect px(qRound(norm.x() * img.width()), qRound(norm.y() * img.height()), qRound(norm.width() * img.width()), qRound(norm.height() * img.height()));
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
    bool cancelled = false;
    QString firstError;
    QStringList warnings;
};

void AppController::openFiles(const QVariantList &fileUrls)
{
    if (m_recognition.busy() || m_importing || m_export.exporting())
        return;

    QStringList paths;
    for (const QVariant &variant : fileUrls) {
        const QUrl url = variant.toUrl();
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (!path.isEmpty())
            paths.append(path);
    }

    if (paths.isEmpty()) {
        setStatus(StatusMessage::translate("AppController", "No files selected."));
        return;
    }

    auto state = std::make_shared<ImportState>();
    state->paths = std::move(paths);
    m_import = state;
    m_importDone = 0;
    m_importTotal = state->paths.size();
    m_importing = true;
    emit importingChanged();
    emit importProgressChanged();
    emit configChanged();
    importNextFile(state);
}

void AppController::cancelImport()
{
    if (m_import)
        m_import->cancelled = true;
}

void AppController::importNextFile(const std::shared_ptr<ImportState> &state)
{
    if (state->cancelled || state->next >= state->paths.size()) {
        finishImport(*state);
        return;
    }
    const QString path = state->paths.at(state->next++);
    setStatus(StatusMessage::translate("AppController", "Importing %1 (%2/%3)…").arg(QFileInfo(path).fileName()).arg(state->next).arg(state->paths.size()));
    const int pagesBefore = m_document.pageCount();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("djvu") || suffix == QStringLiteral("djv")) {
        auto *watcher = new QFutureWatcher<DocumentModel::PreparedDjVu>(this);
        connect(watcher, &QFutureWatcher<DocumentModel::PreparedDjVu>::finished, this, [this, watcher, state, pagesBefore]() {
            const auto prepared = watcher->result();
            watcher->deleteLater();
            {
                QWriteLocker locker(&m_documentLock);
                m_document.appendPreparedDjVu(prepared);
            }
            state->warnings.append(prepared.warnings);
            recordImportedFile(state, pagesBefore, prepared.error);
        });
        watcher->setFuture(QtConcurrent::run([path]() { return DocumentModel::prepareDjVu(path); }));
        return;
    }
    QString error;
    {
        QWriteLocker locker(&m_documentLock);
        m_document.appendFile(path, &error);
    }
    recordImportedFile(state, pagesBefore, error);
}

void AppController::recordImportedFile(const std::shared_ptr<ImportState> &state, int pagesBefore, const QString &error)
{
    const int added = m_document.pageCount() - pagesBefore;
    if (added > 0) {
        ++state->addedFiles;
        state->addedPages += added;
        if (pagesBefore == 0)
            m_currentPage = 0;
        m_pageModel.appendPages(added);
        m_pageModel.setCurrent(m_currentPage);
        notifyPageListGrown();
    } else {
        ++state->skipped;
        if (state->firstError.isEmpty())
            state->firstError = error;
    }
    ++m_importDone;
    emit importProgressChanged();
    QTimer::singleShot(0, this, [this, state]() { importNextFile(state); });
}

void AppController::finishImport(const ImportState &state)
{
    StatusMessage summary;
    if (state.addedPages == 0) {
        summary = state.firstError.isEmpty() ? StatusMessage::translate("AppController", "None of the selected files could be added.") : StatusMessage::literal(state.firstError);
    } else if (state.skipped > 0) {
        summary = StatusMessage::translate("AppController", "Added %1 file(s), %2 page(s); %3 file(s) skipped.").arg(state.addedFiles).arg(state.addedPages).arg(state.skipped);
    } else {
        summary = StatusMessage::translate("AppController", "Added %1 file(s), %2 page(s).").arg(state.addedFiles).arg(state.addedPages);
    }
    if (state.cancelled && state.addedPages > 0) {
        summary = StatusMessage::translate("AppController", "Import stopped: %1 of %2 file(s) opened, %3 page(s).").arg(state.addedFiles).arg(m_importTotal).arg(state.addedPages);
    }
    for (const QString &warning : state.warnings)
        reportProblem(StatusMessage::literal(warning));
    if (state.skipped > 0 && !state.firstError.isEmpty())
        reportProblem(StatusMessage::literal(state.firstError), ProblemLog::Error);

    if (!state.warnings.isEmpty()) {
        setStatus(StatusMessage::join({summary,
                                       StatusMessage::translate("AppController",
                                                                "Warning: %1 page(s) replaced with blank pages — "
                                                                "see the problem log.")
                                           .arg(state.warnings.size())}));
    } else {
        setStatus(summary);
    }
    m_importing = false;
    m_import = nullptr;
    m_importDone = m_importTotal;
    emit importingChanged();
    emit importProgressChanged();
    emit configChanged();
    if (state.addedPages > 0) {
        updateBoxesForCurrent();
        notifyImportFinished();
    }
}

bool AppController::reportProblem(const StatusMessage &message, ProblemLog::Severity severity)
{
    if (!m_problems || message.isEmpty())
        return false;
    m_problems->report(message, severity);
    return true;
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

        setStatus(StatusMessage::translate("AppController", "Page %1 deleted.").arg(index + 1));

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

    setStatus(StatusMessage::translate("AppController", "Page %1 deleted.").arg(index + 1));

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

    setStatus(StatusMessage::translate("AppController", "Moved page %1 to position %2.").arg(from + 1).arg(to + 1));

    notifyDocumentChanged();
    return true;
}

void AppController::recognizeCurrent()
{
    if (m_recognition.busy() || m_importing || m_document.isEmpty())
        return;
    if (!canRecognize()) {
        setStatus(StatusMessage::translate("AppController", "Set a model name in Settings first."));
        return;
    }

    m_recognition.startCurrent(m_currentPage, m_document.pageCount());
    m_recognitionStopped = false;
}

void AppController::recognizeAll()
{
    if (m_recognition.busy() || m_importing || m_document.isEmpty())
        return;
    if (!canRecognize()) {
        setStatus(StatusMessage::translate("AppController", "Set a model name in Settings first."));
        return;
    }

    m_recognition.startAll(m_document.pageCount());
    m_recognitionStopped = false;
}

void AppController::applyRawResult(int index, const OcrResult &rawResult)
{
    if (!m_document.isValidIndex(index))
        return;

    OcrResult parsed = rawResult;
    if (auto parser = makeParser())
        parsed = parser->parse(rawResult.text);

    DocumentPage &page = m_document.page(index);
    page.result = parsed;
    page.recognized = true;
    page.parseNote = parsed.success && !parsed.notes.isEmpty() ? parsed.notes.join(QLatin1String(" ")) : QString();

    m_pageModel.setRecognized(index, true);

    const bool hadDups = !parsed.pages.isEmpty() && parsed.pages.first().hasDuplicates;
    if (hadDups)
        m_pageModel.setHasDuplicates(index, true);

    const bool droppedEdit = m_editStore.reset(index, parsed.text);
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
    m_recognitionStopped = true;
    m_recognition.stop();
}

void AppController::setCurrentPageText(const QString &text)
{
    if (!currentPageEditable())
        return;

    const int index = m_currentPage;
    if (text == pageText(index))
        return;

    setPageText(index, text);
    if (!m_editStore.isEdited(index)) {
        m_editStore.setEdited(index, true);
        m_pageModel.setEdited(index, true);
    }
    emit resultChanged();
    emit editStateChanged();
}

void AppController::revertCurrentPageEdits()
{
    const int index = m_currentPage;
    if (!m_editStore.isEdited(index))
        return;
    const QString original = m_editStore.baseline(index);
    m_editStore.revert(index);
    setPageText(index, original);
    m_pageModel.setEdited(index, false);
    emit resultChanged();
    emit editStateChanged();
}

void AppController::onBoxRectChanged(int boxIndex, qreal x, qreal y, qreal width, qreal height)
{
    if (!m_document.isValidIndex(m_currentPage))
        return;
    DocumentPage &page = m_document.page(m_currentPage);
    if (!page.recognized || page.result.pages.isEmpty())
        return;
    QList<BoundingBox> &boxes = page.result.pages[0].boxes;
    if (boxIndex < 0 || boxIndex >= boxes.size())
        return;
    boxes[boxIndex].rect = QRectF(x, y, width, height);
    ++m_cropRevision;

    m_boxModel.updateBoxRect(boxIndex, x, y, width, height);

    emit boxesChanged();
    emit imageChanged();
}

bool AppController::removeBlock(int boxIndex)
{
    if (m_recognition.busy() || m_importing)
        return false;
    if (!m_document.isValidIndex(m_currentPage))
        return false;
    DocumentPage &page = m_document.page(m_currentPage);
    if (!page.recognized || page.result.pages.isEmpty())
        return false;
    QList<BoundingBox> &boxes = page.result.pages[0].boxes;
    if (boxIndex < 0 || boxIndex >= boxes.size())
        return false;

    boxes.removeAt(boxIndex);
    ++m_cropRevision;
    m_boxModel.removeBox(boxIndex);

    setPageText(m_currentPage, rebuildPageText(page.result.pages[0]));
    if (!m_editStore.isEdited(m_currentPage)) {
        m_editStore.setEdited(m_currentPage, true);
        m_pageModel.setEdited(m_currentPage, true);
    }

    if (m_selectedBox == boxIndex)
        setSelectedBoxIndex(-1);
    else if (m_selectedBox > boxIndex)
        setSelectedBoxIndex(m_selectedBox - 1);

    emit boxesChanged();
    emit resultChanged();
    emit editStateChanged();
    return true;
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

int AppController::selectedBlockCheckStatus() const
{
    const BoundingBox *box = selectedBox();
    return box ? static_cast<int>(box->checkStatus) : 0;
}

QString AppController::selectedBlockCorrected() const
{
    const BoundingBox *box = selectedBox();
    return box ? box->correctedText : QString();
}

void AppController::setSelectedBoxIndex(int index)
{
    int clamped = index;
    if (!m_document.isValidIndex(m_currentPage) || !m_document.page(m_currentPage).recognized) {
        clamped = -1;
    } else if (index != -1) {
        const int size = m_document.page(m_currentPage).result.pages.isEmpty() ? 0 : m_document.page(m_currentPage).result.pages[0].boxes.size();
        if (index < 0 || index >= size)
            clamped = -1;
    }
    if (m_selectedBox == clamped)
        return;
    m_selectedBox = clamped;
    emit selectedBoxChanged();
}

void AppController::checkSelectedBlock()
{
    if (m_selectedBox < 0)
        return;
    m_verify.checkBlock(m_currentPage, m_selectedBox);
}

void AppController::revertBlockCorrection()
{
    if (m_selectedBox < 0 || !m_document.isValidIndex(m_currentPage))
        return;

    bool textChanged = false;
    {
        QWriteLocker locker(&m_documentLock);
        DocumentPage &page = m_document.page(m_currentPage);
        if (!page.recognized || page.result.pages.isEmpty())
            return;
        QList<BoundingBox> &boxes = page.result.pages[0].boxes;
        if (m_selectedBox >= boxes.size())
            return;

        BoundingBox &box = boxes[m_selectedBox];
        if (box.checkStatus == BoxCheckStatus::NotChecked && box.correctedText.isEmpty()) {
            return;
        }
        box.checkStatus = BoxCheckStatus::NotChecked;
        box.correctedText.clear();

        const QString rebuilt = rebuildPageText(page.result.pages[0]);
        if (rebuilt == m_editStore.baseline(m_currentPage)) {
            setPageText(m_currentPage, rebuilt);
            m_editStore.revert(m_currentPage);
            m_pageModel.setEdited(m_currentPage, false);
        } else {
            setPageText(m_currentPage, rebuilt);
            m_editStore.setEdited(m_currentPage, true);
            m_pageModel.setEdited(m_currentPage, true);
        }
        ++m_cropRevision;
        textChanged = true;
    }

    m_boxModel.updateBoxCheck(m_selectedBox, static_cast<int>(BoxCheckStatus::NotChecked), QString());
    emit selectedBoxChanged();
    emit checkStateChanged();
    if (textChanged) {
        emit resultChanged();
        emit editStateChanged();
    }
    emit boxesChanged();
}

void AppController::checkEnabledBlocksOnPage()
{
    m_verify.checkPageEnabledBlocks(m_currentPage);
}

void AppController::checkAllEnabledBlocks(bool onlyUnchecked)
{
    m_verify.checkAllEnabledBlocks(onlyUnchecked);
}

void AppController::stopCheck()
{
    m_verify.stop();
}

bool AppController::pageVerificationSupported() const
{
    return m_verify.pageVerificationSupported(m_currentPage);
}

bool AppController::allPageVerificationSupported() const
{
    return m_verify.allPageVerificationSupported();
}

void AppController::applyCheckResultToBox(int pageIndex, int boxIndex, const CheckResult &result)
{
    if (!m_document.isValidIndex(pageIndex))
        return;

    BoxCheckStatus status = BoxCheckStatus::NotChecked;
    QString corrected;
    bool textChanged = false;
    {
        QWriteLocker locker(&m_documentLock);
        DocumentPage &page = m_document.page(pageIndex);
        if (!page.recognized || page.result.pages.isEmpty())
            return;
        QList<BoundingBox> &boxes = page.result.pages[0].boxes;
        if (boxIndex < 0 || boxIndex >= boxes.size())
            return;

        BoundingBox &box = boxes[boxIndex];
        switch (result.status) {
        case CheckStatus::Ok:
            box.checkStatus = BoxCheckStatus::Ok;
            box.correctedText.clear();
            break;
        case CheckStatus::Fixed:
            box.checkStatus = BoxCheckStatus::Fixed;
            box.correctedText = result.text;
            break;
        case CheckStatus::Review:
            box.checkStatus = BoxCheckStatus::Review;
            box.correctedText.clear();
            break;
        case CheckStatus::Failed:
            break;
        }
        status = box.checkStatus;
        corrected = box.correctedText;

        if (box.checkStatus == BoxCheckStatus::Fixed) {
            setPageText(pageIndex, rebuildPageText(page.result.pages[0]));
            if (!m_editStore.isEdited(pageIndex)) {
                m_editStore.setEdited(pageIndex, true);
                m_pageModel.setEdited(pageIndex, true);
            }
            textChanged = true;
        }
        ++m_cropRevision;
    }

    const bool onCurrentPage = pageIndex == m_currentPage;
    if (onCurrentPage)
        m_boxModel.updateBoxCheck(boxIndex, static_cast<int>(status), corrected);
    if (onCurrentPage && m_selectedBox == boxIndex)
        emit selectedBoxChanged();
    if (textChanged) {
        emit resultChanged();
        emit editStateChanged();
    }
    if (onCurrentPage)
        emit boxesChanged();
}

bool AppController::exportPages(const QUrl &fileUrl, int scope, int fromPage, int toPage)
{
    return m_export.exportPages(fileUrl, scope, m_currentPage, fromPage, toPage);
}

QStringList AppController::exportNameFilters() const
{
    QStringList filters;
    filters << StatusMessage::translate("AppController", "Markdown (*.md)").text() << StatusMessage::translate("AppController", "Plain text (*.txt)").text()
            << StatusMessage::translate("AppController", "HTML (*.html)").text();
    if (Exporter::isPandocAvailable())
        filters << StatusMessage::translate("AppController", "Word document (*.docx)").text();
    filters << StatusMessage::translate("AppController", "PDF (*.pdf)").text();
    return filters;
}

void AppController::setStatus(const StatusMessage &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusChanged();
}

void AppController::retranslate()
{
    emit statusChanged();
    emit retranslateRequested();
}

void AppController::notifyPageListGrown()
{
    emit documentChanged();
}

void AppController::notifyImportFinished()
{
    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
    emit boxesChanged();
    emit editStateChanged();
}

void AppController::notifyDocumentChanged()
{
    ++m_previewGeneration;
    clearPreviewCache();
    m_previewRendering = -1;
    {
        QMutexLocker locker(&m_thumbnailMutex);
        m_thumbnailCache.clear();
        m_thumbnailOrder.clear();
        m_thumbnailBytes = 0;
    }
    m_reportedUnrenderablePages.clear();
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

QString AppController::resolveImagesForPreview(const QString &markdown)
{
    if (m_previewCacheRevision == m_imageRevision && m_previewCacheCropRevision == m_cropRevision && m_previewCacheText == markdown)
        return m_previewCacheResult;

    static const QRegularExpression re(QStringLiteral(R"(!\[([^\]]*)\]\(image://ocr/crop/(\d+)\))"));

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
                result += QStringLiteral("![%1](data:image/png;base64,%2)").arg(m.captured(1), QString::fromLatin1(bytes.toBase64()));
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
