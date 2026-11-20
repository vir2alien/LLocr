#include "runtime/ConnectionProbe.h"

#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QtConcurrent>

#include "config/SettingsStore.h"
#include "runtime/HttpClient.h"

namespace llocr {

namespace {

constexpr int kProbeTimeoutMs = 5000;

int countModels(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return 0;
    return doc.object().value(QStringLiteral("data")).toArray().size();
}

}  // namespace

ConnectionProbe::ConnectionProbe(SettingsStore &settings, QObject *parent) : QObject(parent), m_settings(settings) {}

void ConnectionProbe::probe(const QString &baseUrl, const QString &apiKey)
{
    m_running = true;
    m_ok = false;
    m_httpStatus = 0;
    m_latencyMs = 0;
    m_modelCount = 0;
    m_errorText.clear();
    emit probeFinished();

    QString root = baseUrl.trimmed();
    while (root.endsWith(QLatin1Char('/')))
        root.chop(1);
    if (root.isEmpty()) {
        finish(false, tr("No server address"));
        return;
    }
    const QUrl url(root + QStringLiteral("/v1/models"));
    if (!url.isValid() || url.host().isEmpty()) {
        finish(false, tr("The address is not a valid URL"));
        return;
    }

    if (!apiKey.isEmpty()) {
        startRequest(url, apiKey);
        return;
    }

    // The stored key: the read can block (a locked keychain asks the user),
    // so it happens in a worker and the request follows its answer.
    SettingsStore *settings = &m_settings;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, url]() {
        watcher->deleteLater();
        startRequest(url, watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([settings]() { return settings->apiKey(); }));
}

void ConnectionProbe::reset()
{
    m_running = false;
    m_ok = false;
    m_httpStatus = 0;
    m_latencyMs = 0;
    m_modelCount = 0;
    m_errorText.clear();
    emit probeFinished();
}

void ConnectionProbe::startRequest(const QUrl &url, const QString &apiKey)
{
    if (!m_network)
        m_network = new QNetworkAccessManager(this);

    HttpClient::Options options;
    options.timeoutMs = kProbeTimeoutMs;
    if (!apiKey.isEmpty())
        options.authorization = "Bearer " + apiKey.toUtf8();

    m_timer.start();
    QNetworkReply *reply = m_network->get(HttpClient::makeRequest(url, options));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_latencyMs = int(m_timer.elapsed());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_httpStatus = status;
        const QByteArray body = reply->readAll();
        // The status is read before the transport error: a refused key is an
        // answer of the server, and the answer names the key.
        if (status == 401 || status == 403) {
            finish(false, tr("The server refused the key"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            finish(false, reply->errorString());
            return;
        }
        if (status < 200 || status >= 300) {
            finish(false, tr("The server answered with %1").arg(status));
            return;
        }
        m_modelCount = countModels(body);
        finish(true, QString());
    });
}

void ConnectionProbe::finish(bool ok, const QString &error)
{
    m_running = false;
    m_ok = ok;
    m_errorText = error;
    emit probeFinished();
}

}  // namespace llocr
