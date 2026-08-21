#pragma once

#include <functional>
#include <memory>

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>

#include "core/ProviderConfig.h"
#include "providers/OpenAiProvider.h"

namespace llocr {

class SettingsStore;

class RecognitionController : public QObject
{
    Q_OBJECT

public:
    using ImageProvider = std::function<QImage(int pageIndex)>;

    explicit RecognitionController(SettingsStore &settings,
                                   ImageProvider imageProvider,
                                   QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    void startCurrent(int index, int totalPages, const QString& prompt);
    void startAll(int totalPages, const QString& prompt);
    void stop();

signals:
    void rawResultReady(int pageIndex, const llocr::OcrResult& raw);
    void runFinished();
    void statusRequested(const QString& message);

    void busyChanged();

private slots:
    void onRecognitionFinished();

private:
    void recognizePage(int index);
    void finishRun();
    void setBusy(bool busy);
    OcrRequest buildRequest(const QImage &image, const QString &prompt) const;
    ProviderConfig buildConfig() const;

    SettingsStore &m_settings;
    ImageProvider m_imageProvider;

    std::unique_ptr<OpenAiProvider> m_provider;
    QFutureWatcher<OcrResult> m_watcher;

    QString m_prompt;

    int m_totalPages = 0;
    bool m_busy = false;
    bool m_stopRequested = false;
    bool m_recognizeAll = false;
    int m_recognizingIndex = -1;
};

}  // namespace llocr