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

/// One place for the HTTP policy the runtime layer used to spell out four times
/// (ADR 108): a transfer timeout, manual redirect following, and the rule that
/// the `Authorization` header never follows a redirect to another host.
///
/// It is deliberately *not* a network-access-manager wrapper: callers keep
/// ownership of the manager they already have (a task, a controller, a worker),
/// and only the policy is shared.
class HttpClient
{
public:
    struct Options {
        int timeoutMs = 30000;
        QByteArray authorization;                  ///< first hop only
        QHash<QByteArray, QByteArray> headers;     ///< extra request headers
    };

    /// Process-wide network defaults (system proxy). Idempotent, and called by
    /// every call site that touches the network — previously it happened as a
    /// side effect of constructing DownloadManager, so a build that never
    /// constructed one silently ignored the user's proxy settings.
    static void applyProcessDefaults();

    /// A request carrying the shared policy.
    static QNetworkRequest makeRequest(const QUrl &url, const Options &options);

    struct Redirect {
        bool present = false;   ///< the reply asked for a redirect
        QUrl target;            ///< resolved against the current URL
        bool allowed = false;    ///< hop budget left, scheme accepted
        bool crossHost = false; ///< caller must drop the Authorization header
    };

    /// Interprets a finished reply's redirect, if any.
    static Redirect redirectFor(const QNetworkReply *reply, const QUrl &current,
                                int hopsUsed, int maxHops = 5,
                                bool allowInsecure = false);

    struct Response {
        int status = 0;          ///< HTTP status, 0 when the request never completed
        QByteArray body;
        QString error;           ///< transport-level failure description
        bool timedOut = false;
        QList<QNetworkReply::RawHeaderPair> headers;  ///< final reply's raw headers
    };

    /// Blocking GET with a watchdog timer, following redirects under the same
    /// policy. Only for call sites that already run on a worker thread (the
    /// catalogs); never call it from the GUI thread (ADR 105).
    static Response get(const QUrl &url, const Options &options, int maxRedirects = 5,
                        bool allowInsecure = false);

    /// Same, on a caller-supplied manager (kept for call sites that already own
    /// one). `nam` may be null, in which case the per-thread manager is used.
    static Response get(QNetworkAccessManager *nam, const QUrl &url, const Options &options,
                        int maxRedirects = 5, bool allowInsecure = false);
};

}  // namespace llocr
