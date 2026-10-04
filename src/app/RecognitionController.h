#pragma once

#include <functional>
#include <memory>

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>

#include "core/ConnectionConfig.h"
#include "core/OcrRequest.h"
#include "core/StatusMessage.h"
#include "models/OcrModel.h"
#include "runtime/ResolvedConnection.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class SettingsStore;
class RequestProfileStore;

enum class PageSkip {
    None,
    Unreadable,  ///< a blank replacement for a page the source could not decode
    AlreadyRecognized,
};

class RecognitionController : public QObject
{
    Q_OBJECT

public:
    using ImageProvider = std::function<QImage(int pageIndex, QString &error)>;
    using SkipResolver = std::function<PageSkip(int pageIndex, bool batch)>;

    explicit RecognitionController(
        SettingsStore &settings, RuntimeController &runtime, RequestProfileStore &requestProfiles, ImageProvider imageProvider, QObject *parent = nullptr, SkipResolver skipPage = {});

    bool busy() const { return m_busy; }
    void startCurrent(int index, int totalPages);
    void startAll(int totalPages);
    void stop();

signals:
    void rawResultReady(int pageIndex, const llocr::OcrResult &raw);
    void statusRequested(const StatusMessage &message);

    void busyChanged();

private slots:
    void onRecognitionFinished();

private:
    void ensureConnectionReady();
    void resolveModel();
    int firstPageToRecognize(int from);
    void recognizePage(int index);
    void finishRun();
    void setBusy(bool busy);
    QString promptText() const;
    StatusMessage skipReport() const;
    OcrRequest buildRequest(const QImage &image, const ResolvedConnection &conn) const;

    SettingsStore &m_settings;
    RuntimeController &m_runtime;
    RequestProfileStore &m_requestProfiles;
    ImageProvider m_imageProvider;
    SkipResolver m_skipPage;
    int m_skippedUnreadable = 0;
    int m_skippedRecognized = 0;

    ResolvedConnection m_connection;

    std::unique_ptr<OcrModel> m_model;
    QString m_modelId;
    QFutureWatcher<OcrResult> m_watcher;

    int m_totalPages = 0;
    int m_startIndex = 0;
    bool m_busy = false;
    bool m_stopRequested = false;
    bool m_recognizeAll = false;
    int m_recognizingIndex = -1;
};

}  // namespace llocr