#include "app/AppController.h"
#include "app/BlockTextHighlighter.h"
#include "app/LruImageCache.h"
#include "app/PageIndex.h"

#include <algorithm>
#include <utility>

#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHash>
#include <QMarginsF>
#include <QMutex>
#include <QPageLayout>
#include <QPageSize>
#include <QQuickTextDocument>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QStringView>
#include <QtConcurrent/QtConcurrentRun>
#include <QTimer>
#include <QVariant>

#include "parsers/BlockStyle.h"
#include "parsers/ParserFactory.h"

#include "config/RequestProfileStore.h"
#include "core/BoxTransitions.h"
#include "core/ModelProfiles.h"
#include "core/StatusMessage.h"
#include "models/OcrModel.h"

namespace llocr {

AppController::AppController(SettingsStore &settings,
                             RuntimeController &runtime,
                             RequestProfileStore &requestProfiles,
                             RequestProfileStore &rawPageOcrRequestProfiles,
                             RequestProfileStore &checkRequestProfiles,
                             RequestProfileStore &layoutRequestProfiles,
                             RequestProfileStore &fixRequestProfiles,
                             VerificationPromptStore &verification,
                             QObject *parent)
    : m_settings(settings), m_runtime(runtime), m_verification(verification), m_checkRequestProfiles(checkRequestProfiles), m_layoutRequestProfiles(layoutRequestProfiles),
      m_fixRequestProfiles(fixRequestProfiles), m_recognition(
                                                    settings,
                                                    runtime,
                                                    requestProfiles,
                                                    rawPageOcrRequestProfiles,
                                                    [this](int index, QString &error) { return pageImage(index, &error); },
                                                    nullptr,
                                                    [this](int index, bool batch) {
                                                        QReadLocker locker(&m_documentLock);
                                                        if (!m_document.isValidIndex(index))
                                                            return PageSkip::Unreadable;
                                                        const DocumentPage &page = m_document.page(index);
                                                        if (!page.sourceError.isEmpty())
                                                            return PageSkip::Unreadable;
                                                        if (batch && page.recognized)
                                                            return PageSkip::AlreadyRecognized;
                                                        return PageSkip::None;
                                                    }),
      m_verify({m_document,
                verification,
                checkRequestProfiles,
                fixRequestProfiles,
                settings,
                runtime,
                [this](int pageIndex, int boxIndex) { return croppedImage(pageIndex, boxIndex); },
                [this]() { return m_recognition.busy(); }},
               this),
      m_layout(layoutRequestProfiles, runtime, this),
      m_export({m_document, m_settings, [this](int pageIndex, int boxIndex) { return croppedImage(pageIndex, boxIndex); }, [this]() { return m_importing; }}, this), QObject(parent)
{
    connect(&m_recognition, &RecognitionController::busyChanged, this, [this]() {
        if (!m_recognition.busy() && !m_recognitionStopped && m_settings.autoCheck())
            QTimer::singleShot(0, this, [this]() { m_verify.checkAllEnabledBlocks(); });
        emit busyChanged();
        updateAvailability();
    });
    connect(&m_recognition, &RecognitionController::statusRequested, this, [this](const StatusMessage &message) { setStatus(message); });
    connect(&m_recognition, &RecognitionController::rawResultReady, this, &AppController::applyRawResult);

    // The verification filter changes which blocks «Проверить» would pick up
    // and which pages count as verified, so the availability flags and every
    // page's aggregate follow it live: the model signal covers edits in the
    // settings window, the store's modelChanged covers load/reset.
    const auto verificationFilterChanged = [this]() {
        refreshAllPageVerifications();
        updateAvailability();
    };
    connect(qobject_cast<VerificationBlocksModel *>(verification.blockModel()), &VerificationBlocksModel::countsChanged, this, verificationFilterChanged);
    connect(&verification, &VerificationPromptStore::modelChanged, this, verificationFilterChanged);

    connect(&m_verify, &VerificationQueueController::stateChanged, this, [this]() {
        emit checkStateChanged();
        updateAvailability();
    });
    connect(&m_verify, &VerificationQueueController::statusRequested, this, [this](const StatusMessage &message) { setStatus(message); });
    connect(&m_verify, &VerificationQueueController::verificationStarted, this, &AppController::applyVerificationStarted);
    connect(&m_verify, &VerificationQueueController::blockVerified, this, &AppController::applyVerdictToBox);
    connect(&m_verify, &VerificationQueueController::blockRecognized, this, &AppController::applyRecognitionToBox);
    connect(&m_verify, &VerificationQueueController::blockFixed, this, &AppController::applyFixToBox);
    connect(&m_verify, &VerificationQueueController::problemReported, this, [this](const StatusMessage &message) { reportProblem(message, ProblemLog::Error); });

    connect(this, &AppController::pageChanged, this, &AppController::updateAvailability);
    connect(this, &AppController::documentChanged, this, &AppController::updateAvailability);
    connect(this, &AppController::resultChanged, this, &AppController::updateAvailability);
    connect(this, &AppController::configChanged, this, &AppController::updateAvailability);
    connect(this, &AppController::importingChanged, this, &AppController::updateAvailability);
    connect(&m_pageModel, &PageListModel::selectionChanged, this, &AppController::updateAvailability);

    connect(&m_layout, &LayoutController::busyChanged, this, [this]() {
        emit layoutStateChanged();
        updateAvailability();
    });
    connect(&m_layout, &LayoutController::statusRequested, this, [this](const StatusMessage &message) { setStatus(message); });
    connect(&m_layout, &LayoutController::progressChanged, this, &AppController::layoutProgressChanged);
    connect(&m_layout, &LayoutController::layoutFinished, this, &AppController::applyLayout);
    connect(&m_settings, &SettingsStore::layoutRequestProfileIdChanged, this, &AppController::layoutAvailableChanged);

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
    connect(&m_runtime, &RuntimeController::taskModelsChanged, this, &AppController::updateAvailability);
    connect(this, &AppController::resultChanged, this, &AppController::blockTextRangeChanged);
    connect(this, &AppController::editStateChanged, this, &AppController::blockTextRangeChanged);
    connect(this, &AppController::selectedBoxChanged, this, &AppController::blockTextRangeChanged);
    // The parser options shape the block-to-text mapping; the memoized rebuild
    // has no revision to key on for them.
    connect(&m_settings, &SettingsStore::parserIdChanged, this, &AppController::invalidateRebuildCache);
    connect(&m_settings, &SettingsStore::modelRecipeIdChanged, this, &AppController::invalidateRebuildCache);
    connect(&m_settings, &SettingsStore::keepPageNumbersChanged, this, &AppController::invalidateRebuildCache);
    connect(&m_settings, &SettingsStore::tablesAsHtmlChanged, this, &AppController::invalidateRebuildCache);
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
    return effectiveParserIdFor(m_settings.modelRecipeId());
}

QString AppController::effectiveParserIdFor(const QString &profileId) const
{
    const QString configured = m_settings.parserId();
    if (!configured.isEmpty() && configured != ParserFactory::kAutoId)
        return configured;
    return OcrModel::create(profileId)->defaultParserId();
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

IOutputParser::RebuiltPageText AppController::currentPageRebuild() const
{
    if (!m_document.isValidIndex(m_currentPage))
        return {};
    const DocumentPage &page = m_document.page(m_currentPage);
    if (page.result.pages.isEmpty())
        return {};
    const QString shown = pageText(m_currentPage);
    if (m_rebuildCachePage == m_currentPage && m_rebuildCacheDocRevision == m_docRevision && m_rebuildCacheCropRevision == m_cropRevision && m_rebuildCacheText == shown)
        return m_rebuildCache;

    const auto parser = makeParser();
    if (!parser)
        return {};
    IOutputParser::RebuiltPageText rebuild = parser->rebuildTextWithRanges(page.result.pages.first());

    if (!rebuild.ranges.isEmpty() && rebuild.text != shown) {
        QList<BlockTextRange> reanchored;
        int searchFrom = 0;
        for (const BlockTextRange &range : rebuild.ranges) {
            const QString blockText = rebuild.text.mid(range.start, range.length);
            if (blockText.isEmpty())
                continue;
            const int pos = shown.indexOf(blockText, searchFrom);
            if (pos < 0)
                continue;
            reanchored.append({range.boxIndex, pos, range.length});
            searchFrom = pos + range.length;
        }
        rebuild.text = shown;
        rebuild.ranges = reanchored;
    }

    m_rebuildCachePage = m_currentPage;
    m_rebuildCacheDocRevision = m_docRevision;
    m_rebuildCacheCropRevision = m_cropRevision;
    m_rebuildCacheText = shown;
    m_rebuildCache = rebuild;
    return rebuild;
}

bool AppController::blockTextMapped() const
{
    return !currentPageRebuild().ranges.isEmpty();
}

QList<int> AppController::blockTextRange(int boxIndex) const
{
    const auto rebuild = currentPageRebuild();
    for (const BlockTextRange &range : rebuild.ranges) {
        if (range.boxIndex == boxIndex)
            return {range.start, range.length};
    }
    return {};
}

int AppController::boxIndexForTextPosition(int position) const
{
    const auto rebuild = currentPageRebuild();
    if (rebuild.ranges.isEmpty())
        return -1;
    for (const BlockTextRange &range : rebuild.ranges) {
        if (position >= range.start && position < range.start + range.length)
            return range.boxIndex;
    }
    if (position == rebuild.text.size())
        return rebuild.ranges.last().boxIndex;
    return -1;
}

void AppController::attachBlockTextHighlighter(QObject *textDocument, const QColor &color)
{
    QTextDocument *document = nullptr;
    if (auto *wrapper = qobject_cast<QQuickTextDocument *>(textDocument))
        document = wrapper->textDocument();
    else if (auto *plain = qobject_cast<QTextDocument *>(textDocument))
        document = plain;
    if (!document)
        return;
    if (!m_blockHighlighter) {
        m_blockHighlighter = new BlockTextHighlighter(this);
        connect(this, &AppController::blockTextRangeChanged, this, &AppController::updateBlockHighlight);
    }
    m_blockHighlighter->setColor(color);
    m_blockHighlighter->setDocument(document);
    updateBlockHighlight();
}

void AppController::invalidateRebuildCache()
{
    if (m_rebuildCachePage == -1)
        return;  // nothing memoized — the mapping cannot have changed
    m_rebuildCachePage = -1;
    m_rebuildCacheDocRevision = -1;
    m_rebuildCacheCropRevision = -1;
    m_rebuildCacheText.clear();
    m_rebuildCache = {};
    // The ranges the QML property and the highlighter see come from the memo;
    // without this announcement they would wait for the next edit.
    emit blockTextRangeChanged();
}

void AppController::updateBlockHighlight()
{
    if (!m_blockHighlighter)
        return;
    if (m_selectedBox < 0) {
        // No selected block — the highlight is off; do not pay a page rebuild
        // for it on every keystroke.
        m_blockHighlighter->setStart(-1);
        m_blockHighlighter->setLength(0);
        return;
    }
    const QList<int> range = blockTextRange(m_selectedBox);
    m_blockHighlighter->setStart(range.isEmpty() ? -1 : range.at(0));
    m_blockHighlighter->setLength(range.isEmpty() ? 0 : range.at(1));
}

void AppController::setPageText(int index, const QString &text)
{
    if (!m_document.isValidIndex(index))
        return;
    QWriteLocker locker(&m_documentLock);
    OcrResult &result = m_document.page(index).result;
    if (!result.pages.isEmpty())
        result.pages[0].text = text;
    result.text = text;
}

QString AppController::pageText(int index) const
{
    return m_document.isValidIndex(index) ? m_document.page(index).result.text : QString();
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
    if (m_document.isEmpty() || m_recognition.busy() || m_layout.busy() || m_importing || m_verify.checkBusy())
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

QImage AppController::pageImage(int index, QString *error)
{
    QWriteLocker locker(&m_documentLock);
    return m_document.fullImage(index, error);
}

QImage AppController::pageThumbnail(int index)
{
    {
        QMutexLocker locker(&m_thumbnailMutex);
        if (const QImage *hit = m_thumbnailCache.find(index)) {
            m_thumbnailCache.touch(index);
            return *hit;
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
    m_thumbnailCache.insert(index, image);
    return image;
}

bool AppController::previewRendering(int index) const
{
    return m_previewRendering.load() == index;
}

QImage AppController::previewImage(int index)
{
    {
        QMutexLocker locker(&m_previewMutex);
        if (const QImage *hit = m_previewCache.find(index)) {
            m_previewCache.touch(index);
            return *hit;
        }
    }

    DocumentModel::RenderRequest request;
    {
        QReadLocker locker(&m_documentLock);
        if (!m_document.isValidIndex(index))
            return {};
        request = m_document.renderRequestFor(index);
    }

    if (m_previewRendering.load() == index)
        return {};
    m_previewRendering.store(index);
    emit previewRenderingChanged();
    const quint64 generation = m_previewGeneration.load();

    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, index, generation]() {
        const QImage rendered = watcher->result();
        watcher->deleteLater();
        if (generation != m_previewGeneration.load())
            return;
        if (m_previewRendering.load() == index) {
            m_previewRendering.store(-1);
            emit previewRenderingChanged();
        }
        if (rendered.isNull())
            return;
        {
            QMutexLocker locker(&m_previewMutex);
            m_previewCache.insert(index, rendered);
        }
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
        if (pagesBefore == 0)
            ensureDefaultSelection();
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
        notifyPageChanged();
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
    if (m_recognition.busy() || m_importing || checkBusy())
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
    ensureDefaultSelection();

    updateBoxesForCurrent();

    setStatus(StatusMessage::translate("AppController", "Page %1 deleted.").arg(index + 1));

    notifyDocumentChanged();
    return true;
}

bool AppController::movePage(int from, int to)
{
    if (m_recognition.busy() || m_importing || checkBusy())
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

void AppController::recognizeCurrentPage(const QString &task)
{
    const ConnectionRole role = connectionRoleFromName(task);
    if (m_recognition.busy() || m_layout.busy() || m_importing || checkBusy() || m_document.isEmpty())
        return;
    if (!canRecognize()) {
        setStatus(StatusMessage::translate("AppController", "Set a model name in Settings first."));
        return;
    }

    m_recognition.startCurrent(m_currentPage, m_document.pageCount(), role);
    m_recognitionStopped = false;
}

void AppController::recognizeAllPages(const QString &task)
{
    const ConnectionRole role = connectionRoleFromName(task);
    if (m_recognition.busy() || m_layout.busy() || m_importing || checkBusy() || m_document.isEmpty())
        return;
    if (!canRecognize()) {
        setStatus(StatusMessage::translate("AppController", "Set a model name in Settings first."));
        return;
    }

    m_recognition.startAll(m_document.pageCount(), role);
    m_recognitionStopped = false;
}

void AppController::recognizeCurrent()
{
    recognizeCurrentPage(connectionRoleName(ConnectionRole::PageOcr));
}

void AppController::recognizeAll()
{
    recognizeAllPages(connectionRoleName(ConnectionRole::PageOcr));
}

void AppController::applyRawResult(int index, ConnectionRole task, const OcrResult &rawResult)
{
    if (!m_document.isValidIndex(index))
        return;

    ParserOptions options = parserOptions();
    if (task == ConnectionRole::RawPageOcr)
        options.modelId = m_settings.requestProfileIdForRole(connectionRoleName(task));
    const QSize pixelSize = m_document.page(index).pixelSize;
    if (pixelSize.isValid() && pixelSize.width() > 0)
        options.pageAspect = qreal(pixelSize.height()) / pixelSize.width();

    OcrResult parsed = rawResult;
    if (auto parser = ParserFactory::create(effectiveParserIdFor(options.modelId), options))
        parsed = parser->parse(rawResult.text);
    m_boxModel.setStyleModelId(options.modelId);

    {
        QWriteLocker locker(&m_documentLock);
        DocumentPage &page = m_document.page(index);
        page.result = parsed;
        page.recognized = true;
        page.parseNote = parsed.success && !parsed.notes.isEmpty() ? parsed.notes.join(QLatin1String(" ")) : QString();
    }

    m_pageModel.setRecognized(index, true);
    setRecognitionMode(index, task == ConnectionRole::RawPageOcr ? PageRecognitionMode::RawOcr : PageRecognitionMode::MarkupOcr);
    refreshPageVerification(index);

    const bool hadDups = !parsed.pages.isEmpty() && parsed.pages.first().hasDuplicates;
    if (hadDups)
        m_pageModel.setHasDuplicates(index, true);

    const bool droppedEdit = m_editStore.reset(index, parsed.text);
    if (droppedEdit)
        m_pageModel.setEdited(index, false);

    if (index == m_currentPage) {
        updateBoxesForCurrent();
        emit resultChanged();
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

    if (!text.isEmpty()) {
        for (int i = 0; i < m_document.pageCount(); ++i) {
            if (i != index && m_document.page(i).recognized && pageText(i) == text) {
                qWarning().noquote() << "Panel text matches page" << i + 1 << "while saving page" << index + 1 << "— write refused";
                emit resultChanged();  // snap the panel back to the stored text
                return;
            }
        }
    }

    // First edit on this page remembers the pre-edit text for Revert.
    if (!m_editStore.isEdited(index))
        m_editStore.reset(index, pageText(index));
    setPageText(index, text);
    markPageEdited(index);
    emit resultChanged();
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
    {
        QWriteLocker locker(&m_documentLock);
        if (!m_document.isValidIndex(m_currentPage))
            return;
        DocumentPage &page = m_document.page(m_currentPage);
        if (!page.recognized || page.result.pages.isEmpty())
            return;
        QList<BoundingBox> &boxes = page.result.pages[0].boxes;
        if (boxIndex < 0 || boxIndex >= boxes.size())
            return;
        boxes[boxIndex].rect = QRectF(x, y, width, height);
    }
    ++m_cropRevision;

    m_boxModel.updateBoxRect(boxIndex, x, y, width, height);

    emit imageChanged();
}

bool AppController::removeBlock(int boxIndex)
{
    if (m_recognition.busy() || m_layout.busy() || m_importing || checkBusy())
        return false;
    {
        QWriteLocker locker(&m_documentLock);
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
    }
    m_boxModel.removeBox(boxIndex);
    // The removed block could have been the one holding the page back: both
    // the aggregate and the markup-only completion are derived from the set.
    refreshRecognitionModeAfterBlockText(m_currentPage);
    refreshPageVerification(m_currentPage);

    const QString rebuilt = rebuildPageText(m_document.page(m_currentPage).result.pages[0]);
    setPageText(m_currentPage, rebuilt);
    m_editStore.reset(m_currentPage, rebuilt);
    markPageEdited(m_currentPage);

    if (m_selectedBox == boxIndex)
        setSelectedBoxIndex(-1);
    else if (m_selectedBox > boxIndex)
        setSelectedBoxIndex(m_selectedBox - 1);

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

int AppController::selectedBlockCorrectedCheckStatus() const
{
    const BoundingBox *box = selectedBox();
    return box ? static_cast<int>(box->correctedCheckStatus) : 0;
}

QVariantList AppController::selectedBlockTextRange() const
{
    const QList<int> range = blockTextRange(m_selectedBox);
    if (range.isEmpty())
        return {};
    return {range.at(0), range.at(1)};
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
    const BoundingBox *box = selectedBox();
    if (!box || !BoxTransitions::hasCorrection(*box))
        return;

    const int pageIndex = m_currentPage;
    const int boxIndex = m_selectedBox;
    if (!applyBoxTransition(pageIndex, boxIndex, BoxTransitions::correctionReverted))
        return;

    QString rebuilt;
    {
        QReadLocker locker(&m_documentLock);
        if (m_document.page(pageIndex).result.pages.isEmpty())
            return;
        rebuilt = rebuildPageText(m_document.page(pageIndex).result.pages[0]);
    }

    if (rebuilt == m_editStore.baseline(pageIndex)) {
        m_editStore.revert(pageIndex);
        m_pageModel.setEdited(pageIndex, false);
    } else {
        // Structural change: rebase the baseline (see removeBlock).
        m_editStore.reset(pageIndex, rebuilt);
        markPageEdited(pageIndex);
    }

    emit checkStateChanged();
    emit resultChanged();
    emit editStateChanged();
}

void AppController::checkEnabledBlocksOnPage(int fixMode)
{
    m_verify.checkPageEnabledBlocks(m_currentPage, fixMode);
}

void AppController::checkAllEnabledBlocks(int fixMode)
{
    m_verify.checkAllEnabledBlocks(fixMode);
}

void AppController::recognizeScopePages(const QString &task)
{
    const ConnectionRole role = connectionRoleFromName(task);
    if (m_recognition.busy() || m_layout.busy() || m_importing || checkBusy() || m_document.isEmpty())
        return;
    if (!canRecognize()) {
        setStatus(StatusMessage::translate("AppController", "Set a model name in Settings first."));
        return;
    }
    m_recognition.startPages(scopePages(), role);
    m_recognitionStopped = false;
}

void AppController::recognizeScopeBlocks()
{
    if (m_layout.busy())
        return;
    m_verify.recheckPagesBlocks(scopePages());
}

void AppController::layoutScopePages()
{
    startLayout(scopePages());
}

void AppController::checkScopeEnabledBlocks(int fixMode)
{
    m_verify.checkPagesEnabledBlocks(scopePages(), fixMode);
}

void AppController::fixScopeProblemBlocks()
{
    m_verify.fixPagesProblemBlocks(scopePages());
}

void AppController::stopCheck()
{
    m_verify.stop();
}

void AppController::recognizeSelectedBlock()
{
    m_verify.recheckBlock(m_currentPage, m_selectedBox);
}

void AppController::recognizeProblemBlocksOnPage()
{
    m_verify.recheckPageProblemBlocks(m_currentPage);
}

void AppController::recognizeAllProblemBlocks()
{
    m_verify.recheckAllProblemBlocks();
}

void AppController::recognizeAllBlocksOnPage()
{
    if (m_layout.busy())
        return;
    m_verify.recheckPageBlocks(m_currentPage);
}

void AppController::fixSelectedBlock()
{
    if (m_selectedBox < 0)
        return;
    m_verify.fixBlock(m_currentPage, m_selectedBox);
}

void AppController::fixAllProblemBlocks()
{
    m_verify.fixAllProblemBlocks();
}

const ModelProfiles::Role *AppController::layoutRole() const
{
    return ModelProfiles::roleFor(m_layoutRequestProfiles.activeProfileId(), QStringLiteral("layout"));
}

bool AppController::layoutAvailable() const
{
    return layoutRole() != nullptr;
}

bool AppController::pageBlocksRecognitionSupported() const
{
    return m_verify.pageBlocksRecognitionSupported(m_currentPage);
}

void AppController::layoutCurrentPage()
{
    startLayout({m_currentPage});
}

void AppController::layoutAllPages()
{
    QList<int> pages;
    for (int i = 0; i < m_document.pageCount(); ++i)
        pages.append(i);
    startLayout(pages);
}

void AppController::stopLayout()
{
    m_layout.stop();
}

void AppController::copyCurrentPageText()
{
    const QString text = resultText();
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
    setStatus(StatusMessage::translate("AppController", "Page text copied to the clipboard."));
}

void AppController::startLayout(const QList<int> &pages)
{
    if (pages.isEmpty() || m_layout.busy() || m_recognition.busy() || m_importing || checkBusy())
        return;
    const ModelProfiles::Role *role = layoutRole();
    if (!role) {
        setStatus(StatusMessage::translate("AppController", "The layout model does not support page markup."));
        return;
    }
    const QString prompt = role->prompts.isEmpty() ? QString() : role->prompts.constFirst().text;
    if (prompt.isEmpty()) {
        setStatus(StatusMessage::translate("AppController", "The model profile has no markup prompt."));
        return;
    }
    m_layout.layoutPages(pages, [this](int index) { return pageImage(index); }, role->systemPrompt, prompt);
}

void AppController::applyLayout(int pageIndex, const QString &rawText)
{
    if (!m_document.isValidIndex(pageIndex) || rawText.isEmpty())
        return;

    const ModelProfiles::Role *role = layoutRole();
    if (!role || role->parser.isEmpty())
        return;

    ParserOptions options;
    options.modelId = m_layoutRequestProfiles.activeProfileId();
    options.keepPageNumbers = true;
    m_boxModel.setStyleModelId(options.modelId);
    OcrResult parsed;
    if (auto parser = ParserFactory::create(role->parser, options))
        parsed = parser->parse(rawText);
    if (!parsed.success || parsed.pages.isEmpty())
        return;

    bool hasPositioned = false;
    for (const BoundingBox &box : parsed.pages.first().boxes) {
        if (box.positioned) {
            hasPositioned = true;
            break;
        }
    }
    if (!hasPositioned) {
        const StatusMessage message = StatusMessage::translate("AppController", "The model returned no layout blocks for page %1.").arg(pageIndex + 1);
        setStatus(message);
        reportProblem(message, ProblemLog::Warning);
        return;
    }

    const OcrPage &pageData = parsed.pages.first();
    const int blockCount = pageData.boxes.size();
    {
        QWriteLocker locker(&m_documentLock);
        DocumentPage &page = m_document.page(pageIndex);
        page.result = parsed;
        page.recognized = true;
        page.parseNote = parsed.notes.isEmpty() ? QString() : parsed.notes.join(QLatin1String(" "));
    }

    m_pageModel.setRecognized(pageIndex, true);
    setRecognitionMode(pageIndex, PageRecognitionMode::MarkupOnly);
    refreshPageVerification(pageIndex);
    if (pageData.hasDuplicates)
        m_pageModel.setHasDuplicates(pageIndex, true);

    const bool droppedEdit = m_editStore.reset(pageIndex, parsed.text);
    if (droppedEdit)
        m_pageModel.setEdited(pageIndex, false);

    ++m_cropRevision;

    if (pageIndex == m_currentPage)
        updateBoxesForCurrent();
    emit resultChanged();
    if (droppedEdit)
        emit editStateChanged();

    setStatus(StatusMessage::translate("AppController", "Page %1 marked up: %2 block(s).").arg(pageIndex + 1).arg(blockCount));
}

bool AppController::pageVerificationSupported() const
{
    return m_verify.pageVerificationSupported(m_currentPage);
}

bool AppController::allPageVerificationSupported() const
{
    return m_verify.allPageVerificationSupported();
}

int AppController::currentPageRecognitionMode() const
{
    return m_document.isValidIndex(m_currentPage) ? int(m_document.page(m_currentPage).recognitionMode) : int(PageRecognitionMode::None);
}

int AppController::currentPageVerification() const
{
    if (!m_document.isValidIndex(m_currentPage) || m_document.page(m_currentPage).result.pages.isEmpty())
        return int(PageVerification::Incomplete);
    return int(PageStatus::verificationAggregate(m_document.page(m_currentPage).result.pages.first(), [this](const QString &label) { return m_verification.isTypeEnabled(label); }));
}

void AppController::updateAvailability()
{
    const AvailabilityFlags current{
        pageVerificationSupported(),
        allPageVerificationSupported(),
        pageProblemRecognitionSupported(),
        allProblemRecognitionSupported(),
        pageBlocksRecognitionSupported(),
        canRecognize(),
        actionsIdle(),
        pageOcrModelAvailable(),
        rawPageOcrModelAvailable(),
        blockRecognitionModelAvailable(),
        decisionModelAvailable(),
        layoutModelAvailable(),
        fixModelAvailable(),
        scopeVerificationSupported(),
        scopeBlocksRecognitionSupported(),
        currentPageRecognitionMode(),
        currentPageVerification(),
        selectedPageCount(),
        scopedProblemBlockCount(),
    };
    if (current == m_availability)
        return;
    m_availability = current;
    emit availabilityChanged();
}

bool AppController::pageProblemRecognitionSupported() const
{
    return m_verify.pageRecheckSupported(m_currentPage);
}

bool AppController::pageOcrModelAvailable() const
{
    return m_runtime.taskModelAvailable(QStringLiteral("pageOcr"));
}

bool AppController::rawPageOcrModelAvailable() const
{
    return m_runtime.taskModelAvailable(QStringLiteral("rawPageOcr"));
}

bool AppController::blockRecognitionModelAvailable() const
{
    return m_runtime.taskModelAvailable(QStringLiteral("blockRecognition"));
}

bool AppController::decisionModelAvailable() const
{
    return m_runtime.taskModelAvailable(QStringLiteral("decision"));
}

bool AppController::layoutModelAvailable() const
{
    return m_runtime.taskModelAvailable(QStringLiteral("layout"));
}

bool AppController::fixModelAvailable() const
{
    return m_runtime.taskModelAvailable(QStringLiteral("fix"));
}

QList<int> AppController::scopePages() const
{
    QList<int> pages;
    const bool all = m_pageModel.selectedCount() == 0;
    for (int i = 0; i < m_document.pageCount(); ++i) {
        if (!all && !m_pageModel.data(m_pageModel.index(i), PageListModel::SelectedRole).toBool())
            continue;
        pages.append(i);
    }
    return pages;
}

void AppController::ensureDefaultSelection()
{
    if (m_pageModel.selectedCount() == 0 && m_document.isValidIndex(m_currentPage))
        m_pageModel.selectSingle(m_currentPage);
}

int AppController::selectedPageCount() const
{
    return m_pageModel.selectedCount();
}

int AppController::scopedProblemBlockCount() const
{
    int total = 0;
    for (const int pageIndex : scopePages()) {
        if (!m_document.isValidIndex(pageIndex))
            continue;
        const DocumentPage &page = m_document.page(pageIndex);
        if (!page.recognized || page.result.pages.isEmpty())
            continue;
        total += PageStatus::problemBlockCount(page.result.pages.first());
    }
    return total;
}

bool AppController::scopeVerificationSupported() const
{
    for (const int pageIndex : scopePages()) {
        if (m_verify.pageVerificationSupported(pageIndex))
            return true;
    }
    return false;
}

bool AppController::scopeBlocksRecognitionSupported() const
{
    for (const int pageIndex : scopePages()) {
        if (m_verify.pageBlocksRecognitionSupported(pageIndex))
            return true;
    }
    return false;
}

bool AppController::allProblemRecognitionSupported() const
{
    return m_verify.allPageRecheckSupported();
}

bool AppController::applyBoxTransition(int pageIndex, int boxIndex, const std::function<void(BoundingBox &)> &transition)
{
    if (!m_document.isValidIndex(pageIndex))
        return false;

    int status = int(BoxCheckStatus::NotChecked);
    QString corrected;
    QString text;
    double confidence = -1.0;
    int correctedStatus = int(BoxCheckStatus::NotChecked);
    double correctedConfidence = -1.0;
    {
        QWriteLocker locker(&m_documentLock);
        DocumentPage &page = m_document.page(pageIndex);
        if (!page.recognized || page.result.pages.isEmpty())
            return false;
        QList<BoundingBox> &boxes = page.result.pages[0].boxes;
        if (boxIndex < 0 || boxIndex >= boxes.size())
            return false;

        BoundingBox &box = boxes[boxIndex];
        transition(box);
        ++m_cropRevision;

        status = int(box.checkStatus);
        corrected = box.correctedText;
        text = box.text;
        confidence = box.confidence;
        correctedStatus = int(box.correctedCheckStatus);
        correctedConfidence = box.correctedConfidence;
    }

    if (pageIndex == m_currentPage) {
        m_boxModel.updateBoxCheck(boxIndex, status, corrected, text, confidence, correctedStatus, correctedConfidence);
        if (m_selectedBox == boxIndex)
            emit selectedBoxChanged();
    }
    refreshPageVerification(pageIndex);
    return true;
}

void AppController::setRecognitionMode(int pageIndex, PageRecognitionMode mode)
{
    if (!m_document.isValidIndex(pageIndex))
        return;
    m_document.page(pageIndex).recognitionMode = mode;
    m_pageModel.setRecognitionMode(pageIndex, int(mode));
}

void AppController::refreshPageVerification(int pageIndex)
{
    const PageVerification verification =
        m_document.isValidIndex(pageIndex) && !m_document.page(pageIndex).result.pages.isEmpty()
            ? PageStatus::verificationAggregate(m_document.page(pageIndex).result.pages.first(), [this](const QString &label) { return m_verification.isTypeEnabled(label); })
            : PageVerification::Incomplete;
    m_pageModel.setVerification(pageIndex, int(verification));
}

void AppController::refreshAllPageVerifications()
{
    for (int i = 0; i < m_document.pageCount(); ++i)
        refreshPageVerification(i);
}

void AppController::refreshRecognitionModeAfterBlockText(int pageIndex)
{
    if (!m_document.isValidIndex(pageIndex) || m_document.page(pageIndex).result.pages.isEmpty())
        return;
    const DocumentPage &page = m_document.page(pageIndex);
    const QString modelId = m_checkRequestProfiles.activeProfileId();
    const PageRecognitionMode mode = PageStatus::recognitionModeAfterBlockText(
        page.recognitionMode, page.result.pages.first(), [&modelId](const BoundingBox &box) { return blockStyleForLabel(box.label, modelId).style != BlockStyle::ImagePlaceholder; });
    setRecognitionMode(pageIndex, mode);
}

void AppController::applyVerificationStarted(int pageIndex, int boxIndex)
{
    applyBoxTransition(pageIndex, boxIndex, BoxTransitions::verificationStarted);
}

void AppController::applyVerdictToBox(int pageIndex, int boxIndex, const CheckResult &result)
{
    // A failed request is not a verdict: verificationStarted already erased
    // the previous one, so the block stays gray until a later run retries it.
    if (result.status == CheckStatus::Failed)
        return;
    applyBoxTransition(pageIndex, boxIndex, [&result](BoundingBox &box) { BoxTransitions::verdictApplied(box, result); });
}

void AppController::applyRecognitionToBox(int pageIndex, int boxIndex, const CheckResult &result)
{
    bool textChanged = false;
    const bool applied = applyBoxTransition(pageIndex, boxIndex, [&result, &textChanged](BoundingBox &box) {
        switch (result.status) {
        case CheckStatus::Recognized:
            // Any OCR result overwrites the text layer, the correction layer
            // and both verdicts (ADR 164) — a re-recognition never stacks a
            // second variant.
            BoxTransitions::ocrResultApplied(box, result.text);
            textChanged = true;
            break;
        case CheckStatus::Review:
            // Unreadable: the stored correction (a legacy project's) still
            // stops being the effective text.
            textChanged = !box.correctedText.isEmpty();
            BoxTransitions::blockUnreadable(box);
            break;
        case CheckStatus::Failed:
        case CheckStatus::Ok:
        case CheckStatus::Fixed:
        case CheckStatus::Mismatch:
            break;
        }
    });
    if (!applied)
        return;
    refreshRecognitionModeAfterBlockText(pageIndex);
    if (!textChanged)
        return;
    applyPageTextChanged(pageIndex);
}

void AppController::applyFixToBox(int pageIndex, int boxIndex, const CheckResult &result)
{
    // A failed request is not a fix: the block keeps its marks (and any
    // previous correction) until a later run retries it.
    if (result.status == CheckStatus::Failed)
        return;

    bool textChanged = false;
    const bool applied = applyBoxTransition(pageIndex, boxIndex, [&result, &textChanged](BoundingBox &box) {
        const QString before = BoxTransitions::effectiveText(box);
        switch (result.status) {
        case CheckStatus::Ok:
            // The candidate is confirmed: it becomes the correction layer's
            // duplicate of the text (the fix can be reverted, an Ok verdict
            // cannot be told from an absent one).
            BoxTransitions::fixConfirmedOriginal(box);
            break;
        case CheckStatus::Fixed:
            BoxTransitions::fixApplied(box, result.text);
            break;
        case CheckStatus::Review:
            // The model refuses to judge: the block is marked damaged on the
            // correction layer, any previous correction stops being effective.
            BoxTransitions::fixMarkedUnreadable(box);
            break;
        case CheckStatus::Failed:
        case CheckStatus::Recognized:
        case CheckStatus::Mismatch:
            break;
        }
        textChanged = BoxTransitions::effectiveText(box) != before;
    });
    if (!applied || !textChanged)
        return;
    applyPageTextChanged(pageIndex);
}

void AppController::applyPageTextChanged(int pageIndex)
{
    QString rebuilt;
    {
        QReadLocker locker(&m_documentLock);
        if (!m_document.isValidIndex(pageIndex) || m_document.page(pageIndex).result.pages.isEmpty())
            return;
        rebuilt = rebuildPageText(m_document.page(pageIndex).result.pages[0]);
    }

    setPageText(pageIndex, rebuilt);
    m_editStore.reset(pageIndex, rebuilt);
    markPageEdited(pageIndex);
    emit resultChanged();
    emit editStateChanged();
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

void AppController::markPageEdited(int index)
{
    if (m_editStore.isEdited(index))
        return;
    m_editStore.setEdited(index, true);
    m_pageModel.setEdited(index, true);
}

void AppController::notifyPageListGrown()
{
    emit documentChanged();
}

void AppController::notifyDocumentChanged()
{
    ++m_previewGeneration;
    {
        QMutexLocker locker(&m_previewMutex);
        m_previewCache.clear();
    }
    if (m_previewRendering.exchange(-1) != -1)
        emit previewRenderingChanged();
    {
        QMutexLocker locker(&m_thumbnailMutex);
        m_thumbnailCache.clear();
    }
    m_reportedUnrenderablePages.clear();
    emit documentChanged();
    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
    emit editStateChanged();
}

void AppController::notifyPageChanged()
{
    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
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

QString AppController::projectFileName() const
{
    return m_projectPath.isEmpty() ? QString() : QFileInfo(m_projectPath.toLocalFile()).fileName();
}

bool AppController::projectGuardsBusy() const
{
    return m_recognition.busy() || m_layout.busy() || m_importing || m_export.exporting() || m_projectBusy || m_verify.queueActive() || m_verify.checkBusy();
}

bool AppController::collectProjectData(ProjectData *data, QString *error)
{
    QWriteLocker locker(&m_documentLock);
    data->currentPage = m_currentPage;

    QHash<QString, int> sourceIds;
    for (int i = 0; i < m_document.pageCount(); ++i) {
        const DocumentPage &page = m_document.page(i);
        const QString key = QDir::fromNativeSeparators(QFileInfo(page.sourcePath).absoluteFilePath());
        int id = sourceIds.value(key, -1);
        if (id < 0) {
            ProjectSource source;
            source.id = data->sources.size();
            source.originalName = QFileInfo(key).fileName();
            source.typeName = ProjectStore::sourceTypeKey(page.sourceType);
            if (QFileInfo::exists(key)) {
                source.sourcePath = key;
            } else if (!page.image.isNull()) {
                // The source file is gone (a temp-dir import, a removed file);
                // embed the rendered copy the page still holds.
                source.fallbackImage = page.image;
            } else {
                if (error)
                    *error = QCoreApplication::translate("AppController", "The source file %1 is gone, and the page has no rendered copy to embed.").arg(source.originalName);
                return false;
            }
            id = source.id;
            sourceIds.insert(key, id);
            data->sources.append(source);
        }

        ProjectPageData entry;
        entry.sourceId = id;
        entry.sourcePageIndex = page.sourcePageIndex;
        entry.recognized = page.recognized;
        entry.recognitionMode = page.recognitionMode;
        entry.hasDuplicates = !page.result.pages.isEmpty() && page.result.pages.first().hasDuplicates;
        entry.text = page.result.text;
        entry.parseNote = page.parseNote;
        entry.boxes = page.result.pages.isEmpty() ? QList<BoundingBox>() : page.result.pages.first().boxes;
        if (page.recognized) {
            entry.baseline = m_editStore.baseline(i);
            entry.edited = m_editStore.isEdited(i);
        }
        data->pages.append(entry);
    }
    return true;
}

void AppController::applyProjectData(const ProjectData &data)
{
    {
        QWriteLocker locker(&m_documentLock);
        m_document.clear();
    }
    m_editStore.clear();
    m_pageModel.clear();
    m_boxModel.setBoxes({});
    m_selectedBox = -1;
    m_currentPage = 0;

    struct Occurrence {
        int first = 0;
        int count = 0;
        QSet<int> used;
    };
    QHash<int, QList<Occurrence>> occurrences;
    QList<int> mapped(data.pages.size(), -1);

    QHash<int, int> sourceIndexById;
    for (int i = 0; i < data.sources.size(); ++i)
        sourceIndexById.insert(data.sources.at(i).id, i);

    QStringList skipped;
    for (int p = 0; p < data.pages.size(); ++p) {
        const ProjectPageData &entry = data.pages.at(p);
        const int sourceIndex = sourceIndexById.value(entry.sourceId, -1);
        if (sourceIndex < 0) {
            skipped.append(QString::number(p + 1));
            continue;
        }
        const ProjectSource &source = data.sources.at(sourceIndex);
        if (!source.available) {
            skipped.append(QString::number(p + 1));
            continue;
        }

        int index = -1;
        for (Occurrence &range : occurrences[sourceIndex]) {
            const int slot = entry.sourcePageIndex >= 0 ? entry.sourcePageIndex : 0;
            if (slot < range.count && !range.used.contains(slot)) {
                range.used.insert(slot);
                index = range.first + slot;
                break;
            }
        }
        if (index < 0) {
            const int before = m_document.pageCount();
            QString appendError;
            {
                QWriteLocker locker(&m_documentLock);
                m_document.appendFile(source.extractedPath, &appendError);
            }
            const int added = m_document.pageCount() - before;
            if (added <= 0) {
                skipped.append(QString::number(p + 1));
                reportProblem(StatusMessage::literal(appendError));
                continue;
            }
            Occurrence range;
            range.first = before;
            range.count = added;
            const int slot = entry.sourcePageIndex >= 0 ? entry.sourcePageIndex : 0;
            if (slot < added) {
                range.used.insert(slot);
                index = before + slot;
            }
            occurrences[sourceIndex].append(range);
        }
        mapped[p] = index;
    }

    QList<int> kept;
    QList<int> savedIndexForSlot;
    for (int p = 0; p < mapped.size(); ++p) {
        if (mapped.at(p) >= 0) {
            kept.append(mapped.at(p));
            savedIndexForSlot.append(p);
        }
    }
    const QSet<int> keepSet(kept.cbegin(), kept.cend());
    {
        QWriteLocker locker(&m_documentLock);
        for (int i = m_document.pageCount() - 1; i >= 0; --i) {
            if (!keepSet.contains(i))
                m_document.removePage(i);
        }
    }
    QList<int> sortedKept = kept;
    std::sort(sortedKept.begin(), sortedKept.end());
    QHash<int, int> reindex;
    for (int i = 0; i < sortedKept.size(); ++i)
        reindex.insert(sortedKept.at(i), i);
    for (int &k : kept)
        k = reindex.value(k, -1);
    {
        QWriteLocker locker(&m_documentLock);
        for (int slot = 0; slot < kept.size(); ++slot) {
            const int from = kept.at(slot);
            if (from == slot)
                continue;
            m_document.movePage(from, slot);
            for (int q = 0; q < kept.size(); ++q) {
                if (q != slot && kept.at(q) >= slot && kept.at(q) < from)
                    ++kept[q];
            }
            kept[slot] = slot;
        }
    }

    m_pageModel.appendPages(kept.size());
    {
        QWriteLocker locker(&m_documentLock);
        for (int slot = 0; slot < kept.size(); ++slot) {
            const ProjectPageData &entry = data.pages.at(savedIndexForSlot.at(slot));
            DocumentPage &page = m_document.page(slot);
            page.recognized = entry.recognized;
            page.recognitionMode = entry.recognitionMode;
            page.parseNote = entry.parseNote;
            OcrResult result;
            result.success = true;
            OcrPage structured;
            structured.text = entry.text;
            structured.boxes = entry.boxes;
            structured.hasDuplicates = entry.hasDuplicates;
            result.text = entry.text;
            result.pages.append(structured);
            page.result = result;
        }
    }

    for (int slot = 0; slot < kept.size(); ++slot) {
        const ProjectPageData &entry = data.pages.at(savedIndexForSlot.at(slot));
        m_editStore.reset(slot, entry.baseline);
        if (entry.edited)
            m_editStore.setEdited(slot, true);
        m_pageModel.setRecognized(slot, entry.recognized);
        m_pageModel.setEdited(slot, entry.edited);
        m_pageModel.setHasDuplicates(slot, entry.hasDuplicates);
        m_pageModel.setRecognitionMode(slot, int(entry.recognitionMode));
    }
    refreshAllPageVerifications();

    m_pageModel.setCurrent(0);
    m_currentPage = data.pages.isEmpty() ? 0 : std::clamp(data.currentPage, 0, m_document.pageCount() - 1);
    m_pageModel.setCurrent(m_currentPage);
    ensureDefaultSelection();
    // Project labels come from whatever model created the file; the default
    // vocabulary is the only safe overlay mapping here.
    m_boxModel.setStyleModelId(QString());
    updateBoxesForCurrent();

    if (!skipped.isEmpty())
        reportProblem(StatusMessage::translate("AppController", "Project pages %1 could not be restored — see the problem log.").arg(skipped.join(QStringLiteral(", "))));
    if (data.pages.isEmpty() || m_document.isEmpty())
        setStatus(StatusMessage::translate("AppController", "The project contains no pages."));
    else if (skipped.isEmpty())
        setStatus(StatusMessage::translate("AppController", "Project opened: %1 page(s).").arg(m_document.pageCount()));
    else
        setStatus(StatusMessage::translate("AppController", "Project opened: %1 page(s), %2 skipped.").arg(m_document.pageCount()).arg(skipped.size()));

    notifyDocumentChanged();
}

void AppController::openProject(const QUrl &fileUrl)
{
    if (projectGuardsBusy())
        return;
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        setStatus(StatusMessage::translate("AppController", "No project selected."));
        return;
    }

    auto sessionDir = std::make_shared<QTemporaryDir>();
    if (!sessionDir->isValid()) {
        setStatus(StatusMessage::translate("AppController", "Cannot create a temporary directory for the project."));
        return;
    }

    m_projectBusy = true;
    emit projectBusyChanged();
    setStatus(StatusMessage::translate("AppController", "Opening project…"));

    auto *watcher = new QFutureWatcher<ProjectStore::LoadResult>(this);
    connect(watcher, &QFutureWatcher<ProjectStore::LoadResult>::finished, this, [this, watcher, path, sessionDir]() {
        watcher->deleteLater();
        ProjectStore::LoadResult result = watcher->result();
        m_projectBusy = false;
        emit projectBusyChanged();

        for (const QString &warning : std::as_const(result.warnings))
            reportProblem(StatusMessage::literal(warning));
        if (!result.error.isEmpty()) {
            setStatus(StatusMessage::translate("AppController", "Cannot open the project: %1").arg(result.error));
            reportProblem(StatusMessage::literal(result.error), ProblemLog::Error);
            return;
        }

        applyProjectData(result.data);
        m_projectSessionDir = sessionDir;
        m_projectPath = QUrl::fromLocalFile(path);
        emit projectPathChanged();
    });
    const QString extractDir = sessionDir->path();
    watcher->setFuture(QtConcurrent::run([path, extractDir]() { return ProjectStore::load(path, extractDir); }));
}

void AppController::saveProject(const QUrl &fileUrl)
{
    if (projectGuardsBusy())
        return;
    if (m_document.isEmpty()) {
        setStatus(StatusMessage::translate("AppController", "Nothing to save — no pages are open."));
        return;
    }
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        setStatus(StatusMessage::translate("AppController", "No project selected."));
        return;
    }

    ProjectData data;
    QString error;
    if (!collectProjectData(&data, &error)) {
        setStatus(StatusMessage::translate("AppController", "Cannot save the project: %1").arg(error));
        reportProblem(StatusMessage::literal(error), ProblemLog::Error);
        return;
    }

    m_projectBusy = true;
    emit projectBusyChanged();
    setStatus(StatusMessage::translate("AppController", "Saving project…"));

    auto *watcher = new QFutureWatcher<std::pair<bool, QString>>(this);
    connect(watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this, [this, watcher, path]() {
        watcher->deleteLater();
        const auto outcome = watcher->result();
        m_projectBusy = false;
        emit projectBusyChanged();
        if (!outcome.first) {
            setStatus(StatusMessage::translate("AppController", "Cannot save the project: %1").arg(outcome.second));
            reportProblem(StatusMessage::literal(outcome.second), ProblemLog::Error);
            return;
        }
        m_projectPath = QUrl::fromLocalFile(path);
        emit projectPathChanged();
        setStatus(StatusMessage::translate("AppController", "Project saved: %1").arg(QFileInfo(path).fileName()));
    });
    watcher->setFuture(QtConcurrent::run([path, data]() {
        QString saveError;
        const bool ok = ProjectStore::save(path, data, &saveError);
        return std::make_pair(ok, saveError);
    }));
}

}  // namespace llocr
