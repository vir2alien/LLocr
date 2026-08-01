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
#include "core/ProviderConfig.h"
#include "providers/OpenAiProvider.h"

namespace llocr {

/**
 * @brief orchestrator between the QML UI and the backend
 */
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

    Q_PROPERTY(bool canRecognize READ canRecognize NOTIFY configChanged)

    Q_PROPERTY(QStringList parserNames READ parserNames CONSTANT)

    Q_PROPERTY(QString baseUrl READ baseUrl NOTIFY configChanged)
    Q_PROPERTY(QString apiKey READ apiKey NOTIFY configChanged)
    Q_PROPERTY(int timeoutMs READ timeoutMs NOTIFY configChanged)
    Q_PROPERTY(QString modelName READ modelName NOTIFY configChanged)
    Q_PROPERTY(QString prompt READ prompt NOTIFY configChanged)
    Q_PROPERTY(double temperature READ temperature NOTIFY configChanged)
    Q_PROPERTY(int maxTokens READ maxTokens NOTIFY configChanged)
    Q_PROPERTY(QString parserId READ parserId NOTIFY configChanged)
    Q_PROPERTY(int bboxCoordinateRange READ bboxCoordinateRange NOTIFY configChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    void setProviderConfig(const ProviderConfig& config);

    // --- QML getters ---
    bool busy() const { return m_busy; }
    QString resultText() const;
    QString statusMessage() const { return m_statusMessage; }
    bool hasImage() const;
    bool hasResult() const;
    int pageCount() const { return m_document.pageCount(); }
    int currentPage() const { return m_currentPage; }

    bool canRecognize() const { return !m_config.modelName.trimmed().isEmpty(); }
    QStringList parserNames() const;

    bool pandocAvailable() const;
    QStringList exportNameFilters() const;

    bool currentPageEditable() const;
    bool currentPageEdited() const;

    QObject* pageModel() { return &m_pageModel; }
    QObject* boxModel() { return &m_boxModel; }

    QString baseUrl() const { return m_config.baseUrl; }
    QString apiKey() const { return m_config.apiKey; }
    int timeoutMs() const { return m_config.timeoutMs; }
    QString modelName() const { return m_config.modelName; }
    QString prompt() const { return m_config.prompt; }
    double temperature() const { return m_config.temperature; }
    int maxTokens() const { return m_config.maxTokens; }
    QString parserId() const { return m_config.parserId; }
    int bboxCoordinateRange() const { return m_config.bboxCoordinateRange; }

    // --- QML setters ---
    void setCurrentPage(int index);

    QImage currentImage() const;
    QImage pageImage(int index) const;
    OcrResult currentResult() const;

signals:
    void busyChanged();
    void resultChanged();
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
    Q_INVOKABLE void openDocument(const QUrl& fileUrl);

    Q_INVOKABLE void recognizeCurrent();
    Q_INVOKABLE void recognizeAll();

    Q_INVOKABLE void stop();

    Q_INVOKABLE bool exportPages(const QUrl& fileUrl, int scope, int fromPage = 1, int toPage = 1);

    Q_INVOKABLE bool exportResult(const QUrl& fileUrl);

    Q_INVOKABLE void applySettings(const QVariantMap& settings);

    Q_INVOKABLE void setCurrentPageText(const QString& text);
    Q_INVOKABLE void revertCurrentPageEdits();

    void loadSettings();

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
    OcrRequest buildRequest(const QImage& image) const;
    void applyRawResult(int index, const OcrResult& rawResult);

    void finishRun();
    void updateBoxesForCurrent();

    QList<Exporter::Page> collectPages(int scope, int fromPage, int toPage) const;
    QList<Exporter::Page> collectRecognizedPages() const;

    QString effectiveText(int index) const;

    std::unique_ptr<OpenAiProvider> m_provider;

    ProviderConfig m_config;
    SettingsStore m_settings;

    DocumentModel m_document;
    PageListModel m_pageModel;
    BoxListModel m_boxModel;
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
