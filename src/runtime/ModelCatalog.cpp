#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

#include "runtime/ModelCatalog.h"

namespace llocr {

namespace {

const QRegularExpression kSplitRe(
    QStringLiteral(R"(^(.*)-(\d{3,5})-of-(\d{3,5})\.gguf$)"));

const QRegularExpression kQuantRe(QStringLiteral(
    "(?:^|[^A-Za-z0-9])("
    "Q[3456]_K_[SML]|IQ[124][0-9]?_[A-Z_]+|Q[0-9]_[01]|Q[3456]_K|B[2-8]_0|"
    "F16|BF16|FP8|TF32|F8"
    ")(?:[^A-Za-z0-9]|$)"));

QString quantInName(const QString &name)
{
    if (!name.endsWith(QLatin1String(".gguf"), Qt::CaseInsensitive))
        return QString();
    QRegularExpressionMatchIterator it = kQuantRe.globalMatch(name);
    QString last;
    while (it.hasNext())
        last = it.next().captured(1);
    return last;
}

struct GetResult {
    int status = -1;
    QByteArray body;
    QByteArray linkHeader;
    QString error;  // transport-level failure description (status <= 0)
};

QNetworkReply *issueGet(QNetworkAccessManager *nam, const QUrl &url,
                        const QByteArray &authorization)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    if (!authorization.isEmpty())
        req.setRawHeader("Authorization", authorization);
    return nam->get(req);
}

bool waitForReply(QNetworkReply *reply, int timeoutMs)
{
    bool timedOut = false;
    QTimer timer;
    timer.setSingleShot(true);
    QEventLoop loop;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    timer.stop();
    if (timedOut) {
        reply->deleteLater();
        return false;
    }
    return true;
}

GetResult pullGet(QNetworkAccessManager *nam, const QUrl &start,
                  const QByteArray &authorization, int timeoutMs)
{
    QUrl url = start;
    QByteArray auth = authorization;
    for (int hop = 0; hop <= ModelCatalog::kMaxRedirects; ++hop) {
        QNetworkReply *reply = issueGet(nam, url, auth);
        if (!reply)
            return GetResult{};
        if (!waitForReply(reply, timeoutMs)) {
            GetResult res;
            res.error = QObject::tr("request timed out");
            return res;
        }
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status >= 300 && status < 400) {
            const QUrl target =
                reply->attribute(QNetworkRequest::RedirectionTargetAttribute)
                    .toUrl();
            const QUrl next = target.isValid() ? url.resolved(target) : QUrl();
            if (!next.isValid()) {
                GetResult res;
                res.error = reply->errorString();
                reply->deleteLater();
                return res;
            }
            reply->deleteLater();
            if (next.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive)
                != 0) {
                GetResult res;
                res.error = QObject::tr("insecure redirect to %1 blocked")
                                .arg(next.toString(QUrl::FullyEncoded));
                return res;
            }
            if (next.host() != url.host())
                auth = QByteArray();
            url = next;
            continue;
        }

        GetResult res;
        res.status = status;
        res.body = reply->readAll();
        res.linkHeader = reply->rawHeader(QByteArrayLiteral("Link"));
        if (reply->error() != QNetworkReply::NoError)
            res.error = reply->errorString();
        reply->deleteLater();
        return res;
    }
    return GetResult{};
}

}  // namespace

QList<HfFile> ModelCatalog::parseTreeJson(const QJsonArray &items, QString &error)
{
    QList<HfFile> out;
    for (const QJsonValue &v : items) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        const QString path = o.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            continue;
        HfFile f;
        f.path = path;
        f.name = leafName(path);
        const QString type =
            o.value(QStringLiteral("type")).toString().toLower();
        f.isDir = (type == QLatin1String("directory")
                   || type == QLatin1String("folder"));
        f.type = type;

        const QJsonValue sizeV = o.value(QStringLiteral("size"));
        if (sizeV.isDouble())
            f.size = static_cast<qint64>(sizeV.toDouble(0));

        const QJsonObject lfs = o.value(QStringLiteral("lfs")).toObject();
        if (!lfs.isEmpty()) {
            f.isLfs = true;
            f.lfsOid = lfs.value(QStringLiteral("oid")).toString().toLower();
        }
        out.append(std::move(f));
    }
    if (out.isEmpty() && !items.isEmpty())
        error = QObject::tr("The Hugging Face tree response contained no files");
    return out;
}

QUrl ModelCatalog::nextPageUrl(const QByteArray &linkHeader)
{
    for (const QByteArray &part : linkHeader.split(',')) {
        const int lt = part.indexOf('<');
        const int gt = part.indexOf('>');
        if (lt < 0 || gt < 0 || gt <= lt)
            continue;
        const QByteArray urlPart = part.mid(lt + 1, gt - lt - 1).trimmed();
        if (!part.contains(QByteArray("rel")) || !part.contains(QByteArray("next")))
            continue;
        if (urlPart.isEmpty())
            continue;
        return QUrl(QString::fromUtf8(urlPart));
    }
    return QUrl();
}

