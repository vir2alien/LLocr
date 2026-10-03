#pragma once

#include <QByteArray>
#include <QHash>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace llocr {

class HttpClient
{
public:
    struct Options {
        int timeoutMs = 30000;
        QByteArray authorization;               ///< first hop only
        QHash<QByteArray, QByteArray> headers;  ///< extra request headers
    };

    static void applyProcessDefaults();

    static QNetworkRequest makeRequest(const QUrl &url, const Options &options);

    static bool isAllowedUrl(const QUrl &url, bool allowInsecure);

    struct Redirect {
        bool present = false;    ///< the reply asked for a redirect
        QUrl target;             ///< resolved against the current URL
        bool allowed = false;    ///< hop budget left, scheme accepted
        bool crossHost = false;  ///< caller must drop the Authorization header
    };

    static Redirect redirectFor(const QNetworkReply *reply, const QUrl &current, int hopsUsed, int maxHops = 5, bool allowInsecure = false);

    struct Response {
        int status = 0;  ///< HTTP status, 0 when the request never completed
        QByteArray body;
        QString error;  ///< transport-level failure description
        bool timedOut = false;
        QList<QNetworkReply::RawHeaderPair> headers;  ///< final reply's raw headers
    };

    static Response get(const QUrl &url, const Options &options, int maxRedirects = 5, bool allowInsecure = false);

    static Response get(QNetworkAccessManager *nam, const QUrl &url, const Options &options, int maxRedirects = 5, bool allowInsecure = false);
};

}  // namespace llocr
