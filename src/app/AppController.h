#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QReadWriteLock>
#include <QSet>
#include <QUrl>
#include <QVariant>

#include "app/AvailabilityPresenter.h"
#include "app/BlockInspector.h"
#include "app/BoxListModel.h"
#include "app/CandidateRenderer.h"
#include "app/DocumentModel.h"
#include "app/ExportController.h"
#include "app/Exporter.h"
#include "app/LayoutController.h"
#include "app/LruImageCache.h"
#include "app/OrientationController.h"
#include "app/PageEditStore.h"
#include "app/PageListModel.h"
#include "app/ProblemLog.h"
#include "app/ProjectController.h"
#include "app/RecognitionController.h"
#include "app/VerificationPromptStore.h"
#include "app/VerificationQueueController.h"
#include "config/SettingsStore.h"
#include "core/CheckResult.h"
#include "core/OcrResult.h"
#include "core/StatusMessage.h"
#include "models/OcrModel.h"
#include "parsers/IOutputParser.h"
#include "parsers/ParserOptions.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class RequestProfileStore;

constexpr qint64 kPreviewCacheBudgetBytes = 64ll * 1024 * 1024;
constexpr qint64 kThumbnailCacheBudgetBytes = 48ll * 1024 * 1024;

class AppController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
    Q_PROPERTY(int importProgressDone READ importProgressDone NOTIFY importProgressChanged)
    Q_PROPERTY(int importProgressTotal READ importProgressTotal NOTIFY importProgressChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(QString currentPageWarning READ currentPageWarning NOTIFY pageChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY documentChanged)
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY pageChanged)
    Q_PROPERTY(int imageRevision READ imageRevision NOTIFY imageRevisionChanged)
    Q_PROPERTY(int docRevision READ docRevision NOTIFY docRevisionChanged)
    Q_PROPERTY(int previewRenderingPage READ previewRenderingPage NOTIFY previewRenderingChanged)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultChanged)

    Q_PROPERTY(bool currentPageEditable READ currentPageEditable NOTIFY resultChanged)
    Q_PROPERTY(bool currentPageEdited READ currentPageEdited NOTIFY editStateChanged)

    Q_PROPERTY(int selectedBoxIndex READ selectedBoxIndex WRITE setSelectedBoxIndex NOTIFY selectedBoxChanged)
    Q_PROPERTY(QString selectedBlockText READ selectedBlockText NOTIFY selectedBoxChanged)
    Q_PROPERTY(QString selectedBlockLabel READ selectedBlockLabel NOTIFY selectedBoxChanged)
    Q_PROPERTY(int selectedBlockCheckStatus READ selectedBlockCheckStatus NOTIFY selectedBoxChanged)
    Q_PROPERTY(QString selectedBlockCorrected READ selectedBlockCorrected NOTIFY selectedBoxChanged)
    Q_PROPERTY(int selectedBlockCorrectedCheckStatus READ selectedBlockCorrectedCheckStatus NOTIFY selectedBoxChanged)
    Q_PROPERTY(double selectedBlockConfidence READ selectedBlockConfidence NOTIFY selectedBoxChanged)
    Q_PROPERTY(double selectedBlockCorrectedConfidence READ selectedBlockCorrectedConfidence NOTIFY selectedBoxChanged)
    // The outcome of the last fix run (ADR 185) and the layer the user reads:
    // the panel toggles between the corrected and the original text.
    Q_PROPERTY(int selectedBlockFixStatus READ selectedBlockFixStatus NOTIFY selectedBoxChanged)
    // What is wrong with a formula's LaTeX (ADR 194): the kind, so the panel
    // can name it in the UI language.
    Q_PROPERTY(int selectedBlockSyntaxProblem READ selectedBlockSyntaxProblem NOTIFY selectedBoxChanged)
    Q_PROPERTY(bool selectedBlockUsesCorrected READ selectedBlockUsesCorrected WRITE setSelectedBlockUsesCorrected NOTIFY selectedBoxChanged)
    Q_PROPERTY(QVariantList selectedBlockTextRange READ selectedBlockTextRange NOTIFY blockTextRangeChanged)
    // What the correction changed (ADR 186): the ranges the panel tints — the
    // fragments the fix dropped from the original and the ones it put in.
    Q_PROPERTY(QVariantList selectedBlockRemovedRanges READ selectedBlockRemovedRanges NOTIFY blockDiffChanged)
    Q_PROPERTY(QVariantList selectedBlockAddedRanges READ selectedBlockAddedRanges NOTIFY blockDiffChanged)
    // The panel's diff view: the tinted ranges or the plain text.
    Q_PROPERTY(bool highlightDifferences READ highlightDifferences WRITE setHighlightDifferences NOTIFY highlightDifferencesChanged)
    // The page text editor holds the keyboard: the approve hotkey must not
    // take the Return away from a text the user is typing in.
    Q_PROPERTY(bool pageTextEditorFocused READ pageTextEditorFocused WRITE setPageTextEditorFocused NOTIFY pageTextEditorFocusedChanged)

    Q_PROPERTY(bool checkBusy READ checkBusy NOTIFY checkStateChanged)
    Q_PROPERTY(bool checkRunning READ checkRunning NOTIFY checkStateChanged)
    Q_PROPERTY(bool checkFinished READ checkFinished NOTIFY checkStateChanged)
    Q_PROPERTY(int checkProgressDone READ checkProgressDone NOTIFY checkStateChanged)
    Q_PROPERTY(int checkProgressTotal READ checkProgressTotal NOTIFY checkStateChanged)
    Q_PROPERTY(bool checkDecisionPhase READ checkDecisionPhase NOTIFY checkStateChanged)
    Q_PROPERTY(bool checkRecheckPhase READ checkRecheckPhase NOTIFY checkStateChanged)
    Q_PROPERTY(bool checkFixPhase READ checkFixPhase NOTIFY checkStateChanged)
    Q_PROPERTY(QString checkErrorMessage READ checkErrorMessage NOTIFY checkStateChanged)
    Q_PROPERTY(bool pageVerificationSupported READ pageVerificationSupported NOTIFY availabilityChanged)
    Q_PROPERTY(bool allPageVerificationSupported READ allPageVerificationSupported NOTIFY availabilityChanged)
    Q_PROPERTY(bool pageProblemRecognitionSupported READ pageProblemRecognitionSupported NOTIFY availabilityChanged)
    Q_PROPERTY(bool allProblemRecognitionSupported READ allProblemRecognitionSupported NOTIFY availabilityChanged)
    Q_PROPERTY(bool pageBlocksRecognitionSupported READ pageBlocksRecognitionSupported NOTIFY availabilityChanged)
    // Whether the task's model is picked at all (RuntimeController): the
    // actions that need a task gray out without it (the rework plan's §7).
    Q_PROPERTY(bool pageOcrModelAvailable READ pageOcrModelAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(bool rawPageOcrModelAvailable READ rawPageOcrModelAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(bool blockRecognitionModelAvailable READ blockRecognitionModelAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(bool decisionModelAvailable READ decisionModelAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(bool layoutModelAvailable READ layoutModelAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(bool fixModelAvailable READ fixModelAvailable NOTIFY availabilityChanged)
    Q_PROPERTY(int currentPageRecognitionMode READ currentPageRecognitionMode NOTIFY availabilityChanged)
    Q_PROPERTY(int currentPageVerification READ currentPageVerification NOTIFY availabilityChanged)
    // What the orientation check found about the current page: 0 = nothing to
    // report, 1 = the page lies on its side, 2 = the picture could not read
    // the page.
    Q_PROPERTY(int currentPageOrientationProblem READ currentPageOrientationProblem NOTIFY pageChanged)
    // The toolbar's scope: the thumbnail selection, empty = every page. The
    // counts ride the availability broadcast so a verdict landing on a
    // non-current page re-announces them too.
    Q_PROPERTY(int selectedPageCount READ selectedPageCount NOTIFY availabilityChanged)
    Q_PROPERTY(int scopedPendingFixCount READ scopedPendingFixCount NOTIFY availabilityChanged)
    Q_PROPERTY(bool scopeHasResults READ scopeHasResults NOTIFY availabilityChanged)
    Q_PROPERTY(bool scopeVerificationSupported READ scopeVerificationSupported NOTIFY availabilityChanged)
    Q_PROPERTY(bool scopeBlocksRecognitionSupported READ scopeBlocksRecognitionSupported NOTIFY availabilityChanged)

    Q_PROPERTY(bool layoutBusy READ layoutBusy NOTIFY layoutStateChanged)
    Q_PROPERTY(int layoutProgressDone READ layoutProgressDone NOTIFY layoutProgressChanged)
    Q_PROPERTY(int layoutProgressTotal READ layoutProgressTotal NOTIFY layoutProgressChanged)
    Q_PROPERTY(bool layoutAvailable READ layoutAvailable NOTIFY layoutAvailableChanged)

    Q_PROPERTY(QStringList exportNameFilters READ exportNameFilters NOTIFY retranslateRequested)

    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)
    Q_PROPERTY(bool projectBusy READ projectBusy NOTIFY projectBusyChanged)
    Q_PROPERTY(QUrl projectPath READ projectPath NOTIFY projectPathChanged)
    Q_PROPERTY(QString projectFileName READ projectFileName NOTIFY projectPathChanged)

    Q_PROPERTY(QObject *pageModel READ pageModel CONSTANT)
    Q_PROPERTY(QObject *boxModel READ boxModel CONSTANT)

    Q_PROPERTY(bool canRecognize READ canRecognize NOTIFY availabilityChanged)
    // The one busy-gate every "act on the document" button folds in: the
    // recognition, layout and verification stages are mutually exclusive, and
    // each toolbar spelling this set by hand is how the mini toolbar lost
    // layoutBusy.
    Q_PROPERTY(bool actionsIdle READ actionsIdle NOTIFY availabilityChanged)

    Q_PROPERTY(QStringList parserNames READ parserNames NOTIFY retranslateRequested)
    Q_PROPERTY(QStringList parserLabels READ parserLabels NOTIFY retranslateRequested)

    Q_PROPERTY(QString parseWarning READ parseWarning NOTIFY resultChanged)

public:
    explicit AppController(SettingsStore &settings,
                           RuntimeController &runtime,
                           RequestProfileStore &requestProfiles,
                           RequestProfileStore &rawPageOcrRequestProfiles,
                           RequestProfileStore &checkRequestProfiles,
                           RequestProfileStore &layoutRequestProfiles,
                           RequestProfileStore &fixRequestProfiles,
                           VerificationPromptStore &verification,
                           QObject *parent = nullptr);
    ~AppController() override;

    bool busy() const { return m_recognition.busy(); }
    bool exporting() const { return m_export.exporting(); }
    bool projectBusy() const { return m_project.busy(); }
    QUrl projectPath() const { return m_project.path(); }
    QString projectFileName() const { return m_project.fileName(); }
    bool importing() const { return m_importing; }
    int importProgressDone() const { return m_importDone; }
    int importProgressTotal() const { return m_importTotal; }
    QString resultText() const;
    QString statusMessage() const { return m_statusMessage.text(); }
    QString currentPageWarning() const;
    QString parseWarning() const;
    bool hasImage() const;
    bool hasResult() const;
    int pageCount() const { return m_document.pageCount(); }
    int currentPage() const { return m_currentPage; }
    int imageRevision() const { return m_imageRevision; }
    int docRevision() const { return m_docRevision; }

    bool canRecognize() const { return m_availability.canRecognize(); }
    bool actionsIdle() const { return m_availability.actionsIdle(); }
    QStringList parserNames() const;
    QStringList parserLabels() const;

    QStringList exportNameFilters() const;

    bool currentPageEditable() const;
    bool currentPageEdited() const;

    int selectedBoxIndex() const { return m_inspector.selectedBoxIndex(); }
    QString selectedBlockText() const { return m_inspector.selectedBlockText(); }
    QString selectedBlockLabel() const { return m_inspector.selectedBlockLabel(); }
    int selectedBlockCheckStatus() const { return m_inspector.selectedBlockCheckStatus(); }
    QString selectedBlockCorrected() const { return m_inspector.selectedBlockCorrected(); }
    int selectedBlockCorrectedCheckStatus() const { return m_inspector.selectedBlockCorrectedCheckStatus(); }
    double selectedBlockConfidence() const { return m_inspector.selectedBlockConfidence(); }
    double selectedBlockCorrectedConfidence() const { return m_inspector.selectedBlockCorrectedConfidence(); }
    int selectedBlockFixStatus() const { return m_inspector.selectedBlockFixStatus(); }
    int selectedBlockSyntaxProblem() const { return m_inspector.selectedBlockSyntaxProblem(); }
    bool selectedBlockUsesCorrected() const { return m_inspector.selectedBlockUsesCorrected(); }
    void setSelectedBlockUsesCorrected(bool corrected);
    QVariantList selectedBlockTextRange() const { return m_inspector.selectedBlockTextRange(); }
    void setSelectedBoxIndex(int index) { m_inspector.setSelectedBoxIndex(index); }
    QVariantList selectedBlockRemovedRanges() const { return m_inspector.selectedBlockRemovedRanges(); }
    QVariantList selectedBlockAddedRanges() const { return m_inspector.selectedBlockAddedRanges(); }
    bool highlightDifferences() const { return m_inspector.highlightDifferences(); }
    void setHighlightDifferences(bool enabled);
    bool pageTextEditorFocused() const { return m_inspector.pageTextEditorFocused(); }
    void setPageTextEditorFocused(bool focused);

    bool checkBusy() const { return m_verify.checkBusy() || m_verify.queueActive(); }
    bool checkRunning() const { return m_verify.queueActive(); }
    bool checkFinished() const { return m_verify.finished(); }
    int checkProgressDone() const { return m_verify.progressDone(); }
    int checkProgressTotal() const { return m_verify.progressTotal(); }
    bool checkDecisionPhase() const { return m_verify.decisionPhase(); }
    bool checkRecheckPhase() const { return m_verify.recheckPhase(); }
    bool checkFixPhase() const { return m_verify.fixPhase(); }
    QString checkErrorMessage() const { return m_verify.errorMessage(); }
    bool pageVerificationSupported() const { return m_availability.pageVerificationSupported(); }
    bool allPageVerificationSupported() const { return m_availability.allPageVerificationSupported(); }
    bool pageProblemRecognitionSupported() const { return m_availability.pageProblemRecognitionSupported(); }
    bool allProblemRecognitionSupported() const { return m_availability.allProblemRecognitionSupported(); }
    bool pageBlocksRecognitionSupported() const { return m_availability.pageBlocksRecognitionSupported(); }
    bool pageOcrModelAvailable() const { return m_availability.pageOcrModelAvailable(); }
    bool rawPageOcrModelAvailable() const { return m_availability.rawPageOcrModelAvailable(); }
    bool blockRecognitionModelAvailable() const { return m_availability.blockRecognitionModelAvailable(); }
    bool decisionModelAvailable() const { return m_availability.decisionModelAvailable(); }
    bool layoutModelAvailable() const { return m_availability.layoutModelAvailable(); }
    bool fixModelAvailable() const { return m_availability.fixModelAvailable(); }
    int currentPageRecognitionMode() const { return m_availability.currentPageRecognitionMode(); }
    int currentPageVerification() const { return m_availability.currentPageVerification(); }
    int currentPageOrientationProblem() const;
    int selectedPageCount() const { return m_availability.selectedPageCount(); }
    int scopedPendingFixCount() const { return m_availability.scopedPendingFixCount(); }
    bool scopeHasResults() const { return m_availability.scopeHasResults(); }
    bool scopeVerificationSupported() const { return m_availability.scopeVerificationSupported(); }
    bool scopeBlocksRecognitionSupported() const { return m_availability.scopeBlocksRecognitionSupported(); }

    bool layoutBusy() const { return m_layout.busy(); }
    int layoutProgressDone() const { return m_layout.progressDone(); }
    int layoutProgressTotal() const { return m_layout.progressTotal(); }
    bool layoutAvailable() const { return m_layout.available(); }

    QObject *pageModel() const { return const_cast<PageListModel *>(&m_pageModel); }
    QObject *boxModel() const { return const_cast<BoxListModel *>(&m_boxModel); }

    void setCurrentPage(int index);

    void setProblemLog(ProblemLog *log) { m_problems = log; }
    void retranslate();

    QImage pageImage(int index, QString *error = nullptr);
    QImage pageThumbnail(int index);
    QImage previewImage(int index);
    int previewRenderingPage() const { return m_previewRendering.load(); }
    Q_INVOKABLE bool previewRendering(int index) const;

    QImage croppedImage(int pageIndex, int boxIndex);
    // The text of a block as the page shows it: a formula as display math, a
    // table as a grid. The visual-diff decision paints its candidate from it
    // (ADR 237).
    QString blockDisplayText(int pageIndex, int boxIndex);

signals:
    void busyChanged();
    void importingChanged();
    void importProgressChanged();
    void exportingChanged();
    void resultChanged();
    void statusChanged();
    void imageChanged();
    void documentChanged();
    void pageChanged();
    void imageRevisionChanged();
    void docRevisionChanged();
    void configChanged();
    void pageImageReady(int index);
    void previewRenderingChanged();
    void layoutStateChanged();
    void layoutProgressChanged();
    void layoutAvailableChanged();

    void selectedBoxChanged();
    void blockTextRangeChanged();
    // The panel's diff view changed: a new block, a new correction or the
    // highlight toggle.
    void blockDiffChanged();
    void highlightDifferencesChanged();
    void pageTextEditorFocusedChanged();
    void checkStateChanged();
    // The availability flags (canRecognize and the page/all "supported"
    // properties) fold in the current page, the page content, the recognition
    // activity and the runtime config, so they are re-announced on all of
    // those — a single NOTIFY per property cannot cover them.
    void availabilityChanged();

    void editStateChanged();

    void retranslateRequested();

    void projectBusyChanged();
    void projectPathChanged();

public slots:
    Q_INVOKABLE void openFiles(const QVariantList &fileUrls);
    // task selects the recognition pipeline: "pageOcr" (markup-aware) or
    // "rawPageOcr" (markup-free); the legacy no-task spellings keep the
    // pageOcr meaning.
    Q_INVOKABLE void recognizeCurrentPage(const QString &task);
    Q_INVOKABLE void recognizeAllPages(const QString &task);
    Q_INVOKABLE void recognizeCurrent();
    Q_INVOKABLE void recognizeAll();
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool removePage(int index);
    Q_INVOKABLE bool movePage(int from, int to);
    Q_INVOKABLE bool exportPages(const QUrl &fileUrl, int scope, int fromPage = 1, int toPage = 1);
    Q_INVOKABLE void setCurrentPageText(const QString &text);
    Q_INVOKABLE void revertCurrentPageEdits();
    Q_INVOKABLE void onBoxRectChanged(int boxIndex, qreal x, qreal y, qreal width, qreal height);
    Q_INVOKABLE bool removeBlock(int boxIndex);
    // Adds an empty block of the given type to the current page and selects it.
    Q_INVOKABLE bool addBlock(const QString &type);
    // Adds an empty block with the rectangle the user drew on the page (the
    // coordinates are relative to the page, ADR 189).
    Q_INVOKABLE bool addBlockAt(const QString &type, qreal x, qreal y, qreal width, qreal height);
    Q_INVOKABLE QString resolveImagesForPreview(const QString &markdown);
    Q_INVOKABLE void checkSelectedBlock();
    Q_INVOKABLE void revertBlockCorrection();
    // The user confirms the block by hand (ADR 185): the layer in view is
    // marked correct even when the decision model rejected it.
    Q_INVOKABLE void approveSelectedBlock();
    // «Отозвать утверждение»: the user's confirmation is undone and the
    // verdict the decision model gave stands again (ADR 187).
    Q_INVOKABLE void revertBlockApproval();
    // Approves the block in view and jumps to the next problem block of the
    // page: the operator's rhythm (hotkey in the panel).
    Q_INVOKABLE void approveSelectedBlockAndSelectNext();
    // Selects the next block the decision model left open on this page.
    Q_INVOKABLE void selectNextProblemBlock();
    // The user corrects the text of a version by hand (ADR 187): the version
    // is written, marked as the user's own, and the page text follows.
    Q_INVOKABLE void setSelectedBlockText(const QString &text, bool correctedLayer);
    Q_INVOKABLE bool blockTextMapped() const;
    Q_INVOKABLE QList<int> blockTextRange(int boxIndex) const;
    Q_INVOKABLE int boxIndexForTextPosition(int position) const;
    Q_INVOKABLE void attachBlockTextHighlighter(QObject *textDocument, const QColor &color);
    Q_INVOKABLE void checkEnabledBlocksOnPage();
    Q_INVOKABLE void checkAllEnabledBlocks();
    // The toolbar's scoped spellings: the same runs over the thumbnail
    // selection (empty selection = every page).
    Q_INVOKABLE void recognizeScopePages(const QString &task);
    Q_INVOKABLE void recognizeScopeBlocks();
    Q_INVOKABLE void layoutScopePages();
    Q_INVOKABLE void checkScopeEnabledBlocks();
    Q_INVOKABLE void fixScopeProblemBlocks();
    Q_INVOKABLE void stopCheck();
    // Block (re-)recognition through the blockRecognition role: the selected
    // block regardless of status, or only the problem blocks (Mismatch/Review).
    Q_INVOKABLE void recognizeSelectedBlock();
    Q_INVOKABLE void recognizeProblemBlocksOnPage();
    Q_INVOKABLE void recognizeAllProblemBlocks();
    Q_INVOKABLE void recognizeAllBlocksOnPage();
    // The fix pipeline (ADR 163): the selected block, or every Mismatch/Review
    // block of the document through the fix task's OK / FIX / REVIEW protocol.
    Q_INVOKABLE void fixSelectedBlock();
    Q_INVOKABLE void fixAllProblemBlocks();
    // Manual page markup through the layout role (TeleOCR-style two-phase
    // models): the reply replaces the page blocks, the text stays empty until
    // each block goes through blockRecognition.
    Q_INVOKABLE void layoutCurrentPage();
    Q_INVOKABLE void layoutAllPages();
    Q_INVOKABLE void stopLayout();
    // Turns the current page by the given degrees (clockwise). The turn drops
    // the page's recognition: the blocks were read from the old turn and say
    // nothing about the new one.
    Q_INVOKABLE void rotatePage(int degrees);
    Q_INVOKABLE void cancelImport();
    Q_INVOKABLE void copyCurrentPageText();
    Q_INVOKABLE void openProject(const QUrl &fileUrl) { m_project.open(fileUrl); }
    Q_INVOKABLE void saveProject(const QUrl &fileUrl) { m_project.save(fileUrl); }

private:
    struct ImportState;
    void importNextFile(const std::shared_ptr<ImportState> &state);
    void recordImportedFile(const std::shared_ptr<ImportState> &state, int pagesBefore, const QString &error);
    void finishImport(const ImportState &state);
    void setStatus(const StatusMessage &message);
    bool reportProblem(const StatusMessage &message, ProblemLog::Severity severity = ProblemLog::Warning);
    void notifyDocumentChanged();
    void notifyPageChanged();
    // The page's picture changed: the cached renders of that page are stale.
    void invalidatePageImage(int index);
    // The page is turned by the given degrees. The turn drops the page's
    // recognition: the blocks were read from the old turn. A turn by the user
    // also marks the page as turned on purpose: the orientation check leaves
    // it alone.
    bool turnPage(int index, int degrees, bool byUser);
    // Runs the orientation check before the pages are recognized. Returns
    // true when the run waits for the check.
    bool runAfterOrientationCheck(const QList<int> &pages, const std::function<void()> &run);
    // What the orientation check found about a page: the page is marked and
    // the user turns it by hand.
    void setPageOrientationProblem(int index, PageOrientationProblem problem);
    void applyRawResult(int index, ConnectionRole task, const OcrResult &rawResult);
    void applyLayout(int pageIndex, const QString &rawText);
    void startLayout(const QList<int> &pages);
    QString effectiveParserId() const;
    QString effectiveParserIdFor(const QString &profileId, const QString &role = {}) const;
    ParserOptions parserOptions() const;
    std::unique_ptr<IOutputParser> makeParser() const;
    void setPageText(int index, const QString &text);
    QString pageText(int index) const;
    QString rebuildPageText(const OcrPage &page) const;
    void updateBoxesForCurrent();

    // The verification queue's result sources (ADR 164): a decision verdict
    // lands on the effective layer, a block-recognition reply is an OCR
    // result and overwrites the text layer, a fix outcome writes the
    // correction layer.
    void applyVerificationStarted(int pageIndex, int boxIndex);
    void applyVerdictToBox(int pageIndex, int boxIndex, const CheckResult &result);
    void applyRecognitionToBox(int pageIndex, int boxIndex, const CheckResult &result);
    void applyFixToBox(int pageIndex, int boxIndex, const CheckResult &result);
    // The effective text of the page changed structurally: rebuild it, rebase
    // the edit baseline and refresh the views.
    void applyPageTextChanged(int pageIndex);
    // Runs one transition on the box under the document lock and mirrors the
    // resulting two-layer state to the box model; false when the page or the
    // box is gone.
    bool applyBoxTransition(int pageIndex, int boxIndex, const std::function<void(BoundingBox &)> &transition);

    // The page-status derivations (the rework plan's §6): the mode is stored
    // on the page, the verification aggregate is computed over the page's
    // boxes with the current filter. Both land in the page list's roles and,
    // for the current page, in the availability flags.
    void setRecognitionMode(int pageIndex, PageRecognitionMode mode);
    void refreshPageVerification(int pageIndex);
    void refreshAllPageVerifications();
    // MarkupOnly → MarkupOcr once every text-expecting block of the page
    // carries text; a no-op for the other modes.
    void refreshRecognitionModeAfterBlockText(int pageIndex);

    // The toolbar scope: the thumbnail selection, empty selection = every
    // page.
    QList<int> scopePages() const;
    // A fresh document starts with its current page selected so the toolbar
    // scope reads "1 page", not "all".
    void ensureDefaultSelection();

    void updateAvailability();

private:
    SettingsStore &m_settings;
    RuntimeController &m_runtime;
    VerificationPromptStore &m_verification;
    RequestProfileStore &m_checkRequestProfiles;
    RequestProfileStore &m_layoutRequestProfiles;
    RequestProfileStore &m_fixRequestProfiles;
    DocumentModel m_document;
    PageListModel m_pageModel;
    BoxListModel m_boxModel;
    BlockInspector m_inspector;
    RecognitionController m_recognition;
    // Declared before the queue: a render in flight must die with its
    // callbacks before the queue that would receive them.
    CandidateRenderer m_candidateRenderer;
    VerificationQueueController m_verify;
    LayoutController m_layout;
    OrientationController m_orientation;
    std::function<void()> m_pendingRun;  // The run that waits for the orientation check.
    AvailabilityPresenter m_availability;
    bool m_importing = false;
    int m_importDone = 0;
    int m_importTotal = 0;
    std::shared_ptr<ImportState> m_import;
    int m_currentPage = 0;
    int m_imageRevision = 0;
    int m_docRevision = 0;
    int m_cropRevision = 0;
    mutable int m_previewCacheRevision = -1;
    mutable int m_previewCacheCropRevision = -1;
    mutable QString m_previewCacheText;
    mutable QString m_previewCacheResult;
    PageEditStore m_editStore;
    StatusMessage m_statusMessage;     ///< stored untranslated, rendered on read
    ProblemLog *m_problems = nullptr;  ///< not owned; the full diagnostic texts live here
    ExportController m_export;
    mutable QReadWriteLock m_documentLock;
    ProjectController m_project;

    mutable QString m_parserIdCache;
    int m_pageSelectionRevision = 0;
    int m_verificationFilterRevision = 0;
    mutable QString m_parserIdKey;

    LruImageCache m_previewCache{kPreviewCacheBudgetBytes};
    mutable QMutex m_previewMutex;
    std::atomic<int> m_previewRendering = -1;      ///< page index a worker is busy with, -1 = idle
    std::atomic<quint64> m_previewGeneration = 0;  ///< bumped when the document changes

    QMutex m_thumbnailMutex;
    LruImageCache m_thumbnailCache{kThumbnailCacheBudgetBytes};
    QSet<QString> m_reportedUnrenderablePages;

    void notifyPageListGrown();
    void markPageEdited(int index);
};

}  // namespace llocr
