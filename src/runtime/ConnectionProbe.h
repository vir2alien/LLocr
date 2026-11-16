#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace llocr {

class SettingsStore;

// Asks an OpenAI-compatible server whether it answers: the settings take a
// URL and a key, and this is what says whether they work. The stored key is
// read in the C++ and never travels through the UI.
class ConnectionProbe : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool running READ running NOTIFY probeFinished)
    Q_PROPERTY(bool ok READ ok NOTIFY probeFinished)
    Q_PROPERTY(int httpStatus READ httpStatus NOTIFY probeFinished)
    Q_PROPERTY(int latencyMs READ latencyMs NOTIFY probeFinished)
    Q_PROPERTY(int modelCount READ modelCount NOTIFY probeFinished)
    Q_PROPERTY(QString errorText READ errorText NOTIFY probeFinished)

public:
    explicit ConnectionProbe(SettingsStore &settings, QObject *parent = nullptr);

    // An empty key means the shared key of the settings.
    Q_INVOKABLE void probe(const QString &baseUrl, const QString &apiKey = QString());

    // Throws the last answer away: the address the user types makes the old
    // one stale.
    Q_INVOKABLE void reset();

    bool running() const { return m_running; }
    bool ok() const { return m_ok; }
    int httpStatus() const { return m_httpStatus; }
    int latencyMs() const { return m_latencyMs; }
    int modelCount() const { return m_modelCount; }
    QString errorText() const { return m_errorText; }

signals:
    void probeFinished();

private:
    void startRequest(const QUrl &url, const QString &apiKey);
    void finish(bool ok, const QString &error);

    SettingsStore &m_settings;
    QNetworkAccessManager *m_network = nullptr;
    QElapsedTimer m_timer;
    bool m_running = false;
    bool m_ok = false;
    int m_httpStatus = 0;
    int m_latencyMs = 0;
    int m_modelCount = 0;
    QString m_errorText;
};

}  // namespace llocr
