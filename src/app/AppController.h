#pragma once

#include <QImage>
#include <QObject>
#include <QReadWriteLock>
#include <QUrl>
#include <QVariant>
#include <memory>

#include "models/OcrModelListModel.h"
#include "app/BoxListModel.h"
#include "app/DocumentModel.h"
#include "app/Exporter.h"
#include "app/PageListModel.h"
#include "app/PageEditStore.h"
#include "app/ProblemLog.h"
#include "app/RecognitionController.h"
#include "app/SettingsStore.h"
#include "app/VerificationPromptStore.h"
#include "app/VerificationQueueController.h"
#include "app/ExportController.h"
#include "core/CheckResult.h"
#include "core/OcrResult.h"
#include "core/StatusMessage.h"
#include "parsers/IOutputParser.h"
#include "parsers/ParserOptions.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class RequestProfileStore;

class AppController : public QObject
{
    Q_OBJECT

    // The document is GUI-thread resident; m_documentLock serialises the two
    // deliberate crossings of that rule (ADR 104): the export pipeline resolves
    // block crops from a worker thread, and the image provider may render a page.
    // Every access to m_document outside this class should go through an
    // accessor that takes the lock — a partial application of it is what makes
    // the contract unenforceable.

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(QString currentPageWarning READ currentPageWarning NOTIFY pageChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY documentChanged)
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY pageChanged)
    Q_PROPERTY(int imageRevision READ imageRevision NOTIFY imageRevisionChanged)
    Q_PROPERTY(int docRevision READ docRevision NOTIFY docRevisionChanged)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultChanged)

    Q_PROPERTY(bool currentPageEditable READ currentPageEditable NOTIFY resultChanged)
    Q_PROPERTY(bool currentPageEdited READ currentPageEdited NOTIFY editStateChanged)

    Q_PROPERTY(int selectedBoxIndex READ selectedBoxIndex WRITE setSelectedBoxIndex NOTIFY selectedBoxChanged)
    Q_PROPERTY(QString selectedBlockText READ selectedBlockText NOTIFY selectedBoxChanged)
    Q_PROPERTY(QString selectedBlockLabel READ selectedBlockLabel NOTIFY selectedBoxChanged)
    Q_PROPERTY(int selectedBlockCheckStatus READ selectedBlockCheckStatus NOTIFY selectedBoxChanged)
    Q_PROPERTY(QString selectedBlockCorrected READ selectedBlockCorrected NOTIFY selectedBoxChanged)

    Q_PROPERTY(bool checkBusy READ checkBusy NOTIFY checkStateChanged)
    Q_PROPERTY(bool checkRunning READ checkRunning NOTIFY checkStateChanged)
    Q_PROPERTY(bool checkFinished READ checkFinished NOTIFY checkStateChanged)
    Q_PROPERTY(int checkProgressDone READ checkProgressDone NOTIFY checkStateChanged)
    Q_PROPERTY(int checkProgressTotal READ checkProgressTotal NOTIFY checkStateChanged)
    Q_PROPERTY(QString checkErrorMessage READ checkErrorMessage NOTIFY checkStateChanged)
    Q_PROPERTY(bool pageVerificationSupported READ pageVerificationSupported NOTIFY checkStateChanged)
    Q_PROPERTY(bool allPageVerificationSupported READ allPageVerificationSupported NOTIFY checkStateChanged)

    Q_PROPERTY(QStringList exportNameFilters READ exportNameFilters NOTIFY retranslateRequested)

    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)

    Q_PROPERTY(QObject* pageModel READ pageModel CONSTANT)
    Q_PROPERTY(QObject* boxModel READ boxModel CONSTANT)

    // The OCR model adapters as id + display name (ADR 110): the UI picks by id,
    // so an unknown name can no longer be silently replaced by the default.
    Q_PROPERTY(QObject* ocrModels READ ocrModels CONSTANT)

    Q_PROPERTY(bool canRecognize READ canRecognize NOTIFY configChanged)

    Q_PROPERTY(QStringList parserNames READ parserNames NOTIFY retranslateRequested)
    Q_PROPERTY(QStringList parserLabels READ parserLabels NOTIFY retranslateRequested)

    Q_PROPERTY(QString parseWarning READ parseWarning NOTIFY resultChanged)

public:
    explicit AppController(SettingsStore &settings, RuntimeController &runtime,
                           RequestProfileStore &requestProfiles,
                           RequestProfileStore &checkRequestProfiles,
                           VerificationPromptStore &verification,
                           QObject *parent = nullptr);

    bool busy() const { return m_recognition.busy(); }
    bool exporting() const { return m_export.exporting(); }
    bool importing() const { return m_importing; }
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

    bool canRecognize() const;
    QStringList parserNames() const;
    QStringList parserLabels() const;
    QObject *ocrModels() const;

    QStringList exportNameFilters() const;

    bool currentPageEditable() const;
    bool currentPageEdited() const;

    int selectedBoxIndex() const { return m_selectedBox; }
    QString selectedBlockText() const;
    QString selectedBlockLabel() const;
    int selectedBlockCheckStatus() const;
    QString selectedBlockCorrected() const;
    void setSelectedBoxIndex(int index);

    bool checkBusy() const { return m_verify.checkBusy() || m_verify.queueActive(); }
    bool checkRunning() const { return m_verify.queueActive(); }
    bool checkFinished() const { return m_verify.finished(); }
    int checkProgressDone() const { return m_verify.progressDone(); }
    int checkProgressTotal() const { return m_verify.progressTotal(); }
    QString checkErrorMessage() const { return m_verify.errorMessage(); }
    bool pageVerificationSupported() const;
    bool allPageVerificationSupported() const;

    QObject *pageModel() const { return const_cast<PageListModel *>(&m_pageModel); }
    QObject *boxModel() const { return const_cast<BoxListModel *>(&m_boxModel); }

    void setCurrentPage(int index);

    /// The log the import diagnostics are reported to (ADR 119). Optional: a
    /// caller that shows no problem log (a test, a headless tool) simply gets
    /// none, and the status line still says that something was wrong.
    void setProblemLog(ProblemLog *log) { m_problems = log; }

    // The UI language changed: the status/error lines and the translated lists
    // are re-read. Nothing is cached — the messages hold their translation key
    // and are rendered on demand (ADR 114).
    void retranslate();

    QImage currentImage();
    QImage pageImage(int index, QString *error = nullptr);
    QImage pageThumbnail(int index) const;

    /// The preview render (ADR 118). Returns what is already cached and starts a
    /// worker render when there is nothing yet, so the image provider never
    /// blocks the GUI thread on a page render. `pageImageReady` fires when the
    /// result lands; until then the caller shows the thumbnail.
    QImage previewImage(int index);
    /// True while a preview render for `index` is in flight.
    Q_INVOKABLE bool previewRendering(int index) const;

    QImage croppedImage(int pageIndex, int boxIndex);

