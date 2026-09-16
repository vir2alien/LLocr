#pragma once

#include <QFuture>
#include <QFutureInterface>
#include <QObject>
#include <QString>

#include <QQmlEngine>

#include <memory>

#include "models/OcrModel.h"
#include "runtime/ResolvedConnection.h"

namespace llocr {

class RuntimeController;
class SettingsStore;
class RequestProfileStore;

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
                                RequestProfileStore &requestProfiles,
                                QObject *parent = nullptr);

    QFuture<SelfTestResult> runSelfTest();
    Q_INVOKABLE void runSelfTestQml();

    bool selftestRunning() const { return m_selftestRunning; }
    bool selftestOk() const { return m_selftestOk; }
    QString selftestMessage() const { return m_selftestMessage; }

    void retranslate();

signals:
    void selftestFinished();

private:
    void runSelfTestRequest(const ResolvedConnection &conn,
                            std::shared_ptr<QFutureInterface<SelfTestResult>> promise);
    static QImage makeTestImage();

private:
    SettingsStore &m_settings;
    RuntimeController &m_runtime;
    RequestProfileStore &m_requestProfiles;
    std::unique_ptr<OcrModel> m_selftestModel;

    bool m_selftestRunning = false;
    bool m_selftestOk = false;
    QString m_selftestMessage;
};

}  // namespace llocr