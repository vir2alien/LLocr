#pragma once

#include <QImage>
#include <QObject>
#include <QUrl>
#include <QVariantMap>

#include "app/BoxListModel.h"
#include "app/DocumentModel.h"
#include "app/Exporter.h"
#include "app/PageListModel.h"
#include "app/PageEditStore.h"
#include "app/RecognitionController.h"
#include "app/SettingsStore.h"
#include "core/OcrResult.h"

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
    Q_PROPERTY(int imageRevision READ imageRevision NOTIFY imageRevisionChanged)
    Q_PROPERTY(int docRevision READ docRevision NOTIFY docRevisionChanged)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY resultChanged)

    Q_PROPERTY(bool currentPageEditable READ currentPageEditable NOTIFY resultChanged)
    Q_PROPERTY(bool currentPageEdited READ currentPageEdited NOTIFY editStateChanged)

    Q_PROPERTY(QStringList exportNameFilters READ exportNameFilters CONSTANT)

    Q_PROPERTY(QObject* pageModel READ pageModel CONSTANT)
    Q_PROPERTY(QObject* boxModel READ boxModel CONSTANT)

    Q_PROPERTY(QString prompt READ prompt WRITE setPrompt NOTIFY promptChanged)

    Q_PROPERTY(bool canRecognize READ canRecognize NOTIFY configChanged)

    Q_PROPERTY(QStringList parserNames READ parserNames CONSTANT)

public:
    explicit AppController(SettingsStore &settings, QObject *parent = nullptr);

    // --- QML getters ---
    bool busy() const { return m_recognition.busy(); }
    QString resultText() const;
    QString statusMessage() const { return m_statusMessage; }
    bool hasImage() const;
    bool hasResult() const;
    int pageCount() const { return m_document.pageCount(); }
    int currentPage() const { return m_currentPage; }
    int imageRevision() const { return m_imageRevision; }
    int docRevision() const { return m_docRevision; }

    bool canRecognize() const;
    QStringList parserNames() const;

    QStringList exportNameFilters() const;

    bool currentPageEditable() const;
    bool currentPageEdited() const;

    /// UI-facing page/box models. Deliberately non-const: a const accessor would
    /// have to return const QObject*, forcing a const_cast, and QML delegates
    /// need the mutable model instance.
    QObject *pageModel() { return &m_pageModel; }
    QObject *boxModel() { return &m_boxModel; }

    QString prompt() const { return m_prompt; }

    // --- QML setters ---

    void setPrompt(const QString &prompt);
    void setCurrentPage(int index);

    QImage currentImage() const;
    QImage pageImage(int index) const;

    /// Crops the image-block of a page (used by OcrImageProvider and export).
    QImage croppedImage(int pageIndex, int boxIndex) const;

signals:
    void busyChanged();
    void resultChanged();
    void promptChanged();
    void statusChanged();
    void imageChanged();
    void documentChanged();
    void pageChanged();
    void imageRevisionChanged();
    void docRevisionChanged();
    void configChanged();
    void boxesChanged();

    void editStateChanged();

public slots:
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

    /// Replaces image://ocr/crop/<N> with data: URIs for WebEngine preview.
    Q_INVOKABLE QString resolveImagesForPreview(const QString& markdown) const;

private:
    // Values are mirrored by raw ints in Main.qml (0 = all, 1 = current, 2 = range).
    enum ExportScope : int {
        ExportAll = 0,
        ExportCurrent = 1,
        ExportRange = 2,
    };

    void setStatus(const QString& message);

    void notifyDocumentChanged();
    void notifyPageChanged();

    void applyRawResult(int index, const OcrResult& rawResult);

    void updateBoxesForCurrent();

    QList<Exporter::Page> collectPages(int scope, int fromPage, int toPage) const;

    QString effectiveText(int index) const;

    SettingsStore &m_settings;

    DocumentModel m_document;
    PageListModel m_pageModel;
    BoxListModel m_boxModel;

    RecognitionController m_recognition;

    QString m_prompt = "document parsing.";

    Exporter m_exporter;

    int m_currentPage = 0;
    QString m_statusMessage;

    int m_imageRevision = 0;
    int m_docRevision = 0;

    PageEditStore m_editStore;
};

}  // namespace llocr