signals:
    void busyChanged();
    void importingChanged();
    void exportingChanged();
    void resultChanged();
    void statusChanged();
    void imageChanged();
    void documentChanged();
    void pageChanged();
    void imageRevisionChanged();
    void docRevisionChanged();
    void configChanged();
    void boxesChanged();
    /// A worker finished rendering a page for the preview (ADR 118).
    void pageImageReady(int index);

    void selectedBoxChanged();
    void checkStateChanged();

    void editStateChanged();

    void retranslateRequested();

public slots:
    Q_INVOKABLE void openFiles(const QVariantList& fileUrls);
    Q_INVOKABLE void recognizeCurrent();
    Q_INVOKABLE void recognizeAll();
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool removePage(int index);
    Q_INVOKABLE bool movePage(int from, int to);
    Q_INVOKABLE bool exportPages(const QUrl& fileUrl, int scope, int fromPage = 1, int toPage = 1);
    Q_INVOKABLE void setCurrentPageText(const QString& text);
    Q_INVOKABLE void revertCurrentPageEdits();
    Q_INVOKABLE void onBoxRectChanged(int boxIndex, qreal x, qreal y,
                                      qreal width, qreal height);
    // The single entry point for deleting a block: mutates the document, then
    // mirrors the change into the box model, the edit store and the selection.
    Q_INVOKABLE bool removeBlock(int boxIndex);
    Q_INVOKABLE QString resolveImagesForPreview(const QString& markdown);
    Q_INVOKABLE void checkSelectedBlock();
    Q_INVOKABLE void revertBlockCorrection();
    Q_INVOKABLE void checkEnabledBlocksOnPage();
    Q_INVOKABLE void checkAllEnabledBlocks(bool onlyUnchecked = false);
    Q_INVOKABLE void stopCheck();

private:
    struct ImportState;
    void importNextFile(const std::shared_ptr<ImportState>& state);
    void recordImportedFile(const std::shared_ptr<ImportState>& state,
                            int pagesBefore, const QString& error);
    void finishImport(const ImportState& state);
    void setStatus(const StatusMessage& message);
    /// Routes one diagnostic to the problem log and returns whether it was
    /// accepted, so a caller can decide to mention it in the status line too.
    bool reportProblem(const StatusMessage &message,
                       ProblemLog::Severity severity = ProblemLog::Warning);
    void notifyDocumentChanged();
    void notifyPageChanged();
    void applyRawResult(int index, const OcrResult& rawResult);
    // Resolves Settings → Output "Automatic (model default)" against the
    // selected OCR model adapter and builds the configured parser.
    QString effectiveParserId() const;
    ParserOptions parserOptions() const;
    std::unique_ptr<IOutputParser> makeParser() const;
    // The single writer for a page's text: keeps result.text and
    // result.pages[0].text in step, so the two can never diverge (ADR 102).
    void setPageText(int index, const QString& text);
    QString pageText(int index) const;
    QString rebuildPageText(const OcrPage &page) const;
    void updateBoxesForCurrent();
    const BoundingBox *selectedBox() const;

    void applyCheckResultToBox(int pageIndex, int boxIndex, const CheckResult &result);

private:
    SettingsStore &m_settings;
    RuntimeController &m_runtime;
    OcrModelListModel m_ocrModels;
    DocumentModel m_document;
    PageListModel m_pageModel;
    BoxListModel m_boxModel;
    RecognitionController m_recognition;
    VerificationQueueController m_verify;
    bool m_importing = false;
    int m_currentPage = 0;
    int m_imageRevision = 0;
    int m_docRevision = 0;
    int m_cropRevision = 0;
    mutable int m_previewCacheRevision = -1;
    mutable int m_previewCacheCropRevision = -1;
    mutable QString m_previewCacheText;
    mutable QString m_previewCacheResult;
    PageEditStore m_editStore;
    StatusMessage m_statusMessage;  ///< stored untranslated, rendered on read (ADR 114)
    ProblemLog *m_problems = nullptr;  ///< not owned; the full diagnostic texts live here
    ExportController m_export;
    mutable QReadWriteLock m_documentLock;

    int m_selectedBox = -1;
    bool m_recognitionStopped = false;

    // Preview render cache (ADR 118). Only the image provider goes through it;
    // recognition and export still use the synchronous pageImage(), which runs
    // off the GUI thread already.
    QHash<int, QImage> m_previewCache;
    int m_previewRendering = -1;   ///< page index a worker is busy with, -1 = idle
    quint64 m_previewGeneration = 0;  ///< bumped when the document changes
};

}  // namespace llocr
