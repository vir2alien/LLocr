#pragma once

#include <functional>
#include <memory>

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>

#include "core/ProviderConfig.h"
#include "providers/OpenAiProvider.h"
#include "runtime/ResolvedConnection.h"
#include "runtime/RuntimeController.h"

namespace llocr {

class SettingsStore;
class RequestProfileStore;

class RecognitionController : public QObject
{
    Q_OBJECT

public:
    using ImageProvider = std::function<QImage(int pageIndex)>;

    explicit RecognitionController(SettingsStore &settings,
                                   RuntimeController &runtime,
                                   RequestProfileStore &requestProfiles,
                                   ImageProvider imageProvider,
                                   QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    void startCurrent(int index, int totalPages, const QString& prompt);
    void startAll(int totalPages, const QString& prompt);
    void stop();

signals:
    void rawResultReady(int pageIndex, const llocr::OcrResult& raw);
    void statusRequested(const QString& message);

    void busyChanged();

private slots:
    void onRecognitionFinished();

private:
    void ensureConnectionReady();
    void recognizePage(int index);
    void finishRun();
    void setBusy(bool busy);
    OcrRequest buildRequest(const QImage &image, const ResolvedConnection &conn) const;
    ProviderConfig buildConfig(const ResolvedConnection &conn) const;

    SettingsStore &m_settings;
    RuntimeController &m_runtime;
    RequestProfileStore &m_requestProfiles;
    ImageProvider m_imageProvider;

    ResolvedConnection m_connection;
    bool m_connectionReady = false;

    std::unique_ptr<OpenAiProvider> m_provider;
    QFutureWatcher<OcrResult> m_watcher;

    QString m_prompt;

    int m_totalPages = 0;
    int m_startIndex = 0;
    bool m_busy = false;
    bool m_stopRequested = false;
    bool m_recognizeAll = false;
    int m_recognizingIndex = -1;
};

}  // namespace llocr