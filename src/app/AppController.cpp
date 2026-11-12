#include "app/AppController.h"
#include "app/AutoPipelineController.h"
#include "app/AutoPipelineStages.h"
#include "app/LruImageCache.h"
#include "app/PageIndex.h"
#include "app/PageRotation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <QBuffer>
#include <QClipboard>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QMutex>
#include <QPointer>
#include <QQuickTextDocument>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QStringView>
#include <QtConcurrent/QtConcurrentRun>
#include <QThread>
#include <QTimer>
#include <QVariant>

#include "parsers/BlockRender.h"
#include "parsers/BlockStyle.h"
#include "parsers/ParserFactory.h"

#include "config/RequestProfileStore.h"
#include "core/BoxTransitions.h"
#include "core/LatexText.h"
#include "core/ModelProfiles.h"
#include "core/StatusMessage.h"
#include "models/OcrModel.h"

namespace {

constexpr int kPageTextCoalesceWindowMs = 300;

}  // namespace

namespace llocr {

AppController::AppController(SettingsStore &settings,
                             RuntimeController &runtime,
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
      m_workList({m_document, verification, checkRequestProfiles}),
      m_decisionQueue({m_document,
                       m_checkRunGate,
                       [this](int pageIndex, int boxIndex) { return croppedImage(pageIndex, boxIndex); },
                       runtime,
                       settings,
                       [this](int pageIndex, int boxIndex) { return blockDisplayText(pageIndex, boxIndex); },
                       [this](const QString &candidateText, const std::function<void(bool, const QImage &, const QString &)> &done) { m_candidateRenderFn(candidateText, done); }},
                      this),
      m_recheckQueue({m_document, m_checkRunGate, [this](int pageIndex, int boxIndex) { return croppedImage(pageIndex, boxIndex); }, runtime, checkRequestProfiles}, this),
      m_remedyQueue({m_document, m_checkRunGate, [this](int pageIndex, int boxIndex) { return croppedImage(pageIndex, boxIndex); }, runtime, settings, fixRequestProfiles}, this),
      m_candidateRenderFn([this](const QString &candidateText, const std::function<void(bool, const QImage &, const QString &)> &done) { m_candidateRenderer.render(candidateText, done); }),
      m_layout(layoutRequestProfiles, runtime, this), m_availability({m_document,
                                                                      m_pageModel,
                                                                      m_workList,
                                                                      runtime,
                                                                      verification,
                                                                      [this] { return m_currentPage; },
                                                                      [this] { return m_importing; },
                                                                      [this] { return m_layout.busy(); },
                                                                      [this] { return checkBusy(); },
                                                                      [this] { return m_autoPipeline && m_autoPipeline->isRunning(); }},
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
                 [this] { return m_layout.busy() || m_importing || m_export.exporting() || m_decisionQueue.busy() || m_recheckQueue.busy() || m_remedyQueue.busy(); },
                 [this](const StatusMessage &message) { setStatus(message); },
                 [this](const StatusMessage &message, ProblemLog::Severity severity) { reportProblem(message, severity); },
                 [this] { m_inspector.resetSelection(); },
                 [this] { refreshAllPageVerifications(); },
                 [this] { ensureDefaultSelection(); },
                 [this] { updateBoxesForCurrent(); },
                 [this] { notifyDocumentChanged(); }},
                this)
{
    // The gate is the one-run-at-a-time rule across the decision queue and
    // the recheck/remedy runs: the markup pass and a stopped request still
    // winding down hold it from the outside.
    m_checkRunGate.setExternalBusy([this] { return m_layout.busy() || m_decisionQueue.requestBusy() || m_recheckQueue.requestBusy() || m_remedyQueue.requestBusy(); });

    // The orientation check: the picture reads the axis of the text before the
    // page is recognized. A page it cannot settle is marked and left as it
    // is — the user turns it by hand.
    connect(&m_orientation, &OrientationController::skewed, this, &AppController::applyPageDeskew);
    connect(&m_orientation, &OrientationController::sideways, this, [this](int index) {
        setPageOrientationProblem(index, PageOrientationProblem::Sideways);
        setStatus(StatusMessage::translate("AppController", "Page %1 lies on its side. The check cannot tell which way it has to be turned — turn it by hand.").arg(index + 1));
    });
    connect(&m_orientation, &OrientationController::unclear, this, [this](int index) {
        setPageOrientationProblem(index, PageOrientationProblem::Unclear);
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

    // The inspector owns the selected block: its signals are the facade's.
    connect(&m_inspector, &BlockInspector::selectedBoxChanged, this, &AppController::selectedBoxChanged);
    connect(&m_inspector, &BlockInspector::selectedBoxChanged, this, &AppController::blockDocumentDivergedChanged);
    connect(&m_inspector, &BlockInspector::blockTextRangeChanged, this, &AppController::blockTextRangeChanged);
    connect(this, &AppController::resultChanged, this, &AppController::blockDocumentDivergedChanged);
    connect(&m_inspector, &BlockInspector::blockDiffChanged, this, &AppController::blockDiffChanged);
    connect(&m_inspector, &BlockInspector::highlightDifferencesChanged, this, &AppController::highlightDifferencesChanged);
    connect(&m_inspector, &BlockInspector::pageTextEditorFocusedChanged, this, &AppController::pageTextEditorFocusedChanged);

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

    // The three queues share the wiring; each role's results travel on its
    // own channel and land on their apply* handlers.
    wireBlockQueue(m_decisionQueue);
    wireBlockQueue(m_recheckQueue);
    wireBlockQueue(m_remedyQueue);
    connect(&m_decisionQueue, &DecisionQueue::verificationStarted, this, &AppController::applyVerificationStarted);
    connect(&m_decisionQueue, &DecisionQueue::blockVerified, this, &AppController::applyVerdictToBox);
    connect(&m_recheckQueue, &RecheckQueue::blockRecognized, this, &AppController::applyRecognitionToBox);
    connect(&m_remedyQueue, &RemedyQueue::blockFixed, this, &AppController::applyFixToBox);
    connect(&m_remedyQueue, &RemedyQueue::blockRead, this, &AppController::applyReadToBox);

    // The queue's per-block tail (page-text rebuild + its signal fan-out) and
    // the availability rescan are coalesced while a run is active; the run's
    // own state changes carry the guaranteed flush.
    m_pageTextCoalesceTimer.setSingleShot(true);
    m_pageTextCoalesceTimer.setInterval(kPageTextCoalesceWindowMs);
    connect(&m_pageTextCoalesceTimer, &QTimer::timeout, this, &AppController::flushPageTextChanges);
    connect(&m_pageTextCoalesceTimer, &QTimer::timeout, this, &AppController::updateAvailability);
    connect(&m_layout, &LayoutController::busyChanged, this, &AppController::flushPageTextChangesWhenIdle);

    connect(&m_availability, &AvailabilityPresenter::changed, this, &AppController::availabilityChanged);
    // The pipeline's availability is read off the matrix: the models, the
    // idle state and the page selection are the availability's business.
    connect(&m_availability, &AvailabilityPresenter::changed, this, &AppController::autoStateChanged);

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
    connect(&m_layout, &LayoutController::busyChanged, this, &AppController::busyChanged);
    connect(&m_layout, &LayoutController::busyChanged, this, &AppController::updateAvailability);
    // A page the markup auto-turned waits for its second pass: the boxes the
    // sideways layout read say nothing about the turned page. The pass is
    // over when the layout is idle and no page waits for the second one.
    connect(&m_layout, &LayoutController::busyChanged, this, [this]() {
        if (m_layout.busy())
            return;
        if (!m_remarkupPages.isEmpty()) {
            QList<int> pages = m_remarkupPages;
            m_remarkupPages.clear();
            startLayout(pages);
            return;
        }
        finishMarkupPass(!m_markupPassMarkedPages.isEmpty());
    });
    connect(&m_layout, &LayoutController::statusRequested, this, [this](const StatusMessage &message) { setStatus(message); });
    connect(&m_layout, &LayoutController::progressChanged, this, &AppController::layoutProgressChanged);
    connect(&m_layout, &LayoutController::layoutFinished, this, [this](int pageIndex, const QString &rawText) {
        // The pages the markup pass really marked up: a reply without boxes
        // leaves the page without blocks, and a pass that marked no page did
        // nothing.
        if (applyLayout(pageIndex, rawText) && m_markupPassDone)
            m_markupPassMarkedPages.insert(pageIndex);
    });
    connect(&m_settings, &SettingsStore::layoutRequestProfileIdChanged, this, &AppController::layoutAvailableChanged);
    // The layout role of the request profile is one of the auto pipeline's
    // gates: the button's state has to follow the role's appearance and
    // disappearance, and this signal is the only one that announces it.
    connect(&m_settings, &SettingsStore::layoutRequestProfileIdChanged, this, [this]() { emit autoStateChanged(); });

    // The auto pipeline (ADR 257): the stages in the order they run and the
    // orchestrator that owns the run. The stage that needs a model says so
    // and the run passes it over. The markup stage takes the continue mode's
    // filter: the pages already marked are passed over, the recognition keeps
    // the full scope and fills what the marked pages still miss.
    m_autoMarkupStage = new AutoMarkupStage(
        m_layout,
        [this](const AutoPipelineStage::Scope &pages, const std::function<void(bool)> &done) { runMarkupPass(pages, done); },
        [this](const AutoPipelineStage::Scope &pages) {
            if (!m_autoSkipMarkedPages)
                return pages;
            AutoPipelineStage::Scope unmarked;
            for (const int page : pages) {
                if (!m_document.page(page).recognized)
                    unmarked.append(page);
            }
            return unmarked;
        },
        this);
    m_autoRecognitionStage = new AutoRecognitionStage(m_recheckQueue, m_workList, [this]() { return m_availability.blockRecognitionModelAvailable(); }, this);
    m_autoVerificationStage = new AutoVerificationStage(m_decisionQueue, m_workList, [this]() { return m_availability.decisionModelAvailable(); }, QStringLiteral("Verification"), this);
    m_autoFixStage = new AutoFixStage(m_remedyQueue, m_workList, [this]() { return m_availability.fixModelAvailable(); }, this);
    m_autoRecheckStage =
        new AutoVerificationStage(m_decisionQueue, m_workList, [this]() { return m_availability.decisionModelAvailable(); }, QStringLiteral("Verification of the corrected blocks"), this);

    AutoPipelineController::Deps deps;
    deps.scopePages = [this]() { return scopePages(); };
    deps.idle = [this]() { return m_availability.actionsIdle(); };
    m_autoPipeline = new AutoPipelineController(deps, this);
    m_autoPipeline->setStages({m_autoMarkupStage, m_autoRecognitionStage, m_autoVerificationStage, m_autoFixStage, m_autoRecheckStage});
    connect(m_autoPipeline, &AutoPipelineController::stateChanged, this, &AppController::autoStateChanged);
    connect(m_autoPipeline, &AutoPipelineController::stateChanged, this, &AppController::updateAvailability);
    connect(m_autoPipeline, &AutoPipelineController::statusRequested, this, [this](const StatusMessage &message) { setStatus(message); });
    // The run's outcome reaches the user: the button reverts, but the footer
    // says whether the run finished or was stopped. The caveats of the run
    // (the skipped stages and the failures) follow the outcome.
    connect(m_autoPipeline, &AutoPipelineController::finished, this, [this](bool ok) {
        const StatusMessage outcome = StatusMessage::translate("AppController", ok ? "The automatic pipeline is done." : "The automatic pipeline was stopped.");
        const StatusMessage &summary = m_autoPipeline->summary();
        const StatusMessage message = summary.isEmpty() ? outcome : StatusMessage::join({outcome, summary}, QStringLiteral(" "));
        setStatus(message);
        // The footer reads the run's line from its own channel: the shared
        // status message belongs to every task, and the check that follows
        // the run would shadow the outcome.
        setAutoPipelineMessage(message);
    });

    // The stage's progress moves without the pipeline changing its state: the
    // status line reads it live, so the notifier has to follow the progress.
    const auto autoProgressChanged = [this]() {
        if (m_autoPipeline->isRunning())
            emit autoStateChanged();
    };
    connect(&m_layout, &LayoutController::progressChanged, this, autoProgressChanged);
    connect(&m_decisionQueue, &BlockQueue::stateChanged, this, autoProgressChanged);
    connect(&m_recheckQueue, &BlockQueue::stateChanged, this, autoProgressChanged);
    connect(&m_remedyQueue, &BlockQueue::stateChanged, this, autoProgressChanged);

    connect(&m_export, &ExportController::exportingChanged, this, &AppController::exportingChanged);

    connect(&m_project, &ProjectController::busyChanged, this, &AppController::projectBusyChanged);
    connect(&m_project, &ProjectController::restored, this, &AppController::migrateLegacyProjectText);
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
    connect(&m_settings, &SettingsStore::keepHeadersAndPageNumbersChanged, &m_inspector, &BlockInspector::invalidateRebuildCache);
    connect(&m_settings, &SettingsStore::tablesAsHtmlChanged, &m_inspector, &BlockInspector::invalidateRebuildCache);
}

AppController::~AppController() = default;

void AppController::setCandidateRenderFunction(DecisionQueue::RenderCandidateFn render)
{
    m_candidateRenderFn = std::move(render);
}

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
    // The positioned blocks a page carries now come from the markup pass, and
    // the markup parser is the one that renders them back to text with the
    // per-label styles and the block↔text ranges (a raw transcription has no
    // geometry to map).
    return effectiveParserIdFor(m_settings.layoutRequestProfileId(), QStringLiteral("layout"));
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
    options.keepHeadersAndPageNumbers = m_settings.keepHeadersAndPageNumbers();
    options.tablesAsHtml = m_settings.tablesAsHtml();
    return options;
}

std::unique_ptr<IOutputParser> AppController::makeParser() const
{
    return ParserFactory::create(effectiveParserId(), parserOptions());
}

namespace {

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

// The turn the layout tokens voted for (ADR 245), as the controller accepts
// it: the parser already bars a split vote (nine tenths of the blocks that
// voted against up), the controller adds the page-level gates — the pixel
// check marked the page sideways, and the winning direction actually backs
// the page's layout (half the blocks or more). The measured reliability of
// the direction itself is far from the bar an automatic turn would need —
// the suggestion is one click, the user turns (ADR 242).
int suggestedTurnFromVote(const OcrPage &pageData, bool sidewaysMarked)
{
    if (!sidewaysMarked || pageData.orientationVote == 0 || pageData.orientationVoteShare < 50)
        return 0;
    // A quarter turn counts clockwise; a page whose blocks point right is
    // levelled by one turn to the left, and that is how the UI names it.
    const int turn = pageData.orientationVote;
    return turn == 270 ? -90 : turn;
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

int AppController::currentPageOrientationProblem() const
{
    QReadLocker locker(&m_documentLock);
    if (!m_document.isValidIndex(m_currentPage))
        return int(PageOrientationProblem::None);
    return int(m_document.page(m_currentPage).orientationProblem);
}

double AppController::currentPageSkewDegrees() const
{
    QReadLocker locker(&m_documentLock);
    if (!m_document.isValidIndex(m_currentPage))
        return 0.0;
    return m_document.page(m_currentPage).skewDegrees;
}

int AppController::currentPageSuggestedTurn() const
{
    QReadLocker locker(&m_documentLock);
    if (!m_document.isValidIndex(m_currentPage))
        return 0;
    return m_document.page(m_currentPage).suggestedTurn;
}

int AppController::currentPageRotation() const
{
    QReadLocker locker(&m_documentLock);
    if (!m_document.isValidIndex(m_currentPage))
        return 0;
    return m_document.page(m_currentPage).rotation;
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
    return pageUserEdited(m_currentPage);
}

bool AppController::pageUserEdited(int index) const
{
    QReadLocker locker(&m_documentLock);
    if (!m_document.isValidIndex(index) || m_document.page(index).result.pages.isEmpty())
        return false;
    for (const BoundingBox &box : m_document.page(index).result.pages.first().boxes)
        if (!box.userText.isEmpty())
            return true;
    return false;
}

// The document is the join of the blocks (ADR 296): the page text is derived,
// never written by hand.
void AppController::refreshPageDocument(int pageIndex)
{
    QString joined;
    bool edited = false;
    {
        QReadLocker locker(&m_documentLock);
        if (!m_document.isValidIndex(pageIndex) || m_document.page(pageIndex).result.pages.isEmpty())
            return;
        joined = rebuildPageText(m_document.page(pageIndex).result.pages[0]);
        for (const BoundingBox &box : m_document.page(pageIndex).result.pages.first().boxes)
            edited = edited || !box.userText.isEmpty();
    }
    setPageText(pageIndex, joined);
    m_pageModel.setEdited(pageIndex, edited);
    m_editStore.setEdited(pageIndex, edited);
    emit resultChanged();
    emit editStateChanged();
    emit blockDocumentDivergedChanged();
}

// The editor's pull speaks data: URIs for the crops; the model stores the
// compact image://ocr/crop references.
QString AppController::decodeEditorMarkdown(const QString &text) const
{
    QString decoded = text;
    for (const auto &ref : m_editorCropRefs)
        decoded.replace(ref.first, ref.second);
    return decoded;
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

    // The image provider calls in from an image-loader worker; the watcher
    // and the previewRenderingChanged signal below are GUI-thread property.
    // The queued start lands after the event loop turn, and the provider
    // serves the thumbnail fallback meanwhile.
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, index]() { previewImage(index); }, Qt::QueuedConnection);
        return {};
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

QString AppController::blockDisplayText(int pageIndex, int boxIndex)
{
    QReadLocker locker(&m_documentLock);
    if (!m_document.isValidIndex(pageIndex))
        return {};
    const DocumentPage &page = m_document.page(pageIndex);
    if (page.result.pages.isEmpty())
        return {};
    const QList<BoundingBox> &boxes = page.result.pages[0].boxes;
    if (boxIndex < 0 || boxIndex >= boxes.size())
        return {};
    const BoundingBox &box = boxes.at(boxIndex);
    const ParserOptions options = parserOptions();
    return renderBlockText(BoxTransitions::effectiveText(box), blockStyleForLabel(box.label, options.modelId), options);
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
    if (m_layout.busy() || m_importing || m_export.exporting())
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
    if (m_layout.busy() || m_importing || checkBusy())
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
    if (m_layout.busy() || m_importing || checkBusy())
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

void AppController::setUserBlockText(int segmentIndex, const QString &markdown)
{
    QWriteLocker locker(&m_documentLock);
    if (!m_document.isValidIndex(m_currentPage) || m_document.page(m_currentPage).result.pages.isEmpty())
        return;
    QList<BoundingBox> &boxes = m_document.page(m_currentPage).result.pages[0].boxes;
    // The index is the ordinal of the page's top-level markdown blocks — the
    // same order the editor's DOM blocks are tagged with. A model fragment
    // may span several of them; the layer replaces the box's whole fragment,
    // its segments with the edited one taken from the editor. Returning every
    // segment to its canonical text dissolves the layer again.
    const ParserOptions options = parserOptions();
    const auto document = renderPageText(m_document.page(m_currentPage).result.pages.first(), options);
    const QVariantList map = blockBoxMap(document.text, document.ranges);
    if (segmentIndex < 0 || segmentIndex >= map.size())
        return;
    const int boxIndex = map.at(segmentIndex).toInt();
    if (boxIndex < 0 || boxIndex >= boxes.size())
        return;
    const QVariantList segments = blockSegments(document.text);
    QString layer;
    for (int i = 0; i < map.size() && i < segments.size(); ++i) {
        if (map.at(i).toInt() != boxIndex)
            continue;
        if (!layer.isEmpty())
            layer += QStringLiteral("\n\n");
        layer += (i == segmentIndex) ? decodeEditorMarkdown(markdown) : segments.at(i).toString();
    }
    // The layer releases when the markdown equals the canonical render (ADR
    // 296) — not the raw text: a heading's canonical markdown is its "# "
    // form, a table's its rendered markdown, so the effective text compares
    // equal only for plain blocks.
    const BoundingBox &box = boxes.at(boxIndex);
    BlockStyleInfo style = blockStyleForLabel(box.label, options.modelId);
    if (style.style == BlockStyle::ImagePlaceholder)
        style.imageIndex = boxIndex;
    if (layer == renderBlockText(BoxTransitions::effectiveText(box), style, options))
        boxes[boxIndex].userText.clear();
    else
        BoxTransitions::setUserText(boxes[boxIndex], layer);
    locker.unlock();
    refreshPageDocument(m_currentPage);
}

void AppController::releaseSelectedBlockUserText()
{
    releaseBlockUserText(m_inspector.selectedBoxIndex());
}

void AppController::releaseBlockUserText(int boxIndex)
{
    {
        QWriteLocker locker(&m_documentLock);
        if (boxIndex < 0 || !m_document.isValidIndex(m_currentPage) || m_document.page(m_currentPage).result.pages.isEmpty())
            return;
        QList<BoundingBox> &boxes = m_document.page(m_currentPage).result.pages[0].boxes;
        if (boxIndex >= boxes.size())
            return;
        boxes[boxIndex].userText.clear();
    }
    refreshPageDocument(m_currentPage);
    setStatus(StatusMessage::translate("AppController", "The block text was returned to the document."));
}

bool AppController::selectedBlockDocumentDiverged() const
{
    const BoundingBox *box = m_inspector.selectedBox();
    return box && !box->userText.isEmpty();
}

void AppController::revertCurrentPageEdits()
{
    const int index = m_currentPage;
    {
        QWriteLocker locker(&m_documentLock);
        if (!m_document.isValidIndex(index) || m_document.page(index).result.pages.isEmpty())
            return;
        for (BoundingBox &box : m_document.page(index).result.pages[0].boxes)
            box.userText.clear();
    }
    refreshPageDocument(index);
}

void AppController::onBoxRectChanged(int boxIndex, qreal x, qreal y, qreal width, qreal height)
{
    bool contentDropped = false;
    BoundingBox updated;
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

        BoundingBox &box = boxes[boxIndex];
        box.rect = QRectF(x, y, width, height);
        // The text was recognized for the region the block used to cover: it
        // says nothing about this piece of the page any more (ADR 241).
        contentDropped = BoxTransitions::blockRectChanged(box);
        updated = box;
        ++m_cropRevision;
    }

    m_boxModel.updateBoxRect(boxIndex, x, y, width, height);
    m_boxModel.updateBoxState(boxIndex, updated);
    if (m_inspector.selectedBoxIndex() == boxIndex)
        m_inspector.blockStateChanged();
    emit imageChanged();
    if (!contentDropped)
        return;

    // The page text followed the blocks: the emptied block takes its text with
    // it. The drag calls this on every mouse move, so the expensive part runs
    // once — when the block still carried something.
    refreshRecognitionModeAfterBlockText(m_currentPage);
    refreshPageVerification(m_currentPage);
    // The emptied block's user layer dies with its content (ADR 296).
    refreshPageDocument(m_currentPage);
}

bool AppController::removeBlock(int boxIndex)
{
    if (m_layout.busy() || m_importing || checkBusy())
        return false;
    BoundingBox removed;
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

        removed = boxes.at(boxIndex);
        boxes.removeAt(boxIndex);
        ++m_cropRevision;
    }
    m_freeSlots[m_currentPage] = slotsAfterRemoval(m_freeSlots.value(m_currentPage), boxIndex, removed.rect);
    m_boxModel.removeBox(boxIndex);
    // The removed block could have been the one holding the page back: both
    // the aggregate and the markup-only completion are derived from the set.
    refreshRecognitionModeAfterBlockText(m_currentPage);
    refreshPageVerification(m_currentPage);

    refreshPageDocument(m_currentPage);

    if (m_inspector.selectedBoxIndex() == boxIndex)
        setSelectedBoxIndex(-1);
    else if (m_inspector.selectedBoxIndex() > boxIndex)
        setSelectedBoxIndex(m_inspector.selectedBoxIndex() - 1);

    emit resultChanged();
    emit editStateChanged();
    return true;
}

bool AppController::moveBlock(int from, int to)
{
    if (m_layout.busy() || m_importing || checkBusy())
        return false;
    {
        QWriteLocker locker(&m_documentLock);
        if (!m_document.isValidIndex(m_currentPage))
            return false;
        DocumentPage &page = m_document.page(m_currentPage);
        if (!page.recognized || page.result.pages.isEmpty())
            return false;
        QList<BoundingBox> &boxes = page.result.pages[0].boxes;
        if (from < 0 || from >= boxes.size() || to < 0 || to >= boxes.size() || from == to)
            return false;
        boxes.move(from, to);
    }
    m_freeSlots[m_currentPage] = slotsAfterMove(m_freeSlots.value(m_currentPage), from, to);
    m_boxModel.moveBox(from, to);
    refreshPageDocument(m_currentPage);

    if (m_inspector.selectedBoxIndex() == from)
        setSelectedBoxIndex(to);
    else {
        const int low = qMin(from, to);
        const int high = qMax(from, to);
        const int selected = m_inspector.selectedBoxIndex();
        if (selected >= low && selected <= high)
            setSelectedBoxIndex(selected + (to > from ? -1 : 1));
    }
    return true;
}

bool AppController::addBlock(const QString &type)
{
    return addBlockAt(type, 0.35, 0.45, 0.3, 0.08);
}

bool AppController::addBlockAt(const QString &type, qreal x, qreal y, qreal width, qreal height)
{
    if (m_layout.busy() || m_importing || checkBusy())
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
    // The block takes the place its position on the page gives it (ADR 310):
    // the page text follows the order of the blocks.
    const QList<FreeSlot> freed = m_freeSlots.value(m_currentPage);
    Placement placement;
    int boxCount = 0;
    {
        QWriteLocker locker(&m_documentLock);
        DocumentPage &page = m_document.page(m_currentPage);
        if (page.result.pages.isEmpty())
            page.result.pages.append(OcrPage());
        QList<BoundingBox> &boxes = page.result.pages[0].boxes;
        boxCount = boxes.size();
        placement = placementFor(boxes, box.rect, freed);
        boxes.insert(placement.index, box);
        page.recognized = true;
        ++m_cropRevision;
    }
    m_freeSlots[m_currentPage] = slotsAfterPlacement(freed, placement);

    m_pageModel.setRecognized(m_currentPage, true);
    if (m_document.page(m_currentPage).recognitionMode == PageRecognitionMode::None)
        setRecognitionMode(m_currentPage, PageRecognitionMode::MarkupOnly);

    if (m_boxModel.rowCount() == boxCount)
        m_boxModel.insertBox(placement.index, box);
    else
        updateBoxesForCurrent();

    refreshPageDocument(m_currentPage);

    refreshPageVerification(m_currentPage);
    setSelectedBoxIndex(placement.index);

    setStatus(StatusMessage::translate("AppController", "Block added."));
    emit resultChanged();
    return true;
}

void AppController::checkSelectedBlock()
{
    if (m_inspector.selectedBoxIndex() < 0 || !blockQueueTaskReady(m_currentPage))
        return;
    m_decisionQueue.run({{m_currentPage, m_inspector.selectedBoxIndex()}});
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

void AppController::checkEnabledBlocksOnPage()
{
    if (!blockQueueTaskReady(m_currentPage))
        return;
    int answered = 0;
    int rejected = 0;
    const QList<BlockWorkList::Task> tasks = m_workList.enabledBoxesOnPage(m_currentPage, &answered, &rejected);
    startVerificationOrReport(tasks, answered, rejected);
}

void AppController::checkAllEnabledBlocks()
{
    // The old controller refused silently before collecting; the report below
    // is for a run that was allowed to look at the document.
    if (m_layout.busy() || checkBusy())
        return;
    QList<int> pages;
    for (int p = 0; p < m_document.pageCount(); ++p)
        pages.append(p);
    int answered = 0;
    int rejected = 0;
    startVerificationOrReport(collectEnabledWithCounts(pages, &answered, &rejected), answered, rejected);
}

void AppController::recognizeScopeBlocks(RerunMode mode)
{
    if (m_layout.busy())
        return;
    const QList<BlockWorkList::Task> tasks = mode == RerunMode::RedoAll ? m_workList.reRecognizableBlocks(scopePages()) : m_workList.unrecognizedBlocks(scopePages());
    if (tasks.isEmpty()) {
        setStatus(StatusMessage::translate("AppController", "Nothing to recognize: the blocks in scope already carry text"));
        return;
    }
    m_recheckQueue.run(tasks);
}

void AppController::layoutScopePages(RerunMode mode)
{
    QList<int> pages = scopePages();
    if (mode == RerunMode::SkipDone) {
        QList<int> unmarked;
        for (const int page : pages) {
            if (!m_document.page(page).recognized)
                unmarked.append(page);
        }
        if (unmarked.isEmpty()) {
            setStatus(StatusMessage::translate("AppController", "Nothing to mark up: the pages in scope already carry markup"));
            return;
        }
        pages = unmarked;
    }
    m_autoTurnedPages.clear();
    startLayout(pages);
}

void AppController::checkScopeEnabledBlocks(RerunMode mode)
{
    if (m_layout.busy() || checkBusy())
        return;
    if (mode == RerunMode::RedoAll) {
        const QList<BlockWorkList::Task> tasks = m_workList.recheckableBlocks(scopePages());
        if (tasks.isEmpty()) {
            setStatus(StatusMessage::translate("AppController", "Nothing to check: the blocks in scope already carry a verdict"));
            return;
        }
        m_decisionQueue.run(tasks);
        return;
    }
    int answered = 0;
    int rejected = 0;
    startVerificationOrReport(collectEnabledWithCounts(scopePages(), &answered, &rejected), answered, rejected);
}

void AppController::fixScopeProblemBlocks(RerunMode mode)
{
    if (m_layout.busy() || checkBusy())
        return;
    if (mode == RerunMode::RedoAll) {
        // The redo is a clean slate: the stored correction is erased before
        // the request is built, so the fixer judges the block's own text —
        // the hook runs inside the queue, ahead of the candidate readout.
        const QList<BlockWorkList::Task> tasks = m_workList.refixableBlocks(scopePages());
        if (tasks.isEmpty()) {
            setStatus(StatusMessage::translate("AppController", "Nothing to fix: the blocks in scope already carry a remedy answer"));
            return;
        }
        m_remedyQueue.runFix(tasks, [this](const BlockWorkList::Task &task) {
            applyBoxTransition(task.page, task.box, [](BoundingBox &box) {
                if (box.fixStatus != BoxFixStatus::None)
                    BoxTransitions::correctionReverted(box);
            });
        });
        return;
    }
    const QList<BlockWorkList::Task> tasks = m_workList.fixableBlocks(scopePages());
    if (tasks.isEmpty()) {
        setStatus(StatusMessage::translate("AppController", "Nothing to fix: the blocks in scope already carry a remedy answer"));
        return;
    }
    m_remedyQueue.runFix(tasks);
}

void AppController::readScopeBlocks(RerunMode mode)
{
    if (m_layout.busy() || checkBusy())
        return;
    if (mode == RerunMode::RedoAll) {
        const QList<BlockWorkList::Task> tasks = m_workList.readableBlocks(scopePages());
        if (tasks.isEmpty()) {
            setStatus(StatusMessage::translate("AppController", "Nothing to re-read: the blocks in scope already carry the reader's answer"));
            return;
        }
        m_remedyQueue.runRead(tasks, [this](const BlockWorkList::Task &task) {
            applyBoxTransition(task.page, task.box, [](BoundingBox &box) {
                if (box.fixStatus != BoxFixStatus::None)
                    BoxTransitions::correctionReverted(box);
            });
        });
        return;
    }
    const QList<BlockWorkList::Task> tasks = m_workList.notReadBlocks(scopePages());
    if (tasks.isEmpty()) {
        setStatus(StatusMessage::translate("AppController", "Nothing to re-read: the blocks in scope already carry the reader's answer"));
        return;
    }
    m_remedyQueue.runRead(tasks);
}

void AppController::stopCheck()
{
    m_decisionQueue.stop();
    m_recheckQueue.stop();
    m_remedyQueue.stop();
}

QList<BlockWorkList::Task> AppController::collectEnabledWithCounts(const QList<int> &pages, int *answered, int *rejected) const
{
    QList<BlockWorkList::Task> tasks;
    for (const int p : pages) {
        int pageAnswered = 0;
        int pageRejected = 0;
        tasks.append(m_workList.enabledBoxesOnPage(p, &pageAnswered, &pageRejected));
        if (answered)
            *answered += pageAnswered;
        if (rejected)
            *rejected += pageRejected;
    }
    return tasks;
}

void AppController::startVerificationOrReport(const QList<BlockWorkList::Task> &tasks, int answered, int rejected)
{
    if (!tasks.isEmpty()) {
        m_decisionQueue.run(tasks);
        return;
    }
    if (answered == 0 && rejected == 0)
        return;
    // A formula the app rejected on its LaTeX syntax is named apart from the
    // blocks that were already verified: it was not asked about (ADR 194).
    QList<StatusMessage> parts;
    parts.append(StatusMessage::translate("AppController", "Nothing to check:"));
    if (answered > 0)
        parts.append(StatusMessage::translate("AppController", "%1 block(s) already verified").arg(answered));
    if (rejected > 0)
        parts.append(StatusMessage::translate("AppController", "%1 formula(s) rejected: invalid LaTeX syntax").arg(rejected));
    setStatus(StatusMessage::join(parts, QStringLiteral(" ")));
}

bool AppController::blockQueueTaskReady(int pageIndex) const
{
    return m_document.isValidIndex(pageIndex) && m_document.page(pageIndex).recognized;
}

void AppController::wireBlockQueue(BlockQueue &queue)
{
    // The lambdas outlive this call, so they hold the queue by address —
    // a member of this controller, whose connections die with it.
    BlockQueue *const q = &queue;
    connect(&queue, &BlockQueue::stateChanged, this, &AppController::checkStateChanged);
    // busy() is the layout pass or a queue running: both state changes must
    // re-announce it to the QML bindings.
    connect(&queue, &BlockQueue::stateChanged, this, &AppController::busyChanged);
    // The queue's stateChanged fires per request and the rescan it drives is
    // O(pages×blocks) — it rides the coalescing window while a run is active.
    connect(&queue, &BlockQueue::stateChanged, this, &AppController::scheduleAvailabilityRefresh);
    connect(&queue, &BlockQueue::stateChanged, this, [q, this]() {
        if (q->active())
            setAutoPipelineMessage(StatusMessage());
    });
    connect(&queue, &BlockQueue::stateChanged, this, &AppController::flushPageTextChangesWhenIdle);
    // A new check run takes the footer line: the outcome of the last run is
    // over, and the check that follows it must not read as a finished run.
    // The run's readouts reset when a run starts, not on the per-request
    // state changes that follow; the readout's progress source becomes this
    // queue. The run's own end sets the finished flag; a stop does not (the
    // stopped run reads as not finished, as before).
    connect(&queue, &BlockQueue::runStarted, this, [q, this]() {
        setAutoPipelineMessage(StatusMessage());
        m_checkError = StatusMessage();
        m_checkFinished = false;
        m_progressQueue = q;
    });
    connect(&queue, &BlockQueue::finished, this, [q, this]() {
        if (!q->active())
            m_checkFinished = q->finishedFlag();
    });
    connect(&queue, &BlockQueue::statusRequested, this, [this](const StatusMessage &message) { setStatus(message); });
    connect(&queue, &BlockQueue::problemReported, this, [this](const StatusMessage &message) {
        m_checkError = message;
        reportProblem(message, ProblemLog::Error);
    });
}

int AppController::checkProgressDone() const
{
    return m_progressQueue ? m_progressQueue->progressDone() : 0;
}

int AppController::checkProgressTotal() const
{
    return m_progressQueue ? m_progressQueue->progressTotal() : 0;
}

void AppController::recognizeSelectedBlock()
{
    if (m_inspector.selectedBoxIndex() < 0 || !blockQueueTaskReady(m_currentPage))
        return;
    m_recheckQueue.run({{m_currentPage, m_inspector.selectedBoxIndex()}});
}

void AppController::recognizeProblemBlocksOnPage()
{
    if (!blockQueueTaskReady(m_currentPage))
        return;
    m_recheckQueue.run(m_workList.problemBlocks({m_currentPage}));
}

void AppController::recognizeAllProblemBlocks()
{
    if (m_layout.busy() || checkBusy())
        return;
    QList<int> pages;
    for (int p = 0; p < m_document.pageCount(); ++p)
        pages.append(p);
    m_recheckQueue.run(m_workList.problemBlocks(pages));
}

void AppController::recognizeAllBlocksOnPage()
{
    if (m_layout.busy() || !blockQueueTaskReady(m_currentPage))
        return;
    m_recheckQueue.run(m_workList.unrecognizedBlocks({m_currentPage}));
}

void AppController::fixSelectedBlock()
{
    if (m_inspector.selectedBoxIndex() < 0 || !blockQueueTaskReady(m_currentPage))
        return;
    m_remedyQueue.runFix({{m_currentPage, m_inspector.selectedBoxIndex()}});
}

void AppController::readSelectedBlock()
{
    if (m_inspector.selectedBoxIndex() < 0 || !blockQueueTaskReady(m_currentPage))
        return;
    m_remedyQueue.runRead({{m_currentPage, m_inspector.selectedBoxIndex()}});
}

void AppController::fixAllProblemBlocks()
{
    if (m_layout.busy() || checkBusy())
        return;
    QList<int> pages;
    for (int p = 0; p < m_document.pageCount(); ++p)
        pages.append(p);
    m_remedyQueue.runFix(m_workList.fixableBlocks(pages));
}

void AppController::layoutCurrentPage()
{
    m_autoTurnedPages.clear();
    startLayout({m_currentPage});
}

void AppController::layoutAllPages()
{
    m_autoTurnedPages.clear();
    QList<int> pages;
    for (int i = 0; i < m_document.pageCount(); ++i)
        pages.append(i);
    startLayout(pages);
}

void AppController::stopLayout()
{
    m_layout.stop();
}

bool AppController::autoPipelineRunning() const
{
    return m_autoPipeline->isRunning();
}

bool AppController::autoPipelineAvailable() const
{
    if (!m_autoPipeline->canStart())
        return false;
    // The gates the stages use: the markup needs the layout role of the
    // request profile and a model to serve it, the recognition its own model.
    // The button must not promise a run whose first stage is passed over.
    return m_layout.available() && m_availability.layoutModelAvailable() && m_availability.blockRecognitionModelAvailable();
}

QString AppController::autoPipelineBlockedReason() const
{
    // The gate the facade adds on top of the orchestrator's own: the run needs
    // a model for the markup and one for the recognition. It comes first,
    // because without the models the run would not work at all.
    if (!m_availability.layoutModelAvailable() || !m_availability.blockRecognitionModelAvailable())
        return StatusMessage::translate("AutoPipelineController", "No model for the markup or the recognition task").text();
    // The markup needs the layout role of the request profile: without it the
    // first stage is passed over and the run does nothing.
    if (!m_layout.available())
        return StatusMessage::translate("AutoPipelineController", "The markup model does not support page markup").text();
    return m_autoPipeline->blockedReason();
}

int AppController::autoPipelineStageIndex() const
{
    return m_autoPipeline->stageIndex();
}

int AppController::autoPipelineStageCount() const
{
    return m_autoPipeline->stageCount();
}

QString AppController::autoPipelineStageTitle() const
{
    return m_autoPipeline->stageTitle();
}

int AppController::autoPipelineProgressDone() const
{
    return m_autoPipeline->progressDone();
}

int AppController::autoPipelineProgressTotal() const
{
    return m_autoPipeline->progressTotal();
}

void AppController::startAutoPipeline(RerunMode mode)
{
    m_autoSkipMarkedPages = mode == RerunMode::SkipDone;
    m_autoPipeline->start();
}

void AppController::stopAutoPipeline()
{
    m_autoPipeline->stop();
}

void AppController::runMarkupPass(const QList<int> &pages, const std::function<void(bool)> &done)
{
    m_markupPassDone = done;
    m_markupPassMarkedPages.clear();
    startLayout(pages);
}

void AppController::finishMarkupPass(bool ok)
{
    if (!m_markupPassDone)
        return;
    const auto done = m_markupPassDone;
    m_markupPassDone = nullptr;
    done(ok);
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
    if (pages.isEmpty() || m_layout.busy() || m_importing || checkBusy()) {
        finishMarkupPass(false);
        return;
    }
    const ModelProfiles::Role *role = m_layout.role();
    if (!role) {
        setStatus(StatusMessage::translate("AppController", "The layout model does not support page markup."));
        finishMarkupPass(false);
        return;
    }
    const QString prompt = role->prompts.isEmpty() ? QString() : role->prompts.constFirst().text;
    if (prompt.isEmpty()) {
        setStatus(StatusMessage::translate("AppController", "The model profile has no markup prompt."));
        finishMarkupPass(false);
        return;
    }
    // The markup model reads the page too: the page is turned first when it
    // lies the wrong way up.
    const auto start = [this, pages, role, prompt]() {
        // The layout refuses a second pass in silence, and a pass that never
        // starts never ends: the stage would wait for it forever. Whatever
        // took the layout meanwhile, the guard refuses the pass and reports
        // the failure at once instead of waiting on it.
        if (m_layout.busy()) {
            finishMarkupPass(false);
            return;
        }
        m_layout.layoutPages(pages, [this](int index) { return pageImage(index); }, role->systemPrompt, prompt);
    };
    if (runAfterOrientationCheck(pages, start))
        return;
    // The pass starts at once, so nothing waits for the orientation check
    // any more: a check still in flight carries the continuation of a pass
    // this one replaced, and its finished() must find nothing to start —
    // reporting that refusal would abort the pass that is running.
    m_pendingRun = nullptr;
    start();
}

bool AppController::applyLayout(int pageIndex, const QString &rawText)
{
    if (!m_document.isValidIndex(pageIndex) || rawText.isEmpty())
        return false;

    const ModelProfiles::Role *role = m_layout.role();
    if (!role || role->parser.isEmpty())
        return false;

    ParserOptions options;
    options.modelId = m_layoutRequestProfiles.activeProfileId();
    options.keepHeadersAndPageNumbers = true;
    m_boxModel.setStyleModelId(options.modelId);
    OcrResult parsed;
    if (auto parser = ParserFactory::create(role->parser, options)) {
        parsed = parser->parse(rawText);
        applyFormulaVerdicts(parsed);
        rebuildTextFromBlocks(parsed, *parser);
    }
    if (!parsed.success || parsed.pages.isEmpty())
        return false;

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
        return false;
    }

    const OcrPage &pageData = parsed.pages.first();
    const int blockCount = pageData.boxes.size();
    int suggestedTurn = 0;
    int autoTurn = 0;
    {
        QWriteLocker locker(&m_documentLock);
        DocumentPage &page = m_document.page(pageIndex);
        page.result = parsed;
        page.recognized = true;
        page.parseNote = parsed.notes.isEmpty() ? QString() : parsed.notes.join(QLatin1String(" "));
        const bool sidewaysMarked = page.orientationProblem == PageOrientationProblem::Sideways;
        suggestedTurn = suggestedTurnFromVote(pageData, sidewaysMarked);
        // The markup pass turns the page itself (ADR 246) when the model's
        // rotation tokens vote one way (the parser bars a split vote) and the
        // pixel check marked the page sideways: the direction is advised, the
        // axis is measured. Once per page — the second pass runs to read the
        // boxes from the turned page, and its vote is the model's word again,
        // not a reason to turn in circles.
        if (suggestedTurn != 0 && m_settings.orientationAutoTurnEnabled() && !m_autoTurnedPages.contains(pageIndex)) {
            autoTurn = suggestedTurn;
            m_autoTurnedPages.insert(pageIndex);
            m_remarkupPages.append(pageIndex);
        }
        page.suggestedTurn = autoTurn != 0 ? 0 : suggestedTurn;
    }
    // The layout replaced the page's blocks: the places the old ones left
    // behind say nothing about the new ones.
    m_freeSlots.remove(pageIndex);
    if (autoTurn != 0) {
        setStatus(StatusMessage::translate("AppController", "Page %1 lies on its side — turned by %2°, the markup runs again.").arg(pageIndex + 1).arg(autoTurn));
        turnPage(pageIndex, autoTurn, false);
        return true;
    }

    m_pageModel.setRecognized(pageIndex, true);
    setRecognitionMode(pageIndex, PageRecognitionMode::MarkupOnly);
    refreshPageVerification(pageIndex);
    if (pageData.hasDuplicates)
        m_pageModel.setHasDuplicates(pageIndex, true);

    refreshPageDocument(pageIndex);

    ++m_cropRevision;

    if (pageIndex == m_currentPage)
        updateBoxesForCurrent();

    setStatus(StatusMessage::translate("AppController", "Page %1 marked up: %2 block(s).").arg(pageIndex + 1).arg(blockCount));
    return true;
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
    schedulePageTextChanged(pageIndex);
}

void AppController::applyFixToBox(int pageIndex, int boxIndex, const CheckResult &result)
{
    applyRemedyToBox(pageIndex, boxIndex, result, CheckRequest::FixProtocol::FullBlock);
}

void AppController::applyReadToBox(int pageIndex, int boxIndex, const CheckResult &result)
{
    applyRemedyToBox(pageIndex, boxIndex, result, CheckRequest::FixProtocol::ReadBlock);
}

// The fix and the read land the same way: the correction layer takes the
// model's block, and the block's status says which protocol answered (ADR 260).
void AppController::applyRemedyToBox(int pageIndex, int boxIndex, const CheckResult &result, CheckRequest::FixProtocol protocol)
{
    // A failed request is not a fix: the block keeps its marks (and any
    // previous correction) until a later run retries it.
    if (result.status == CheckStatus::Failed)
        return;

    // A demoted FIX (diff-gate downgrade, EMPTY crop) still lands on the
    // block — the reason tells the user why nothing changed.
    if (!result.notes.isEmpty())
        setStatus(result.notes);

    const bool read = protocol == CheckRequest::FixProtocol::ReadBlock;
    bool textChanged = false;
    const bool applied = applyBoxTransition(pageIndex, boxIndex, [&result, &textChanged, read](BoundingBox &box) {
        const QString before = BoxTransitions::effectiveText(box);
        switch (result.status) {
        case CheckStatus::Ok:
            // The candidate is confirmed: it becomes the correction layer's
            // duplicate of the text (the fix can be reverted, an Ok verdict
            // cannot be told from an absent one).
            if (read)
                BoxTransitions::readConfirmedOriginal(box);
            else
                BoxTransitions::fixConfirmedOriginal(box);
            break;
        case CheckStatus::Fixed:
            if (read)
                BoxTransitions::readApplied(box, result.text);
            else
                BoxTransitions::fixApplied(box, result.text);
            break;
        case CheckStatus::Review:
            // The model refuses to judge: the block is marked damaged on the
            // correction layer, any previous correction stops being effective.
            if (read)
                BoxTransitions::readMarkedUnreadable(box);
            else
                BoxTransitions::fixMarkedUnreadable(box);
            break;
        case CheckStatus::Failed:
        case CheckStatus::Recognized:
        case CheckStatus::Mismatch:
            break;
        }
        textChanged = BoxTransitions::effectiveText(box) != before;
    });
    if (!applied)
        return;
    // A read that landed as the recognition (ADR 265) filled the page's blocks:
    // the page stops being markup-only.
    if (read)
        refreshRecognitionModeAfterBlockText(pageIndex);
    if (!textChanged)
        return;
    schedulePageTextChanged(pageIndex);
}

void AppController::applyPageTextChanged(int pageIndex)
{
    // The document is the join of the blocks (ADR 296): the user's layers are
    // already part of the rebuild, a landing block refreshes the page around
    // them, and the block whose recognition changed shows its AI-version
    // notice wherever a user layer now differs from the block.
    refreshPageDocument(pageIndex);
}

void AppController::schedulePageTextChanged(int pageIndex)
{
    if (!m_pendingPageTextPages.contains(pageIndex))
        m_pendingPageTextPages.append(pageIndex);
    if (!busy()) {
        flushPageTextChanges();
        return;
    }
    // Trailing-edge throttle: the timer is restarted only once it has fired,
    // so a steady stream of blocks still refreshes the page once per window.
    if (!m_pageTextCoalesceTimer.isActive())
        m_pageTextCoalesceTimer.start();
}

void AppController::flushPageTextChanges()
{
    m_pageTextCoalesceTimer.stop();
    if (m_pendingPageTextPages.isEmpty())
        return;
    const QList<int> pages = m_pendingPageTextPages;
    m_pendingPageTextPages.clear();
    for (const int pageIndex : pages)
        applyPageTextChanged(pageIndex);
}

void AppController::flushPageTextChangesWhenIdle()
{
    // The queue reports its last block while still active — the idle
    // transition lands one event-loop turn later, which a reader of the page
    // text must not have to wait for. A work list that just drained flushes
    // in the turn of its last block.
    const std::array<BlockQueue *, 3> queues = {&m_decisionQueue, &m_recheckQueue, &m_remedyQueue};
    for (BlockQueue *queue : queues) {
        if (queue->active() && queue->progressTotal() > 0 && queue->progressDone() >= queue->progressTotal()) {
            flushPageTextChanges();
            updateAvailability();
            return;
        }
    }
    if (!busy()) {
        flushPageTextChanges();
        updateAvailability();
    }
}

void AppController::scheduleAvailabilityRefresh()
{
    if (!busy()) {
        updateAvailability();
        return;
    }
    // Trailing-edge throttle, the same window as the page-text tail.
    if (!m_pageTextCoalesceTimer.isActive())
        m_pageTextCoalesceTimer.start();
}

bool AppController::exportPages(const QUrl &fileUrl, int scope, int fromPage, int toPage)
{
    // An export can start while a run is active (the toolbar gates it on the
    // queue only): the pages must not be collected from a coalesced text.
    flushPageTextChanges();
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
    // Another task took the line: the run's outcome is over.
    setAutoPipelineMessage(StatusMessage());
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusChanged();
}

void AppController::setAutoPipelineMessage(const StatusMessage &message)
{
    if (m_autoPipelineMessage == message)
        return;
    m_autoPipelineMessage = message;
    emit autoPipelineMessageChanged();
}

void AppController::retranslate()
{
    emit statusChanged();
    emit retranslateRequested();
    // The stage titles are translation keys, rendered on demand: the header
    // has to read them again in the new language.
    emit autoStateChanged();
    emit autoPipelineMessageChanged();
}

void AppController::notifyPageListGrown()
{
    emit documentChanged();
}

void AppController::notifyDocumentChanged()
{
    // A structural change reindexes pages: a pending rebuild would write into
    // whatever page now holds the index.
    m_pendingPageTextPages.clear();
    m_pageTextCoalesceTimer.stop();
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
    if (m_layout.busy() || m_importing || checkBusy())
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
        // The page was turned: what the check had to say about it is no
        // longer true.
        page.orientationProblem = PageOrientationProblem::None;
        page.suggestedTurn = 0;
        // The blocks were read from the old turn and say nothing about the new
        // one: the page goes back to the state before the recognition.
        page.result = OcrResult();
        page.recognized = false;
        page.recognitionMode = PageRecognitionMode::None;
        page.parseNote.clear();
    }

    m_pageModel.setRecognized(index, false);
    m_pageModel.setEdited(index, false);
    // The edit flag dies with the recognition it described: a stale one here
    // is what ProjectController::collect saves, and the reloaded page wears
    // «Изменено» it did not earn.
    m_editStore.setEdited(index, false);
    m_pageModel.setHasDuplicates(index, false);
    m_pageModel.setRecognitionMode(index, int(PageRecognitionMode::None));
    m_pageModel.setOrientationProblem(index, int(PageOrientationProblem::None));
    refreshPageVerification(index);
    if (index == m_currentPage)
        updateBoxesForCurrent();
    invalidatePageImage(index);
    ++m_cropRevision;
    updateAvailability();
    notifyPageChanged();
    return true;
}

void AppController::setPageOrientationProblem(int index, PageOrientationProblem problem)
{
    {
        QWriteLocker locker(&m_documentLock);
        if (!m_document.isValidIndex(index))
            return;
        m_document.page(index).orientationProblem = problem;
    }
    m_pageModel.setOrientationProblem(index, int(problem));
    emit pageChanged();
}

void AppController::applyPageDeskew(int index, double degrees)
{
    if (!m_document.isValidIndex(index))
        return;
    bool hadResult = false;
    bool changed = false;
    {
        QWriteLocker locker(&m_documentLock);
        DocumentPage &page = m_document.page(index);
        // The check measured the render as it stands, so the correction adds
        // to the straightening the page already carries.
        changed = m_document.setSkew(index, page.skewDegrees + degrees);
        if (changed && page.recognized) {
            // The blocks were read from the unstraightened page: they say
            // nothing about the straightened one.
            hadResult = true;
            page.result = OcrResult();
            page.recognized = false;
            page.recognitionMode = PageRecognitionMode::None;
            page.parseNote.clear();
        }
    }
    if (!changed)
        return;
    if (hadResult) {
        m_pageModel.setRecognized(index, false);
        m_pageModel.setEdited(index, false);
        m_editStore.setEdited(index, false);  // the flag dies with the result, as in turnPage
        m_pageModel.setHasDuplicates(index, false);
        m_pageModel.setRecognitionMode(index, int(PageRecognitionMode::None));
        refreshPageVerification(index);
    }
    invalidatePageImage(index);
    ++m_cropRevision;
    if (index == m_currentPage)
        updateBoxesForCurrent();
    setStatus(StatusMessage::translate("AppController", "Page %1 straightened by %2°.", index + 1, QString::number(std::abs(degrees), 'f', 1)));
    notifyPageChanged();
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
    // The provider is called on the worker for the whole check, and the
    // controller can be destroyed meanwhile (app quit mid-run), so the lambda
    // holds a guarded pointer and not the raw one.
    const QPointer<AppController> self(this);
    m_orientation.check(unchecked, [self](int index) {
        if (!self)
            return QImage();
        return self->pageImage(index);
    });
    return true;
}

void AppController::notifyPageChanged()
{
    emit pageChanged();
    emit imageChanged();
    emit resultChanged();
    emit editStateChanged();
}

// One-time mapping of a legacy project's hand-written page text into the
// per-block user layers (ADR 296). New projects already carry both the join
// and the layers, so the canonical comparison leaves them alone.
void AppController::migrateLegacyProjectText()
{
    const ParserOptions options = parserOptions();
    QWriteLocker locker(&m_documentLock);
    for (int i = 0; i < m_document.pageCount(); ++i) {
        DocumentPage &page = m_document.page(i);
        if (!page.recognized || page.result.pages.isEmpty())
            continue;
        OcrPage &ocrPage = page.result.pages[0];
        if (renderPageText(ocrPage, options).text == page.result.text)
            continue;
        migrateLegacyPageText(ocrPage, page.result.text, options);
    }
}

// The segments the page pushes must live in the same serialization as the
// markdown itself (data: URIs), or the editor's slice anchoring never finds
// a fragment with an image inside it.
QVariantList AppController::encodedBlockSegments() const
{
    const QVariantList segments = m_inspector.blockSegments();
    QVariantList encoded;
    encoded.reserve(segments.size());
    for (const QVariant &segment : segments) {
        QString text = segment.toString();
        for (const auto &ref : m_editorCropRefs)
            text.replace(ref.second, ref.first);
        encoded.append(text);
    }
    return encoded;
}

void AppController::requestEditorMarkdown(const QString &markdown)
{
    // The generation moves on every request, cache hit included: a hit answers
    // synchronously, and an encode already in flight from an older request
    // must not land after it and overwrite the newer answer.
    const quint64 generation = ++m_previewMarkdownGeneration;
    if (m_previewCacheRevision == m_imageRevision && m_previewCacheCropRevision == m_cropRevision && m_previewCacheText == markdown) {
        m_editorCropRefs = m_previewCacheRefs;
        emit editorMarkdownReady(m_previewCacheResult, m_inspector.blockBoxMap(), encodedBlockSegments());
        return;
    }

    const int page = m_currentPage;
    const int imageRevision = m_imageRevision;
    const int cropRevision = m_cropRevision;

    auto *watcher = new QFutureWatcher<QPair<QString, QList<QPair<QString, QString>>>>(this);
    connect(watcher, &QFutureWatcher<QPair<QString, QList<QPair<QString, QString>>>>::finished, this, [this, watcher, markdown, imageRevision, cropRevision, generation, page]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_previewMarkdownGeneration)
            return;
        m_previewCacheRevision = imageRevision;
        m_previewCacheCropRevision = cropRevision;
        m_previewCacheText = markdown;
        m_previewCacheResult = result.first;
        m_previewCacheRefs = result.second;
        // The user went elsewhere while the encode ran: the answer belongs to
        // a page the editor no longer shows, and the new page's own request
        // re-answers.
        if (m_currentPage != page)
            return;
        m_editorCropRefs = result.second;
        // Map and segments are read together, after the wait: a block landing
        // mid-encode must not pair a request-time map with fresh segments.
        emit editorMarkdownReady(result.first, m_inspector.blockBoxMap(), encodedBlockSegments());
    });
    const QPointer<AppController> self(this);
    watcher->setFuture(QtConcurrent::run([self, page, markdown]() {
        if (!self)
            return QPair<QString, QList<QPair<QString, QString>>>();
        QList<QPair<QString, QString>> refs;
        const QString encoded = self->encodePreviewMarkdown(page, markdown, &refs);
        return qMakePair(encoded, refs);
    }));
}

QString AppController::encodePreviewMarkdown(int pageIndex, const QString &markdown, QList<QPair<QString, QString>> *refs)
{
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
        const QImage img = croppedImage(pageIndex, boxIndex);
        if (!img.isNull()) {
            QByteArray bytes;
            QBuffer buffer(&bytes);
            if (buffer.open(QIODevice::WriteOnly)) {
                img.save(&buffer, "PNG");
                const QString dataUri = QStringLiteral("data:image/png;base64,%1").arg(QString::fromLatin1(bytes.toBase64()));
                result += QStringLiteral("![%1](%2)").arg(m.captured(1), dataUri);
                if (refs)
                    refs->append({dataUri, m.capturedView().toString()});
            } else {
                result += m.capturedView();
            }
        } else {
            result += m.capturedView();
        }
        last = m.capturedEnd();
    }
    result += markdownView.sliced(last);
    return result;
}

}  // namespace llocr
