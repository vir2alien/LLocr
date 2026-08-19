#pragma once

#include <memory>

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QUrl>
#include <QVariantMap>

#include "app/BoxListModel.h"
#include "app/DocumentModel.h"
#include "app/Exporter.h"
#include "app/PageListModel.h"
#include "app/SettingsStore.h"
#include "core/OcrResult.h"
#include "providers/OpenAiProvider.h"

namespace llocr {

class AppController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY documentChanged)
    Q_PROPERTY(int currentPage READ currentPage WRITE setCurrentPage NOTIFY pageChanged)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultChanged)

    Q_PROPERTY(bool currentPageEditable READ currentPageEditable NOTIFY resultChanged)
    Q_PROPERTY(bool currentPageEdited READ currentPageEdited NOTIFY editStateChanged)

    Q_PROPERTY(bool pandocAvailable READ pandocAvailable CONSTANT)
    Q_PROPERTY(QStringList exportNameFilters READ exportNameFilters CONSTANT)

    Q_PROPERTY(QObject* pageModel READ pageModel CONSTANT)
    Q_PROPERTY(QObject* boxModel READ boxModel CONSTANT)

    Q_PROPERTY(QString prompt READ prompt WRITE setPrompt NOTIFY promtChanged)

    Q_PROPERTY(bool canRecognize READ canRecognize NOTIFY configChanged)

    Q_PROPERTY(QStringList parserNames READ parserNames CONSTANT)

public:
    explicit AppController(SettingsStore &settings, QObject *parent = nullptr);

    // --- QML getters ---
    bool busy() const { return m_busy; }
    QString resultText() const;
    QString statusMessage() const { return m_statusMessage; }
    bool hasImage() const;
    bool hasResult() const;
    int pageCount() const { return m_document.pageCount(); }
    int currentPage() const { return m_currentPage; }

    bool canRecognize() const;
    QStringList parserNames() const;

    bool pandocAvailable() const;
    QStringList exportNameFilters() const;

    bool currentPageEditable() const;
    bool currentPageEdited() const;

    QObject *pageModel() { return &m_pageModel; }
    QObject *boxModel() { return &m_boxModel; }

    QString prompt() { return m_prompt; }

    // --- QML setters ---

    void setPrompt(const QString &prompt);
    void setCurrentPage(int index);

    QImage currentImage() const;
    QImage pageImage(int index) const;
    OcrResult currentResult() const;

    /// Crops the image-block of a page (used by OcrImageProvider and export).
    QImage croppedImage(int pageIndex, int boxIndex) const;

signals:
    void busyChanged();
    void resultChanged();
    void promtChanged();
    void statusChanged();
    void imageChanged();
    void documentChanged();
    void pageChanged();
    void configChanged();
    void boxesChanged();

    void editStateChanged();

    // signal for the C++ pipeline / tests.
    void recognitionFinished(const llocr::OcrResult& result);

public slots:
    Q_INVOKABLE bool openImage(const QString& filePath);
    Q_INVOKABLE bool openImages(const QStringList& filePaths);
    Q_INVOKABLE void openDocument(const QUrl& fileUrl);
    Q_INVOKABLE void openFiles(const QVariantList& fileUrls);

    Q_INVOKABLE void recognizeCurrent();
    Q_INVOKABLE void recognizeAll();

    Q_INVOKABLE void stop();

    Q_INVOKABLE bool removePage(int index);

    Q_INVOKABLE bool movePage(int from, int to);

    Q_INVOKABLE bool exportPages(const QUrl& fileUrl, int scope, int fromPage = 1, int toPage = 1);

    Q_INVOKABLE bool exportResult(const QUrl& fileUrl);

    Q_INVOKABLE void setCurrentPageText(const QString& text);
    Q_INVOKABLE void revertCurrentPageEdits();

    // --- Image-block editing (called from the preview overlay) ---
    Q_INVOKABLE void onBoxRectChanged(int boxIndex, qreal x, qreal y,
                                      qreal width, qreal height);
    Q_INVOKABLE void onBoxRemoved(int boxIndex);

private slots:
    void onRecognitionFinished();

private:
    enum ExportScope {
        ExportAll = 0,
        ExportCurrent = 1,
        ExportRange = 2,
    };
    Q_ENUM(ExportScope)

    void setBusy(bool busy);
    void setStatus(const QString& message);

    void recognizePage(int index);
    void recognizeSequential(int index);
    OcrRequest buildRequest(const QImage &image, const QString &prompt) const;
    void applyRawResult(int index, const OcrResult& rawResult);

    void finishRun();
    void updateBoxesForCurrent();

    QList<Exporter::Page> collectPages(int scope, int fromPage, int toPage) const;
    QList<Exporter::Page> collectRecognizedPages() const;

    QString effectiveText(int index) const;

    std::unique_ptr<OpenAiProvider> m_provider;

    SettingsStore &m_settings;

    DocumentModel m_document;
    PageListModel m_pageModel;
    BoxListModel m_boxModel;

    QString m_prompt = "document parsing.";

    /* promts:
     * "document parsing."
     * "Multi page parsing."
     * "Free OCR."
     * "Parse the figure."
     * "Describe this image in detail."
     * "<|grounding|>Convert the document to markdown."
     * "<|grounding|>OCR this image."
     * "<|grounding|>Free OCR."
     * "<|grounding|>Locate <|ref|>Invoice Number<|/ref|> in the image."
     */

    Exporter m_exporter;

    QFutureWatcher<OcrResult> m_watcher;

    int m_currentPage = 0;
    bool m_busy = false;
    QString m_statusMessage;

    QHash<int, QString> m_edits;

    bool m_stopRequested = false;
    bool m_recognizeAll = false;
    int m_recognizingIndex = -1;
};

}  // namespace llocr
