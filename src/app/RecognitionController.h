#pragma once

#include <functional>
#include <memory>

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>

#include "core/ConnectionConfig.h"
#include "core/OcrRequest.h"
#include "models/OcrModel.h"
#include "runtime/ResolvedConnection.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class SettingsStore;
class RequestProfileStore;

class RecognitionController : public QObject
{
    Q_OBJECT

public:
    using ImageProvider = std::function<QImage(int pageIndex, QString &error)>;

    explicit RecognitionController(SettingsStore &settings,
                                   RuntimeController &runtime,
                                   RequestProfileStore &requestProfiles,
                                   ImageProvider imageProvider,
                                   QObject *parent = nullptr,
                                   std::function<bool(int)> skipPage = {});

    bool busy() const { return m_busy; }
    void startCurrent(int index, int totalPages);
    void startAll(int totalPages);
    void stop();

signals:
    void rawResultReady(int pageIndex, const llocr::OcrResult& raw);
    void statusRequested(const QString& message);

    void busyChanged();

private slots:
    void onRecognitionFinished();

private:
    void ensureConnectionReady();
    void resolveModel();
    void recognizePage(int index);
    void finishRun();
    void setBusy(bool busy);
    QString promptText() const;
    OcrRequest buildRequest(const QImage &image, const ResolvedConnection &conn) const;
    ConnectionConfig buildConfig(const ResolvedConnection &conn) const;

    SettingsStore &m_settings;
    RuntimeController &m_runtime;
    RequestProfileStore &m_requestProfiles;
    ImageProvider m_imageProvider;
    std::function<bool(int)> m_skipPage;
    int m_skippedPages = 0;

    ResolvedConnection m_connection;
    bool m_connectionReady = false;

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