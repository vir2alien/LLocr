#pragma once

#include <QImage>
#include <QObject>
#include <QPageLayout>
#include <QReadWriteLock>
#include <QUrl>
#include <QVariant>
#include <memory>

#include "app/BoxListModel.h"
#include "app/CheckController.h"
#include "app/DocumentModel.h"
#include "app/Exporter.h"
#include "app/ExportRenderer.h"
#include "app/PageListModel.h"
#include "app/PageEditStore.h"
#include "app/RecognitionController.h"
#include "app/SettingsStore.h"
#include "app/VerificationPromptStore.h"
#include "core/OcrResult.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class RequestProfileStore;

class AppController : public QObject
{
    Q_OBJECT

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
    Q_PROPERTY(int checkProgressDone READ checkProgressDone NOTIFY checkStateChanged)
    Q_PROPERTY(int checkProgressTotal READ checkProgressTotal NOTIFY checkStateChanged)
    Q_PROPERTY(QString checkErrorMessage READ checkErrorMessage NOTIFY checkStateChanged)
    Q_PROPERTY(bool pageVerificationSupported READ pageVerificationSupported NOTIFY checkStateChanged)

    Q_PROPERTY(QStringList exportNameFilters READ exportNameFilters CONSTANT)

    Q_PROPERTY(bool exporting READ exporting NOTIFY exportingChanged)

    Q_PROPERTY(QObject* pageModel READ pageModel CONSTANT)
    Q_PROPERTY(QObject* boxModel READ boxModel CONSTANT)

    Q_PROPERTY(QStringList modelNames READ modelNames CONSTANT)

    Q_PROPERTY(bool canRecognize READ canRecognize NOTIFY configChanged)

    Q_PROPERTY(QStringList parserNames READ parserNames CONSTANT)

public:
    explicit AppController(SettingsStore &settings, RuntimeController &runtime,
                           RequestProfileStore &requestProfiles,
                           RequestProfileStore &checkRequestProfiles,
                           VerificationPromptStore &verification,
                           QObject *parent = nullptr);

    bool busy() const { return m_recognition.busy(); }
    bool exporting() const { return m_exporting; }
    bool importing() const { return m_importing; }
    QString resultText() const;
    QString statusMessage() const { return m_statusMessage; }
    QString currentPageWarning() const;
    bool hasImage() const;
    bool hasResult() const;
    int pageCount() const { return m_document.pageCount(); }
    int currentPage() const { return m_currentPage; }
    int imageRevision() const { return m_imageRevision; }
    int docRevision() const { return m_docRevision; }

    bool canRecognize() const;
    QStringList parserNames() const;
    QStringList modelNames() const;
    Q_INVOKABLE QString modelIdToName(const QString &modelId) const;
    Q_INVOKABLE QString modelNameToId(const QString &modelName) const;

    QStringList exportNameFilters() const;

    bool currentPageEditable() const;
    bool currentPageEdited() const;

    int selectedBoxIndex() const { return m_selectedBox; }
    QString selectedBlockText() const;
    QString selectedBlockLabel() const;
    int selectedBlockCheckStatus() const;
    QString selectedBlockCorrected() const;
    void setSelectedBoxIndex(int index);

    bool checkBusy() const { return m_check.busy() || m_verifyQueueActive; }
    bool checkRunning() const { return m_verifyQueueActive; }
    int checkProgressDone() const { return m_verifyDone; }
    int checkProgressTotal() const { return m_verifyTotal; }
    QString checkErrorMessage() const { return m_checkError; }
    bool pageVerificationSupported() const;

    QObject *pageModel() const { return const_cast<PageListModel *>(&m_pageModel); }
    QObject *boxModel() const { return const_cast<BoxListModel *>(&m_boxModel); }

    void setCurrentPage(int index);

    QImage currentImage();
    QImage pageImage(int index, QString *error = nullptr);
    QImage pageThumbnail(int index) const;

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

    void selectedBoxChanged();
    void checkStateChanged();

    void editStateChanged();

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
    Q_INVOKABLE void onBoxRemoved(int boxIndex);
    Q_INVOKABLE QString resolveImagesForPreview(const QString& markdown);
    Q_INVOKABLE void checkSelectedBlock();
    Q_INVOKABLE void checkEnabledBlocksOnPage();
    Q_INVOKABLE void stopCheck();

private:
    enum ExportScope : int {
        ExportAll = 0,
        ExportCurrent = 1,
        ExportRange = 2,
    };

    struct ImportState;
    void importNextFile(const std::shared_ptr<ImportState>& state);
    void recordImportedFile(const std::shared_ptr<ImportState>& state,
                            int pagesBefore, const QString& error);
    void finishImport(const ImportState& state);
    void setStatus(const QString& message);
    void notifyDocumentChanged();
    void notifyPageChanged();
    void applyRawResult(int index, const OcrResult& rawResult);
    void updateBoxesForCurrent();
    QList<Exporter::Page> collectPages(int scope, int fromPage, int toPage) const;
    QString effectiveText(int index) const;
    const BoundingBox *selectedBox() const;

    void finishExport(const Exporter::Result& result, int pageCount);
    QPageLayout pdfPageLayout() const;
    Exporter::Result finalizeRenderedExport(
        Exporter::Format format, const QString& path,
        const QList<Exporter::Page>& pages, const Exporter::CropProvider& crop,
        const Exporter::ExportOptions& options, const QPageLayout& pdfLayout,
        bool renderOk, const QString& renderedHtml, const QString& renderError) const;

    void startVerifyQueue(const QList<int> &boxIndices);
    void startNextVerify();
    void finishVerifyQueue();
    void applyCheckResultToBox(int boxIndex, const CheckResult &result);

private:
    SettingsStore &m_settings;
    RuntimeController &m_runtime;
    VerificationPromptStore &m_verification;
    DocumentModel m_document;
    PageListModel m_pageModel;
    BoxListModel m_boxModel;
    RecognitionController m_recognition;
    CheckController m_check;
    Exporter m_exporter;
    ExportRenderer m_exportRenderer;
    bool m_exporting = false;
    bool m_importing = false;
    int m_currentPage = 0;
    QString m_statusMessage;
    int m_imageRevision = 0;
    int m_docRevision = 0;
    int m_cropRevision = 0;
    mutable int m_previewCacheRevision = -1;
    mutable int m_previewCacheCropRevision = -1;
    mutable QString m_previewCacheText;
    mutable QString m_previewCacheResult;
    PageEditStore m_editStore;
    mutable QReadWriteLock m_documentLock;

    int m_selectedBox = -1;
    QString m_checkError;

    // Batch verification queue (current page, serial per-block requests).
    QList<int> m_verifyQueue;
    int m_verifyBoxIndex = -1;
    bool m_verifyQueueActive = false;
    int m_verifyTotal = 0;
    int m_verifyDone = 0;
};

}  // namespace llocr
