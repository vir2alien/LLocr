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

    Q_PROPERTY(bool checkBusy READ checkBusy NOTIFY checkStateChanged)
    Q_PROPERTY(bool checkSucceeded READ checkSucceeded NOTIFY checkStateChanged)
    Q_PROPERTY(QString checkResultText READ checkResultText NOTIFY checkStateChanged)
    Q_PROPERTY(QString checkErrorMessage READ checkErrorMessage NOTIFY checkStateChanged)
    Q_PROPERTY(bool checkApplied READ checkApplied NOTIFY checkStateChanged)

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
    void setSelectedBoxIndex(int index);

    bool checkBusy() const { return m_check.busy(); }
    bool checkSucceeded() const { return m_checkApplied ? false : m_checkSucceeded; }
    QString checkResultText() const { return m_checkResultText; }
    QString checkErrorMessage() const { return m_checkError; }
    bool checkApplied() const { return m_checkApplied; }

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
    Q_INVOKABLE void checkSelectedBlock(const QString &prompt);
    Q_INVOKABLE void applyCheckedText();

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

private:
    SettingsStore &m_settings;
    RuntimeController &m_runtime;
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
    bool m_checkSucceeded = false;
    bool m_checkApplied = false;
    QString m_checkResultText;
    QString m_checkError;
    // Where the current check result belongs. The result is applied only when
    // the page/box it was computed for is still the selected one, so a
    // mid-check selection change can never retarget "Apply fix".
    int m_checkPage = -1;
    int m_checkBox = -1;
};

}  // namespace llocr
