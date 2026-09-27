#include "runtime/HttpClient.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QTimer>

namespace llocr {

namespace {

// A NAM per thread: QNetworkAccessManager is not thread-safe, and the catalog
// fetches run on workers while the runtime's own calls run on the GUI thread.
QNetworkAccessManager &networkAccessManager()
{
    static thread_local QNetworkAccessManager manager;
    return manager;
}

}  // namespace

void HttpClient::applyProcessDefaults()
{
    static const bool applied = [] {
        QNetworkProxyFactory::setUseSystemConfiguration(true);
        return true;
    }();
    Q_UNUSED(applied)
}

QNetworkRequest HttpClient::makeRequest(const QUrl &url, const Options &options)
{
    applyProcessDefaults();
    QNetworkRequest request(url);
    // Manual: the caller follows the redirect, so the Authorization header can
    // be dropped when the host changes (HttpClient::redirectFor).
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    if (options.timeoutMs > 0)
        request.setTransferTimeout(options.timeoutMs);
    for (auto it = options.headers.constBegin(); it != options.headers.constEnd(); ++it)
        request.setRawHeader(it.key(), it.value());
    if (!options.authorization.isEmpty())
        request.setRawHeader("Authorization", options.authorization);
    return request;
}

HttpClient::Redirect HttpClient::redirectFor(const QNetworkReply *reply, const QUrl &current, int hopsUsed, int maxHops, bool allowInsecure)
{
    Redirect redirect;
    const QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (!target.isValid())
        return redirect;

    redirect.present = true;
    redirect.target = current.resolved(target);
    redirect.allowed = hopsUsed < maxHops && redirect.target.isValid() &&
                       (redirect.target.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 ||
                        (allowInsecure && (redirect.target.host() == QStringLiteral("127.0.0.1") || redirect.target.host() == QStringLiteral("::1") ||
                                           redirect.target.host().compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0)));
    redirect.crossHost = redirect.target.host() != current.host();
    return redirect;
}

HttpClient::Response HttpClient::get(const QUrl &url, const Options &options, int maxRedirects, bool allowInsecure)
{
    return get(nullptr, url, options, maxRedirects, allowInsecure);
}

HttpClient::Response HttpClient::get(QNetworkAccessManager *nam, const QUrl &url, const Options &options, int maxRedirects, bool allowInsecure)
{
    QNetworkAccessManager &manager = nam ? *nam : networkAccessManager();
    Response response;
    QUrl current = url;
    QByteArray authorization = options.authorization;
    int hops = 0;

    while (true) {
        Options hop = options;
        hop.authorization = authorization;
        QNetworkReply *reply = manager.get(makeRequest(current, hop));
        if (!reply) {
            response.error = QStringLiteral("no network manager");
            return response;
        }

        bool timedOut = false;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QEventLoop loop;
        QObject::connect(&watchdog, &QTimer::timeout, &loop, [&]() {
            timedOut = true;
            reply->abort();
        });
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        watchdog.start(options.timeoutMs > 0 ? options.timeoutMs : 30000);
        loop.exec();
        watchdog.stop();

        if (timedOut) {
            reply->deleteLater();
            response.timedOut = true;
            response.error = QStringLiteral("request timed out");
            return response;
        }

        const Redirect redirect = redirectFor(reply, current, hops, maxRedirects, allowInsecure);
        if (redirect.present) {
            reply->deleteLater();
            if (!redirect.allowed) {
                response.error = QStringLiteral("refused to follow the redirect");
                return response;
            }
            if (redirect.crossHost)
                authorization.clear();
            current = redirect.target;
            ++hops;
            continue;
        }

        response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        response.body = reply->readAll();
        response.headers = reply->rawHeaderPairs();  // e.g. the HF "Link" pagination
        if (reply->error() != QNetworkReply::NoError)
            response.error = reply->errorString();
        reply->deleteLater();
        return response;
    }
}

}  // namespace llocr
