#include <QFutureWatcher>
#include <QImage>
#include <QPainter>

#include "app/SettingsStore.h"
#include "app/RequestProfileStore.h"
#include "core/OcrResult.h"
#include "core/ProviderConfig.h"
#include "providers/OpenAiProvider.h"
#include "runtime/ConnectionMode.h"
#include "runtime/RuntimeController.h"
#include "runtime/SelfTestController.h"

namespace llocr {

SelfTestController::SelfTestController(SettingsStore &settings,
                                       RuntimeController &runtime,
                                       RequestProfileStore &requestProfiles,
                                       QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_runtime(runtime)
    , m_requestProfiles(requestProfiles)
{
}

QFuture<SelfTestResult> SelfTestController::runSelfTest()
{
    auto promise = std::make_shared<QFutureInterface<SelfTestResult>>();
    promise->reportStarted();

    if (RuntimeController::modeFromSettings(m_settings) == ConnectionMode::External) {
        SelfTestResult r;
        r.error = tr("Self-test is available only in Managed mode");
        promise->reportResult(std::move(r));
        promise->reportFinished();
        return promise->future();
    }

    // Start (or reuse) the server first, then issue one real request. The
    // resolve machinery lives on RuntimeController (single source of truth).
    m_runtime.ensureConnectionReady([this, promise](const ResolvedConnection &conn) {
        if (conn.baseUrl.isEmpty()) {
            SelfTestResult r;
            r.error = conn.error.isEmpty() ? tr("Server not available") : conn.error;
            promise->reportResult(std::move(r));
            promise->reportFinished();
            return;
        }
        runSelfTestRequest(conn, promise);
    });
    return promise->future();
}

void SelfTestController::runSelfTestRequest(
    const ResolvedConnection &conn, std::shared_ptr<QFutureInterface<SelfTestResult>> promise)
{
    if (!m_selftestProvider)
        m_selftestProvider = new OpenAiProvider(this);

    OcrRequest request;
    request.image = makeTestImage();
    request.prompt = tr("Describe the text in this image in one short line.");
    request.modelId = conn.modelId;
    // The self-test exercises the same body shape as a real recognition run.
    request.parameters = m_requestProfiles.activeProfile().parameters;

    ProviderConfig config;
    config.baseUrl = conn.baseUrl;
    config.apiKey = conn.apiKey;
    config.timeoutMs = conn.timeoutMs;

    QFutureWatcher<OcrResult> *watch = new QFutureWatcher<OcrResult>(this);
    connect(watch, &QFutureWatcher<OcrResult>::finished, this,
            [promise, watch]() {
                const OcrResult res =
                    watch->future().resultCount() > 0 ? watch->result()
                                                      : OcrResult::makeError(QObject::tr("No response"));
                watch->deleteLater();
                SelfTestResult r;
                if (res.success) {
                    r.ok = true;
                    r.text = res.text;
                } else {
                    r.error =
                        res.errorMessage.isEmpty() ? QObject::tr("Recognition failed") : res.errorMessage;
                }
                promise->reportResult(std::move(r));
                promise->reportFinished();
            });
    watch->setFuture(m_selftestProvider->recognize(request, config));
}

void SelfTestController::runSelfTestQml()
{
    if (m_selftestRunning)
        return;
    // The External-mode guard and the resolve→request chain live in
    // runSelfTest() (single source of truth); this only mirrors its
    // SelfTestResult into the QML-visible selftest* state.
    if (RuntimeController::modeFromSettings(m_settings) == ConnectionMode::External) {
        m_selftestOk = false;
        m_selftestMessage = tr("Self-test is available only in Managed mode");
        emit selftestFinished();
        return;
    }
    m_selftestRunning = true;
    m_selftestOk = false;
    m_selftestMessage = tr("Running self-test…");
    emit selftestFinished();

    auto *watch = new QFutureWatcher<SelfTestResult>(this);
    connect(watch, &QFutureWatcher<SelfTestResult>::finished, this,
            [this, watch]() {
                const SelfTestResult r = watch->result();
                watch->deleteLater();
                m_selftestRunning = false;
                m_selftestOk = r.ok;
                m_selftestMessage = r.ok ? r.text
                                         : (r.error.isEmpty() ? tr("Recognition failed")
                                                               : r.error);
                emit selftestFinished();
            });
    watch->setFuture(runSelfTest());
}

QImage SelfTestController::makeTestImage()
{
    // Built-in synthetic test image: small white surface with a black bar so a
    // real model has something cheap to describe and the full HTTP round-trip
    // is exercised end-to-end.
    QImage img(32, 32, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.fillRect(4, 24, 24, 4, Qt::black);
    p.end();
    return img;
}

}  // namespace llocr