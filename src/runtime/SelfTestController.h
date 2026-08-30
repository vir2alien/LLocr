#pragma once

#include <QFuture>
#include <QFutureInterface>
#include <QObject>
#include <QString>

#include <QQmlEngine>

#include <memory>

#include "runtime/ResolvedConnection.h"

namespace llocr {

class OpenAiProvider;
class RuntimeController;
class SettingsStore;

// QML-facing self-test state + entry point, extracted from the RuntimeController
// facade (§ review 3.4). The actual resolve→request chain is driven through
// RuntimeController::ensureConnectionReady() — this class is a separate consumer
// of the facade's resolve API, which is exactly what the review wanted: the
// facade no longer carries the selftest* surface and the temptation to duplicate
// it (review 3.1) is gone.
class SelfTestController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool selftestRunning READ selftestRunning NOTIFY selftestFinished)
    Q_PROPERTY(bool selftestOk READ selftestOk NOTIFY selftestFinished)
    Q_PROPERTY(QString selftestMessage READ selftestMessage NOTIFY selftestFinished)

public:
    explicit SelfTestController(SettingsStore &settings, RuntimeController &runtime,
                                QObject *parent = nullptr);

    /// Runs an independent self-test (used by the master wizard "Check" button).
    /// Starts the managed server if needed, waits for readiness, then issues one
    /// real OCR request against a built-in test image and returns the text. In
    /// External this reports NotConfigured (there is nothing to self-test here).
    QFuture<SelfTestResult> runSelfTest();

    /// QML-friendly variant: starts the self-test and reports progress via the
    /// `selftest*` properties / `selftestFinished` signal (QFuture is unusable
    /// from QML). No-op while already running.
    Q_INVOKABLE void runSelfTestQml();

    bool selftestRunning() const { return m_selftestRunning; }
    bool selftestOk() const { return m_selftestOk; }
    QString selftestMessage() const { return m_selftestMessage; }

signals:
    void selftestFinished();

private:
    void runSelfTestRequest(const ResolvedConnection &conn,
                            std::shared_ptr<QFutureInterface<SelfTestResult>> promise);
    static QImage makeTestImage();

    SettingsStore &m_settings;
    RuntimeController &m_runtime;
    OpenAiProvider *m_selftestProvider = nullptr;

    bool m_selftestRunning = false;
    bool m_selftestOk = false;
    QString m_selftestMessage;
};

}  // namespace llocr