QString ModelCatalog::leafName(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? path.mid(slash + 1) : path;
}

QStringList ModelCatalog::allGguf(const QStringList &names)
{
    QStringList out;
    for (const QString &n : names) {
        if (n.endsWith(QLatin1String(".gguf"), Qt::CaseInsensitive))
            out << n;
    }
    return out;
}

ModelFileKind ModelCatalog::fileKind(const QString &name)
{
    if (!name.endsWith(QLatin1String(".gguf"), Qt::CaseInsensitive))
        return ModelFileKind::NotModel;
    if (name.contains(QLatin1String("mmproj"), Qt::CaseInsensitive))
        return ModelFileKind::Vision;
    return ModelFileKind::Model;
}

bool ModelCatalog::isMultiPart(const QString &name)
{
    return kSplitRe.match(name).hasMatch();
}

bool ModelCatalog::splitMultiPart(const QString &name, QString *baseOut,
                                  int *indexOut, int *countOut)
{
    const QRegularExpressionMatch m = kSplitRe.match(name);
    if (!m.hasMatch())
        return false;
    if (baseOut)
        *baseOut = m.captured(1);
    if (indexOut)
        *indexOut = m.captured(2).toInt();
    if (countOut)
        *countOut = m.captured(3).toInt();
    return true;
}

bool ModelCatalog::splitAscending(const QString &a, const QString &b)
{
    QString bA, bB;
    int iA = 0, cA = 0, iB = 0, cB = 0;
    const bool mA = splitMultiPart(a, &bA, &iA, &cA);
    const bool mB = splitMultiPart(b, &bB, &iB, &cB);
    if (mA && mB && bA == bB && iA != iB)
        return iA < iB;
    return a < b;
}

QString ModelCatalog::quantizationFromName(const QString &name)
{
    return quantInName(name);
}

QString ModelCatalog::encodePath(const QString &path)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(
        path, QByteArrayLiteral("/")));
}

QUrl ModelCatalog::resolveUrl(const QString &repo, const QString &commitSha,
                              const QString &path)
{
    return QUrl(QStringLiteral("https://huggingface.co/%1/resolve/%2/%3")
                    .arg(encodePath(repo), encodePath(commitSha), encodePath(path)));
}

QString ModelCatalog::fetchHeadSha(QNetworkAccessManager *nam, const QString &repo,
                                   QString &error, const QByteArray &authorization,
                                   int timeoutMs, const QUrl &baseUrl)
{
    const QString origin = baseUrl.isValid()
        ? baseUrl.toString(QUrl::FullyEncoded)
        : QStringLiteral("https://huggingface.co");
    const QUrl url(origin + QStringLiteral("/api/models/%1").arg(encodePath(repo)));
    const GetResult res = pullGet(nam, url, authorization, timeoutMs);
    if (res.status != 200) {
        error = res.status > 0
            ? QObject::tr("Hugging Face API returned HTTP %1 for %2")
                  .arg(res.status).arg(repo)
            : QObject::tr("Hugging Face request failed for %1: %2")
                  .arg(repo, res.error);
        return QString();
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(res.body, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        error = QObject::tr("Malformed model info from Hugging Face");
        return QString();
    }
    const QString sha = doc.object().value(QStringLiteral("sha")).toString();
    if (sha.isEmpty()) {
        error = QObject::tr("Hugging Face response has no commit SHA for %1")
                    .arg(repo);
        return QString();
    }
    return sha;
}

QList<HfFile> ModelCatalog::fetchTree(QNetworkAccessManager *nam, const QString &repo,
                                      const QString &commitSha, QString &error,
                                      const QByteArray &authorization, int timeoutMs,
                                      const QUrl &baseUrl)
{
    QList<HfFile> all;
    const QString origin = baseUrl.isValid()
        ? baseUrl.toString(QUrl::FullyEncoded)
        : QStringLiteral("https://huggingface.co");
    QString pageUrl = origin +
        QStringLiteral("/api/models/%1/tree/%2?recursive=true")
            .arg(encodePath(repo), encodePath(commitSha));

    int guard = 50;
    while (guard-- > 0) {
        const GetResult res = pullGet(nam, QUrl(pageUrl), authorization, timeoutMs);
        if (res.status != 200) {
            error = res.status > 0
                ? QObject::tr("Hugging Face API returned HTTP %1 for tree of %2")
                      .arg(res.status).arg(repo)
                : QObject::tr("Hugging Face request failed for tree of %1: %2")
                      .arg(repo, res.error);
            return QList<HfFile>();
        }
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(res.body, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isArray()) {
            error = QObject::tr("Malformed tree response from Hugging Face");
            return QList<HfFile>();
        }
        QString parseErr;
        all.append(parseTreeJson(doc.array(), parseErr));

        const QUrl next = nextPageUrl(res.linkHeader);
        if (!next.isValid())
            break;
        pageUrl = next.toString();
    }
    return all;
}

}  // namespace llocr