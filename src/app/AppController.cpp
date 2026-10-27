#include "app/AppController.h"
#include "app/LruImageCache.h"
#include "app/PageIndex.h"
#include "app/PageRotation.h"

#include <algorithm>
#include <utility>

#include <QBuffer>
#include <QClipboard>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QMutex>
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
#include "core/LatexText.h"
#include "core/ModelProfiles.h"
#include "core/StatusMessage.h"
#include "models/OcrModel.h"

namespace llocr {

namespace {

// The model's answer has to be this certain before a page is turned on it.
constexpr double kOrientationConfidence = 0.6;

// A page on its side needs much more: the model names the same direction for
// both sides of the same page (measured on the corpus: "90" for every page
// that lies on its side), so its confidence there says nothing about the
// direction — only a very certain answer is worth a quarter turn.
constexpr double kQuarterTurnConfidence = 0.75;

}  // namespace

AppController::AppController(SettingsStore &settings,
                             RuntimeController &runtime,
                             RequestProfileStore &requestProfiles,
                             RequestProfileStore &rawPageOcrRequestProfiles,
                             RequestProfileStore &checkRequestProfiles,
                             RequestProfileStore &layoutRequestProfiles,
                             RequestProfileStore &fixRequestProfiles,
                             VerificationPromptStore &verification,
                             QObject *parent)
    : QObject(parent), m_settings(settings), m_runtime(runtime), m_verification(verification), m_checkRequestProfiles(checkRequestProfiles), m_layoutRequestProfiles(layoutRequestProfiles),
      m_fixRequestProfiles(fixRequestProfiles), m_inspector({m_document,
                                                             [this] { return m_currentPage; },
                                                             [this] { return m_docRevision; },
                                                             [this] { return m_cropRevision; },
                                                             [this](int index) { return pageText(index); },
                                                             [this] { return makeParser(); }},
                                                            this),
      m_recognition(
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
                [this]() { return m_recognition.busy(); },
                [this](const QString &candidateText, const std::function<void(bool, const QImage &, const QString &)> &done) { m_candidateRenderer.render(candidateText, done); }},
               this),
      m_layout(layoutRequestProfiles, runtime, this), m_orientationDecision(runtime, this), m_availability({m_document,
                                                                                                            m_pageModel,
                                                                                                            m_verify,
                                                                                                            runtime,
                                                                                                            verification,
                                                                                                            [this] { return m_currentPage; },
                                                                                                            [this] { return m_importing; },
                                                                                                            [this] { return m_recognition.busy(); },
                                                                                                            [this] { return m_layout.busy(); },
                                                                                                            [this] { return checkBusy(); }},
                                                                                                           this),
      m_export({m_document, m_settings, [this](int pageIndex, int boxIndex) { return croppedImage(pageIndex, boxIndex); }, [this]() { return m_importing; }}, this),
      m_project({m_document,
                 m_pageModel,
                 m_boxModel,
                 m_editStore,
                 m_documentLock,
                 [this] { return m_currentPage; },
                 [this](int index) {
                     m_currentPage = index;
                     m_pageModel.setCurrent(index);
                 },
                 [this] { return m_recognition.busy() || m_layout.busy() || m_importing || m_export.exporting() || m_verify.queueActive() || m_verify.checkBusy(); },
                 [this](const StatusMessage &message) { setStatus(message); },
                 [this](const StatusMessage &message, ProblemLog::Severity severity) { reportProblem(message, severity); },
                 [this] { m_inspector.resetSelection(); },
                 [this] { refreshAllPageVerifications(); },
                 [this] { ensureDefaultSelection(); },
                 [this] { updateBoxesForCurrent(); },
                 [this] { notifyDocumentChanged(); }},
                this)
{
    // The orientation check: a page that lies the wrong way up is turned before
    // it is recognized, so the model reads the text the right way round.
    connect(&m_orientation, &OrientationController::turned, this, [this](int index, int degrees) {
        if (!turnPage(index, degrees, false))
            return;
        setStatus(StatusMessage::translate("AppController", "Page %1 turned %2° clockwise before the recognition.").arg(index + 1).arg(normalizeRotation(degrees)));
    });
    connect(&m_orientation, &OrientationController::unclear, this, [this](int index) {
        if (m_orientationVerdicts.value(index).sideways) {
            setStatus(StatusMessage::translate("AppController", "Page %1 lies on its side. The check cannot tell which way it has to be turned — turn it by hand.").arg(index + 1));
            return;
        }
        setStatus(StatusMessage::translate("AppController", "Page %1: the orientation is not clear, the page was left as it is.").arg(index + 1));
    });
    connect(&m_orientation, &OrientationController::noText, this, [this](int index) {
        setStatus(StatusMessage::translate("AppController", "Page %1: there is no text on the page, it was left as it is.").arg(index + 1));
    });
    connect(&m_orientation, &OrientationController::finished, this, [this]() {
        if (!m_pendingRun)
            return;
        const std::function<void()> run = m_pendingRun;
        m_pendingRun = nullptr;
        run();
    });
    connect(&m_orientationDecision, &DecisionController::judgeFinished, this, &AppController::onOrientationVerdict);

    // The inspector owns the selected block: its signals are the facade's.
    connect(&m_inspector, &BlockInspector::selectedBoxChanged, this, &AppController::selectedBoxChanged);
    connect(&m_inspector, &BlockInspector::blockTextRangeChanged, this, &AppController::blockTextRangeChanged);
    connect(&m_inspector, &BlockInspector::blockDiffChanged, this, &AppController::blockDiffChanged);
    connect(&m_inspector, &BlockInspector::highlightDifferencesChanged, this, &AppController::highlightDifferencesChanged);
    connect(&m_inspector, &BlockInspector::pageTextEditorFocusedChanged, this, &AppController::pageTextEditorFocusedChanged);

    connect(&m_recognition, &RecognitionController::busyChanged, this, [this]() {
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
        ++m_verificationFilterRevision;
        refreshAllPageVerifications();
        updateAvailability();
    };
    connect(qobject_cast<VerificationBlocksModel *>(verification.blockModel()), &VerificationBlocksModel::countsChanged, this, verificationFilterChanged);
    connect(&verification, &VerificationPromptStore::modelChanged, this, verificationFilterChanged);

    connect(&m_verify, &VerificationQueueController::stateChanged, this, &AppController::checkStateChanged);
    connect(&m_verify, &VerificationQueueController::stateChanged, this, &AppController::updateAvailability);
    connect(&m_verify, &VerificationQueueController::statusRequested, this, [this](const StatusMessage &message) { setStatus(message); });
    connect(&m_verify, &VerificationQueueController::verificationStarted, this, &AppController::applyVerificationStarted);
    connect(&m_verify, &VerificationQueueController::blockVerified, this, &AppController::applyVerdictToBox);
    connect(&m_verify, &VerificationQueueController::blockRecognized, this, &AppController::applyRecognitionToBox);
    connect(&m_verify, &VerificationQueueController::blockFixed, this, &AppController::applyFixToBox);
    connect(&m_verify, &VerificationQueueController::problemReported, this, [this](const StatusMessage &message) { reportProblem(message, ProblemLog::Error); });

    connect(&m_availability, &AvailabilityPresenter::changed, this, &AppController::availabilityChanged);

    connect(this, &AppController::pageChanged, this, &AppController::updateAvailability);
    connect(this, &AppController::documentChanged, this, &AppController::updateAvailability);
    connect(this, &AppController::resultChanged, this, &AppController::updateAvailability);
    connect(this, &AppController::configChanged, this, &AppController::updateAvailability);
    connect(this, &AppController::importingChanged, this, &AppController::updateAvailability);
    connect(&m_pageModel, &PageListModel::selectionChanged, this, [this]() {
        ++m_pageSelectionRevision;
        updateAvailability();
    });

    connect(&m_layout, &LayoutController::busyChanged, this, &AppController::layoutStateChanged);
    connect(&m_layout, &LayoutController::busyChanged, this, &AppController::updateAvailability);
    connect(&m_layout, &LayoutController::statusRequested, this, [this](const StatusMessage &message) { setStatus(message); });
    connect(&m_layout, &LayoutController::progressChanged, this, &AppController::layoutProgressChanged);
    connect(&m_layout, &LayoutController::layoutFinished, this, &AppController::applyLayout);
    connect(&m_settings, &SettingsStore::layoutRequestProfileIdChanged, this, &AppController::layoutAvailableChanged);

    connect(&m_export, &ExportController::exportingChanged, this, &AppController::exportingChanged);

    connect(&m_project, &ProjectController::busyChanged, this, &AppController::projectBusyChanged);
    connect(&m_project, &ProjectController::pathChanged, this, &AppController::projectPathChanged);
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
    connect(this, &AppController::resultChanged, &m_inspector, &BlockInspector::announceTextRangeChanged);
    connect(this, &AppController::editStateChanged, &m_inspector, &BlockInspector::announceTextRangeChanged);
    // The parser options shape the block-to-text mapping; the memoized rebuild
    // has no revision to key on for them.
    connect(&m_settings, &SettingsStore::parserIdChanged, &m_inspector, &BlockInspector::invalidateRebuildCache);
    connect(&m_settings, &SettingsStore::modelRecipeIdChanged, &m_inspector, &BlockInspector::invalidateRebuildCache);
    connect(&m_settings, &SettingsStore::keepPageNumbersChanged, &m_inspector, &BlockInspector::invalidateRebuildCache);
    connect(&m_settings, &SettingsStore::tablesAsHtmlChanged, &m_inspector, &BlockInspector::invalidateRebuildCache);
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
    return effectiveParserIdFor(m_settings.modelRecipeId(), QStringLiteral("pageOcr"));
}

QString AppController::effectiveParserIdFor(const QString &profileId, const QString &role) const
{
    const QString configured = m_settings.parserId();
    if (!configured.isEmpty() && configured != ParserFactory::kAutoId)
        return configured;
    // The parser a profile declares never changes, so the id is remembered
    // for the profile: the QML bindings ask for it on every change, and the
    // profiles are read only once (ADR 207). The task is part of the key: a
    // model can declare a different parser per task.
    const QString key = profileId + QLatin1Char('|') + role;
    if (key != m_parserIdKey) {
        m_parserIdKey = key;
        m_parserIdCache = OcrModel::defaultParserIdFor(profileId, role);
    }
    return m_parserIdCache;
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

namespace {

// The pipeline changed the block-derived text from `previous` to `rebuilt`.
// The page carries the user's own text: the change must land in it, or the
// block the operator just made is missing from the document. The user's
// wording wins where it diverges from the recognition — the change is applied
// where the two still agree (ADR 190).
QString mergePipelineChange(const QString &userText, const QString &previous, const QString &rebuilt)
{
    int prefix = 0;
    const int maxPrefix = qMin(previous.size(), rebuilt.size());
    while (prefix < maxPrefix && previous.at(prefix) == rebuilt.at(prefix))
        ++prefix;
    int suffix = 0;
    const int maxSuffix = qMin(previous.size() - prefix, rebuilt.size() - prefix);
    while (suffix < maxSuffix && previous.at(previous.size() - 1 - suffix) == rebuilt.at(rebuilt.size() - 1 - suffix))
        ++suffix;
    const QString oldMiddle = previous.mid(prefix, previous.size() - prefix - suffix);
    const QString newMiddle = rebuilt.mid(prefix, rebuilt.size() - prefix - suffix);
    if (newMiddle.isEmpty())
        return userText;  // a removal: the removed text is the user's to keep

    // The anchor is the part the two texts still share; the change belongs
    // right after it.
    const QString anchor = rebuilt.left(prefix);
    const int anchorPos = anchor.isEmpty() ? 0 : userText.lastIndexOf(anchor);
    if (anchorPos < 0)
        return userText;  // the user's text diverged: their wording stands
    const int searchFrom = anchorPos + anchor.size();

    if (oldMiddle.isEmpty()) {
        // An addition: the new content goes where the blocks put it.
        QString separator;
        if (!userText.isEmpty() && !userText.endsWith(QLatin1Char('\n')))
            separator = QStringLiteral("\n\n");
        else if (userText.endsWith(QLatin1Char('\n')) && !userText.endsWith(QStringLiteral("\n\n")))
            separator = QStringLiteral("\n");
        return userText.left(searchFrom) + separator + newMiddle + userText.mid(searchFrom);
    }

    // A replacement: the old text gives way to the new one, where the user's
    // text still carries the old one.
    const int pos = userText.indexOf(oldMiddle, searchFrom);
    if (pos < 0)
        return userText;
    return userText.left(pos) + newMiddle + userText.mid(pos + oldMiddle.size());
}

// The formula blocks of a fresh recognition result (ADR 194): the canonical
// LaTeX form of the text and the syntax verdict on it. A formula whose LaTeX
// is broken is rejected here — it never reaches a model.
void applyFormulaVerdicts(OcrResult &result)
{
    for (OcrPage &page : result.pages) {
        for (BoundingBox &box : page.boxes) {
            if (box.text.isEmpty() || !LatexText::isMathLabel(box.label))
                continue;
            BoxTransitions::latexResultApplied(box);
        }
    }
}

// The page text follows the blocks: the formula blocks were brought to their
// canonical form, so the text is built from them again — the model's spacing
// and the markdown fence that carried the formula must not survive into the
// document (ADR 194, ADR 195).
void rebuildTextFromBlocks(OcrResult &result, const IOutputParser &parser)
{
    for (OcrPage &page : result.pages) {
        page.text = parser.rebuildText(page);
        result.text = page.text;
    }
}

}  // namespace

QString AppController::rebuildPageText(const OcrPage &page) const
{
    const auto parser = makeParser();
    return parser ? parser->rebuildText(page) : page.text;
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
        QImage placeholder(rotatedSize(request.page.pixelSize, request.page.rotation), QImage::Format_RGB32);
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

    // The page may lie the wrong way up: it is turned before the recognition.
    const int index = m_currentPage;
    if (runAfterOrientationCheck({index}, [this, role, index]() { m_recognition.startCurrent(index, m_document.pageCount(), role); }))
        return;
    m_recognition.startCurrent(index, m_document.pageCount(), role);
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

    QList<int> pages;
    for (int index = 0; index < m_document.pageCount(); ++index)
        pages.append(index);
    if (runAfterOrientationCheck(pages, [this, role, pages]() { m_recognition.startAll(pages.size(), role); }))
        return;
    m_recognition.startAll(pages.size(), role);
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
    if (task == ConnectionRole::RawPageOcr) {
        options.modelId = m_settings.requestProfileIdForRole(connectionRoleName(task));
        // The task asks for the text alone: a model that volunteers the
        // layout anyway does not turn the page into a markup page.
        options.ignoreLayoutMarkup = true;
    }
    // The model's geometry is read against the page it was shown: the turned
    // page, so the aspect follows the rotation.
    const QSize pixelSize = m_document.page(index).displaySize();
    if (pixelSize.isValid() && pixelSize.width() > 0)
        options.pageAspect = qreal(pixelSize.height()) / pixelSize.width();

    OcrResult parsed = rawResult;
    if (auto parser = ParserFactory::create(effectiveParserIdFor(options.modelId, connectionRoleName(task)), options)) {
        parsed = parser->parse(rawResult.text);
        applyFormulaVerdicts(parsed);
        rebuildTextFromBlocks(parsed, *parser);
    }
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
    if (m_editStore.isEdited(m_currentPage)) {
        // The user's own text owns the page: a removal changes the blocks, not
        // the text under the panel. The revert target moves forward, so the
        // next Revert lands on the structurally-consistent text.
        m_editStore.rebase(m_currentPage, rebuilt);
    } else {
        setPageText(m_currentPage, rebuilt);
        // A removed block is a structural change, not a manual edit: the text
        // still follows the blocks, so a later recognition may write it again
        // — the same rule as a block landing. The revert target moves forward:
        // the removed block's text must not come back (ADR 190).
        m_editStore.reset(m_currentPage, rebuilt);
    }

    if (m_inspector.selectedBoxIndex() == boxIndex)
        setSelectedBoxIndex(-1);
    else if (m_inspector.selectedBoxIndex() > boxIndex)
        setSelectedBoxIndex(m_inspector.selectedBoxIndex() - 1);

    emit resultChanged();
    emit editStateChanged();
    return true;
}

bool AppController::addBlock(const QString &type)
{
    return addBlockAt(type, 0.35, 0.45, 0.3, 0.08);
}

bool AppController::addBlockAt(const QString &type, qreal x, qreal y, qreal width, qreal height)
{
    if (m_recognition.busy() || m_layout.busy() || m_importing || checkBusy())
        return false;
    if (!m_document.isValidIndex(m_currentPage))
        return false;

    BoundingBox box;
    box.label = type.isEmpty() ? QStringLiteral("text") : type;
    // The rectangle the user drew stays inside the page (ADR 189).
    const qreal minSize = 0.01;
    const qreal w = qBound(minSize, width, qreal(1.0));
    const qreal h = qBound(minSize, height, qreal(1.0));
    box.rect = QRectF(qBound(qreal(0.0), x, qreal(1.0) - w), qBound(qreal(0.0), y, qreal(1.0) - h), w, h);
    // The text the blocks stand for: the page text follows the blocks only as
    // long as it is that text (a manual edit or a raw page OCR owns it
    // otherwise).
    const QString blockDerivedText = rebuildPageText(m_document.page(m_currentPage).result.pages.value(0));
    {
        QWriteLocker locker(&m_documentLock);
        DocumentPage &page = m_document.page(m_currentPage);
        if (page.result.pages.isEmpty())
            page.result.pages.append(OcrPage());
        page.result.pages[0].boxes.append(box);
        page.recognized = true;
        ++m_cropRevision;
    }

    m_pageModel.setRecognized(m_currentPage, true);
    if (m_document.page(m_currentPage).recognitionMode == PageRecognitionMode::None)
        setRecognitionMode(m_currentPage, PageRecognitionMode::MarkupOnly);

    updateBoxesForCurrent();

    const OcrPage &page = m_document.page(m_currentPage).result.pages[0];
    const QString rebuilt = rebuildPageText(page);
    if (pageText(m_currentPage) == blockDerivedText) {
        setPageText(m_currentPage, rebuilt);
    }
    // The revert target moves forward with the blocks — also when the page
    // carries the user's own text: the next Revert must land on the text the
    // blocks stand for.
    m_editStore.rebase(m_currentPage, rebuilt);

    refreshPageVerification(m_currentPage);
    setSelectedBoxIndex(page.boxes.size() - 1);

    setStatus(StatusMessage::translate("AppController", "Block added."));
    emit resultChanged();
    return true;
}

void AppController::checkSelectedBlock()
{
    if (m_inspector.selectedBoxIndex() < 0)
        return;
    m_verify.checkBlock(m_currentPage, m_inspector.selectedBoxIndex());
}

void AppController::revertBlockCorrection()
{
    const BoundingBox *box = m_inspector.selectedBox();
    if (!box || !BoxTransitions::hasCorrection(*box))
        return;

    const int pageIndex = m_currentPage;
    const int boxIndex = m_inspector.selectedBoxIndex();
    if (!applyBoxTransition(pageIndex, boxIndex, BoxTransitions::correctionReverted))
        return;

    // The page text follows the block: the preview must not keep the reverted
    // correction. A page the user edited by hand keeps their text — only the
    // undo target moves (the same rule the fix follows).
    applyPageTextChanged(pageIndex);

    emit checkStateChanged();
    emit resultChanged();
    emit editStateChanged();
}

void AppController::setSelectedBlockUsesCorrected(bool corrected)
{
    const int pageIndex = m_currentPage;
    const int boxIndex = m_inspector.selectedBoxIndex();
    {
        QReadLocker locker(&m_documentLock);
        if (!m_document.isValidIndex(pageIndex) || m_document.page(pageIndex).result.pages.isEmpty())
            return;
        const QList<BoundingBox> &boxes = m_document.page(pageIndex).result.pages.first().boxes;
        if (boxIndex < 0 || boxIndex >= boxes.size())
            return;
        const BoundingBox &box = boxes.at(boxIndex);
        // Nothing to switch to: a block without a correction has one text.
        if (box.correctedText.isEmpty() || box.useCorrectedText == corrected)
            return;
    }

    if (!applyBoxTransition(pageIndex, boxIndex, [corrected](BoundingBox &box) { BoxTransitions::layerSwitched(box, corrected); }))
        return;

    // The document's text follows the layer the user reads (ADR 185).
    applyPageTextChanged(pageIndex);
}

void AppController::approveSelectedBlock()
{
    if (m_inspector.selectedBoxIndex() < 0)
        return;

    const int pageIndex = m_currentPage;
    const int boxIndex = m_inspector.selectedBoxIndex();
    if (!applyBoxTransition(pageIndex, boxIndex, BoxTransitions::blockApproved))
        return;

    setStatus(StatusMessage::translate("AppController", "Block approved."));
    updateAvailability();
    emit checkStateChanged();
    emit resultChanged();
}

void AppController::revertBlockApproval()
{
    if (m_inspector.selectedBoxIndex() < 0)
        return;

    // Nothing to undo: the block in view carries no confirmation.
    const BoundingBox *box = m_inspector.selectedBox();
    if (!box || BoxTransitions::effectiveCheckStatus(*box) != BoxCheckStatus::Approved)
        return;

    const int pageIndex = m_currentPage;
    const int boxIndex = m_inspector.selectedBoxIndex();
    if (!applyBoxTransition(pageIndex, boxIndex, BoxTransitions::blockApprovalReverted))
        return;

    // The text does not change: only the verdict does.
    setStatus(StatusMessage::translate("AppController", "Block approval reverted."));
    updateAvailability();
    emit checkStateChanged();
    emit resultChanged();
}

void AppController::approveSelectedBlockAndSelectNext()
{
    approveSelectedBlock();
    selectNextProblemBlock();
}

void AppController::selectNextProblemBlock()
{
    const int pageIndex = m_currentPage;
    int next = -1;
    {
        QReadLocker locker(&m_documentLock);
        if (!m_document.isValidIndex(pageIndex) || m_document.page(pageIndex).result.pages.isEmpty())
            return;
        const QList<BoundingBox> &boxes = m_document.page(pageIndex).result.pages.first().boxes;
        // The blocks the decision model left open, in reading order; the search
        // starts behind the current one so the operator moves forward.
        for (int index = m_inspector.selectedBoxIndex() + 1; index < boxes.size(); ++index) {
            const BoxCheckStatus status = BoxTransitions::effectiveCheckStatus(boxes.at(index));
            if (status == BoxCheckStatus::Mismatch || status == BoxCheckStatus::Review) {
                next = index;
                break;
            }
        }
    }
    if (next < 0)
        return;
    setSelectedBoxIndex(next);
}

void AppController::setSelectedBlockText(const QString &text, bool correctedLayer)
{
    if (m_inspector.selectedBoxIndex() < 0)
        return;

    const int pageIndex = m_currentPage;
    const int boxIndex = m_inspector.selectedBoxIndex();
    if (!applyBoxTransition(pageIndex, boxIndex, [&text, correctedLayer](BoundingBox &box) { BoxTransitions::blockTextEdited(box, text, correctedLayer); }))
        return;

    // The page text follows the block, exactly as a correction does.
    applyPageTextChanged(pageIndex);
    setStatus(StatusMessage::translate("AppController", "Block text changed."));
    updateAvailability();
    emit checkStateChanged();
    emit resultChanged();
}

void AppController::checkEnabledBlocksOnPage()
{
    m_verify.checkPageEnabledBlocks(m_currentPage);
}

void AppController::checkAllEnabledBlocks()
{
    m_verify.checkAllEnabledBlocks();
}

void AppController::recognizeScopePages(const QString &task)
{
    const ConnectionRole role = connectionRoleFromName(task);
    if (m_recognition.busy() || m_layout.busy() || m_importing || checkBusy() || m_document.isEmpty())
        return;
    if (orientationCheckBusy()) {
        setStatus(StatusMessage::translate("AppController", "The orientation check is running."));
        return;
    }
    if (!canRecognize()) {
        setStatus(StatusMessage::translate("AppController", "Set a model name in Settings first."));
        return;
    }
    const QList<int> pages = scopePages();
    if (runAfterOrientationCheck(pages, [this, role, pages]() { m_recognition.startPages(pages, role); }))
        return;
    m_recognition.startPages(pages, role);
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

void AppController::checkScopeEnabledBlocks()
{
    m_verify.checkPagesEnabledBlocks(scopePages());
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
    m_verify.recheckBlock(m_currentPage, m_inspector.selectedBoxIndex());
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
    if (m_inspector.selectedBoxIndex() < 0)
        return;
    m_verify.fixBlock(m_currentPage, m_inspector.selectedBoxIndex());
}

void AppController::fixAllProblemBlocks()
{
    m_verify.fixAllProblemBlocks();
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
    if (orientationCheckBusy()) {
        setStatus(StatusMessage::translate("AppController", "The orientation check is running."));
        return;
    }
    const ModelProfiles::Role *role = m_layout.role();
    if (!role) {
        setStatus(StatusMessage::translate("AppController", "The layout model does not support page markup."));
        return;
    }
    const QString prompt = role->prompts.isEmpty() ? QString() : role->prompts.constFirst().text;
    if (prompt.isEmpty()) {
        setStatus(StatusMessage::translate("AppController", "The model profile has no markup prompt."));
        return;
    }
    // The markup model reads the page too: the page is turned first when it
    // lies the wrong way up.
    const auto start = [this, pages, role, prompt]() { m_layout.layoutPages(pages, [this](int index) { return pageImage(index); }, role->systemPrompt, prompt); };
    if (runAfterOrientationCheck(pages, start))
        return;
    start();
}

void AppController::applyLayout(int pageIndex, const QString &rawText)
{
    if (!m_document.isValidIndex(pageIndex) || rawText.isEmpty())
        return;

    const ModelProfiles::Role *role = m_layout.role();
    if (!role || role->parser.isEmpty())
        return;

    ParserOptions options;
    options.modelId = m_layoutRequestProfiles.activeProfileId();
    options.keepPageNumbers = true;
    m_boxModel.setStyleModelId(options.modelId);
    OcrResult parsed;
    if (auto parser = ParserFactory::create(role->parser, options)) {
        parsed = parser->parse(rawText);
        applyFormulaVerdicts(parsed);
        rebuildTextFromBlocks(parsed, *parser);
    }
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
        // A stripped reply is the tell of a server serving the model without
        // its launch layer (e.g. started before the profile changed): the
        // numbers arrive, the special-token markers around them do not.
        QString snippet = rawText.simplified();
        if (snippet.size() > 160)
            snippet = snippet.left(160) + QStringLiteral("…");
        const StatusMessage message = rawText.isEmpty()
                                          ? StatusMessage::translate("AppController", "The model returned no layout blocks for page %1.").arg(pageIndex + 1)
                                          : StatusMessage::translate("AppController", "The model returned no layout blocks for page %1. Reply starts: %2").arg(pageIndex + 1).arg(snippet);
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

void AppController::updateAvailability()
{
    m_availability.refresh({m_docRevision, m_cropRevision, m_pageSelectionRevision, m_verificationFilterRevision});
}

QList<int> AppController::scopePages() const
{
    return m_availability.scopePages();
}

void AppController::ensureDefaultSelection()
{
    if (m_pageModel.selectedCount() == 0 && m_document.isValidIndex(m_currentPage))
        m_pageModel.selectSingle(m_currentPage);
}

bool AppController::applyBoxTransition(int pageIndex, int boxIndex, const std::function<void(BoundingBox &)> &transition)
{
    if (!m_document.isValidIndex(pageIndex))
        return false;

    BoundingBox updated;
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
        updated = box;
    }

    if (pageIndex == m_currentPage) {
        m_boxModel.updateBoxState(boxIndex, updated);
        if (m_inspector.selectedBoxIndex() == boxIndex)
            m_inspector.blockStateChanged();
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

    // A demoted FIX (diff-gate downgrade, EMPTY crop) still lands on the
    // block — the reason tells the user why nothing changed.
    if (!result.notes.isEmpty())
        setStatus(result.notes);

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

    // A manual edit owns the page text until it is reverted: a block landing
    // (recognition, fix, re-check) updates the blocks but must not wipe the
    // user's text out from under the panel — that used to read as a silent
    // reset, and the next «Отменить изменения» did nothing. The revert target
    // moves forward to the block-derived text instead.
    if (m_editStore.isEdited(pageIndex)) {
        // The pipeline's change still lands in the user's text: dropping it
        // would leave the block the operator just made out of the document
        // (ADR 190).
        const QString previous = m_editStore.baseline(pageIndex);
        m_editStore.rebase(pageIndex, rebuilt);
        const QString merged = mergePipelineChange(pageText(pageIndex), previous, rebuilt);
        if (merged != pageText(pageIndex)) {
            setPageText(pageIndex, merged);
            emit resultChanged();
            emit editStateChanged();
        }
        return;
    }

    setPageText(pageIndex, rebuilt);
    // Rebase only: recognition and fix produce the page text themselves —
    // this must not read as a manual edit («Изменено»).
    const bool droppedEdit = m_editStore.reset(pageIndex, rebuilt);
    if (droppedEdit)
        m_pageModel.setEdited(pageIndex, false);
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

bool AppController::blockTextMapped() const
{
    return m_inspector.blockTextMapped();
}

QList<int> AppController::blockTextRange(int boxIndex) const
{
    return m_inspector.blockTextRange(boxIndex);
}

int AppController::boxIndexForTextPosition(int position) const
{
    return m_inspector.boxIndexForTextPosition(position);
}

void AppController::attachBlockTextHighlighter(QObject *textDocument, const QColor &color)
{
    m_inspector.attachBlockTextHighlighter(textDocument, color);
}

void AppController::setHighlightDifferences(bool enabled)
{
    m_inspector.setHighlightDifferences(enabled);
}

void AppController::setPageTextEditorFocused(bool focused)
{
    m_inspector.setPageTextEditorFocused(focused);
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
    // The «Изменено» label and the Revert button read this flag through
    // properties with different NOTIFY signals; without the broadcast the
    // label only refreshes when an unrelated availability change fires.
    emit editStateChanged();
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

void AppController::invalidatePageImage(int index)
{
    // A render that is still on its way belongs to the old picture: the
    // generation tells it to drop its result.
    ++m_previewGeneration;
    {
        QMutexLocker locker(&m_previewMutex);
        m_previewCache.remove(index);
    }
    if (m_previewRendering.load() == index) {
        m_previewRendering.store(-1);
        emit previewRenderingChanged();
    }
    {
        QMutexLocker locker(&m_thumbnailMutex);
        m_thumbnailCache.remove(index);
    }
    ++m_imageRevision;
    emit imageRevisionChanged();
}

void AppController::rotatePage(int degrees)
{
    if (m_recognition.busy() || m_layout.busy() || m_importing || checkBusy())
        return;
    if (!m_document.isValidIndex(m_currentPage))
        return;
    if (!turnPage(m_currentPage, degrees, true))
        return;

    const int step = normalizeRotation(degrees);
    setStatus(StatusMessage::translate("AppController", "Page %1 turned %2° clockwise.").arg(m_currentPage + 1).arg(step));
    notifyPageChanged();
}

bool AppController::turnPage(int index, int degrees, bool byUser)
{
    if (!m_document.isValidIndex(index))
        return false;

    int rotation = 0;
    {
        QWriteLocker locker(&m_documentLock);
        DocumentPage &page = m_document.page(index);
        rotation = normalizeRotation(page.rotation + degrees);
        m_document.setRotation(index, rotation);
        // The user has turned this page on purpose: the orientation check
        // leaves it alone from now on.
        if (byUser)
            page.userOriented = true;
        // The blocks were read from the old turn and say nothing about the new
        // one: the page goes back to the state before the recognition.
        page.result = OcrResult();
        page.recognized = false;
        page.recognitionMode = PageRecognitionMode::None;
        page.parseNote.clear();
    }

    m_editStore.reset(index, QString());
    m_pageModel.setRecognized(index, false);
    m_pageModel.setEdited(index, false);
    m_pageModel.setHasDuplicates(index, false);
    m_pageModel.setRecognitionMode(index, int(PageRecognitionMode::None));
    refreshPageVerification(index);
    if (index == m_currentPage)
        updateBoxesForCurrent();
    invalidatePageImage(index);
    ++m_cropRevision;
    updateAvailability();
    notifyPageChanged();
    return true;
}

bool AppController::runAfterOrientationCheck(const QList<int> &pages, const std::function<void()> &run)
{
    if (pages.isEmpty() || !m_settings.orientationCheckEnabled())
        return false;
    // The pages the user turned by hand keep their turn: the check does not
    // touch them.
    QList<int> unchecked;
    {
        QReadLocker locker(&m_documentLock);
        for (const int index : pages) {
            if (m_document.isValidIndex(index) && !m_document.page(index).userOriented)
                unchecked.append(index);
        }
    }
    if (unchecked.isEmpty())
        return false;

    m_pendingRun = run;
    m_orientationVerdicts.clear();
    m_orientation.check(unchecked, [this](int index) { return pageImage(index); }, [this](int index, const OrientationVerdict &verdict) { askModelAboutOrientation(index, verdict); });
    return true;
}

void AppController::askModelAboutOrientation(int index, const OrientationVerdict &verdict)
{
    // The picture has read the axis of the text: it names the turns the page
    // may need, and the model's answer is taken among them.
    m_orientationVerdicts.insert(index, verdict);
    m_orientationQueue.append(index);
    askNextOrientationPage();
}

void AppController::askNextOrientationPage()
{
    if (m_orientationPage >= 0 || m_orientationQueue.isEmpty())
        return;

    const ModelProfiles::PromptProfile *profile = ModelProfiles::promptProfile(QStringLiteral("orientation-requests"));
    if (!profile || profile->prompts.questions.isEmpty()) {
        // Without the request texts the model cannot be asked; the pages stay
        // as they are.
        while (!m_orientationQueue.isEmpty())
            m_orientation.settle(m_orientationQueue.takeFirst(), -1);
        return;
    }

    const int index = m_orientationQueue.takeFirst();
    const QImage image = pageImage(index);
    if (image.isNull()) {
        m_orientation.settle(index, -1);
        askNextOrientationPage();
        return;
    }

    m_orientationPage = index;
    m_orientationDecision.askChoice(image, QStringLiteral("page"), profile->prompts.statePrompt, profile->prompts.questions);
}

void AppController::onOrientationVerdict(const DecisionResult &result)
{
    if (m_orientationPage < 0)
        return;
    const int index = m_orientationPage;
    m_orientationPage = -1;

    int degrees = -1;
    if (result.ok) {
        // The picture has narrowed the turns the page may need; the model's
        // answer names the one it is surest of among them.
        const QList<int> candidates = orientationCandidates(m_orientationVerdicts.value(index));
        double best = -1.0;
        for (const int candidate : candidates) {
            const double probability = result.diagnosis.value(QString::number(candidate), 0.0);
            if (probability > best) {
                best = probability;
                degrees = candidate;
            }
        }
        // A page is not turned on an uncertain answer, and a quarter turn
        // needs much more certainty than a half turn.
        const double threshold = (degrees == 90 || degrees == 270) ? kQuarterTurnConfidence : kOrientationConfidence;
        if (best < threshold)
            degrees = -1;
    }
    m_orientation.settle(index, degrees);
    askNextOrientationPage();
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

}  // namespace llocr
