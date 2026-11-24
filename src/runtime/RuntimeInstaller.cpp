#include <algorithm>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QUrl>
#include <utility>

#include <QtConcurrent>

#include "config/SettingsStore.h"
#include "runtime/ArchiveExtractor.h"
#include "runtime/DownloadGroup.h"
#include "runtime/DownloadManager.h"
#include "runtime/DownloadTask.h"
#include "runtime/InstallTransaction.h"
#include "runtime/ReleaseCatalog.h"
#include "runtime/RuntimeInstaller.h"

namespace llocr {

namespace {

QString osLabel(const PlatformInfo &info)
{
    switch (info.os) {
    case PlatformOs::Windows:
        return RuntimeInstaller::tr("Windows");
    case PlatformOs::macOS:
        return RuntimeInstaller::tr("macOS");
    case PlatformOs::Linux:
        return RuntimeInstaller::tr("Linux");
    }
    return RuntimeInstaller::tr("Linux");
}

bool backendMatches(const QString &assetBackend, const QString &requested)
{
    if (assetBackend.isEmpty())
        return true;
    if (assetBackend == requested)
        return true;
    return assetBackend.startsWith(requested + QLatin1Char('-'));
}

QString normalizedChannel(const QString &channel)
{
    return channel == QLatin1String("beta") ? QStringLiteral("beta") : QStringLiteral("stable");
}

// A v-tag carries no build number of its own; the installer labels the release
// with the build its assets resolve to ("v1.0.0 (b12359)"), while a beta tag
// already is the build.
QString taggedName(const ReleaseInfo &release)
{
    const QString build = QStringLiteral("b%1").arg(release.build);
    if (release.build > 0 && release.tagName != build)
        return QStringLiteral("%1 (%2)").arg(release.tagName, build);
    return release.tagName;
}

struct CatalogLoad {
    QList<ReleaseInfo> releases;
    QString error;
    QString note;
};

// The build number of an installed build tag ("b12359" → 12359). Zero when the
// tag carries no number.
int buildNumber(const QString &buildTag)
{
    if (!buildTag.startsWith(QLatin1Char('b')))
        return 0;
    bool ok = false;
    const int number = buildTag.mid(1).toInt(&ok);
    return ok ? number : 0;
}

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    if (bytes >= 1073741824LL)
        return QStringLiteral("%1 GiB").arg(double(bytes) / 1073741824.0, 0, 'f', 1);
    if (bytes >= 1048576LL)
        return QStringLiteral("%1 MiB").arg(double(bytes) / 1048576.0, 0, 'f', 0);
    return QStringLiteral("%1 KiB").arg(double(bytes) / 1024.0, 0, 'f', 0);
}

}  // namespace

RuntimeInstaller::RuntimeInstaller(SettingsStore &settings, InstalledState &state, QObject *parent, const QString &catalogApiBase)
    : QObject(parent), m_settings(settings), m_installState(state), m_channel(normalizedChannel(settings.runtimeChannel())), m_catalogApiBase(catalogApiBase),
      m_installedBuilds(new InstalledBuildsModel(settings, this)), m_downloads(new DownloadManager(this)), m_group(new DownloadGroup(m_downloads, this)),
      m_installLock([this]() -> QLockFile & { return m_installState.installLock(); })
{
    const PlatformInfo info = ReleaseCatalog::detectPlatform();
    m_platformLabel = QStringLiteral("%1 %2").arg(osLabel(info), info.arch);
    m_recommendedBackend = info.backend;
    m_recommendationReason = info.backendReason;
    m_availableBackends = ReleaseCatalog::backendsFor(info);
    if (m_availableBackends.contains(m_recommendedBackend))
        m_backend = m_recommendedBackend;
    else if (!m_availableBackends.isEmpty())
        m_backend = m_availableBackends.first();

    connect(m_group, &DownloadGroup::progressChanged, this, [this]() {
        setProgress(m_group->progress());
        if (m_state == State::Downloading) {
            setStatusMessage(tr("Downloading %1 …").arg(m_pendingMain.fileName.isEmpty() ? tr("runtime") : m_pendingMain.fileName));
        }
    });
    connect(m_group, &DownloadGroup::allFinished, this, [this](bool) { maybeFinishDownloads(); });
    connect(&m_settings, &SettingsStore::serverPathChanged, m_installedBuilds, &InstalledBuildsModel::settingsChanged);
    connect(&m_installState, &InstalledState::pathsChanged, this, [this]() {
        rescanInstalledBuilds();
        recomputeHasUpdate();
    });

    m_installState.ensureDirectories();
    rescanInstalledBuilds();
    reconcileInstalledBackend();
}

RuntimeInstaller::~RuntimeInstaller()
{
    if (m_downloads)
        m_downloads->cancelAll(true);
}

void RuntimeInstaller::shutdown()
{
    if (m_downloads)
        m_downloads->cancelAll(true);
    releaseInstallLock();
}

void RuntimeInstaller::setState(State next)
{
    if (m_state == next)
        return;
    m_state = next;
    emit stateChanged();
}

void RuntimeInstaller::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void RuntimeInstaller::setChannel(const QString &channel)
{
    const QString normalized = normalizedChannel(channel);
    if (normalized == m_channel || m_busy)
        return;
    m_channel = normalized;
    m_settings.setRuntimeChannel(m_channel);
    emit channelChanged();
    if (m_state == State::Idle)
        return;
    m_releases.clear();
    setSelectedRelease(0);
    emit catalogChanged();
    startCatalogFetch();
}

void RuntimeInstaller::setBackend(const QString &backend)
{
    if (m_backend == backend)
        return;
    m_backend = backend;
    emit backendChanged();
    // The size of the download belongs to the backend.
    emit candidateChanged();
}

void RuntimeInstaller::setSelectedRelease(int index)
{
    if (m_selectedRelease == index)
        return;
    m_selectedRelease = index;
    emit selectedReleaseChanged();
    emit candidateChanged();
}

void RuntimeInstaller::setStatusMessage(const QString &msg)
{
    if (m_statusMessage == msg)
        return;
    m_statusMessage = msg;
    emit statusMessageChanged();
}

void RuntimeInstaller::retranslate()
{
    switch (m_state) {
    case State::Fetching:
        setStatusMessage(tr("Checking for updates…"));
        break;
    case State::Downloading:
        setStatusMessage(tr("Downloading %1 …").arg(m_pendingMain.fileName.isEmpty() ? tr("runtime") : m_pendingMain.fileName));
        break;
    case State::Installing:
        setStatusMessage(tr("Installing %1 …").arg(backendDisplayName(m_pendingBackend)));
        break;
    case State::Idle:
    case State::Ready:
    case State::Installed:
    case State::Error:
        break;
    }
}

void RuntimeInstaller::setProgress(double p)
{
    if (qFuzzyCompare(m_progress, p) || p < 0.0 || p > 1.0)
        return;
    m_progress = p;
    emit progressChanged();
}

QString RuntimeInstaller::installedBuild() const
{
    return m_settings.installedBuild();
}

QString RuntimeInstaller::installedBackend() const
{
    return m_settings.runtimeBackend();
}

bool RuntimeInstaller::canInstall() const
{
    return stateInt() == State::Ready && !backend().isEmpty();
}

QString RuntimeInstaller::backendDisplayName(const QString &backend)
{
    static const QHash<QString, QString> wellKnown = {
        {QStringLiteral("cpu"), QStringLiteral("CPU")},
        {QStringLiteral("cuda"), QStringLiteral("CUDA")},
        {QStringLiteral("vulkan"), QStringLiteral("Vulkan")},
        {QStringLiteral("metal"), QStringLiteral("Metal")},
        {QStringLiteral("hip"), QStringLiteral("HIP")},
        {QStringLiteral("sycl"), QStringLiteral("SYCL")},
    };
    if (wellKnown.contains(backend))
        return wellKnown.value(backend);

    const int hyphen = backend.indexOf(QLatin1Char('-'));
    if (hyphen > 0) {
        const QString base = backend.left(hyphen);
        const QString suffix = backend.mid(hyphen + 1);
        QString name = backendDisplayName(base);
        return QStringLiteral("%1 %2").arg(name, suffix);
    }
    return backend;
}

QString RuntimeInstaller::releaseLabel(int index) const
{
    if (index < 0 || index >= m_releases.size())
        return QString();
    return QStringLiteral("%1 · %2").arg(releaseTag(index), releaseDate(index));
}

QString RuntimeInstaller::releaseTag(int index) const
{
    if (index < 0 || index >= m_releases.size())
        return QString();
    return taggedName(m_releases.at(index));
}

QString RuntimeInstaller::releaseDate(int index) const
{
    if (index < 0 || index >= m_releases.size())
        return QString();
    const QDate date = QDate::fromString(m_releases.at(index).publishedAt.left(10), Qt::ISODate);
    return date.isValid() ? date.toString(Qt::ISODate) : QStringLiteral("—");
}

bool RuntimeInstaller::releaseIsActive(int index) const
{
    if (index < 0 || index >= m_releases.size())
        return false;
    const int build = m_releases.at(index).build;
    if (build <= 0)
        return false;
    return build == buildNumber(installedBuild());
}

QString RuntimeInstaller::candidateTag() const
{
    if (m_releases.isEmpty())
        return QString();
    return releaseTag(m_selectedRelease);
}

QString RuntimeInstaller::candidateDate() const
{
    if (m_releases.isEmpty())
        return QString();
    return releaseDate(m_selectedRelease);
}

QString RuntimeInstaller::candidateSize() const
{
    if (m_releases.isEmpty() || m_selectedRelease < 0 || m_selectedRelease >= m_releases.size())
        return QString();
    const ReleaseAsset asset = pickAsset(m_releases.at(m_selectedRelease), m_backend, /*cudart=*/false);
    if (asset.size <= 0)
        return QString();
    return formatBytes(asset.size);
}

const InstalledBuildInfo *RuntimeInstaller::installedCandidate() const
{
    if (m_releases.isEmpty() || m_selectedRelease < 0 || m_selectedRelease >= m_releases.size())
        return nullptr;
    const ReleaseInfo &release = m_releases.at(m_selectedRelease);
    if (release.build <= 0)
        return nullptr;
    const QString build = QStringLiteral("b%1").arg(release.build);
    for (const InstalledBuildInfo &info : m_installedBuilds->builds()) {
        if (info.build != build || info.serverPath.isEmpty())
            continue;
        if (!backendMatches(info.backend, m_backend))
            continue;
        return &info;
    }
    return nullptr;
}

bool RuntimeInstaller::candidateInstalled() const
{
    return installedCandidate() != nullptr;
}

bool RuntimeInstaller::activateCandidate()
{
    if (m_busy)
        return false;
    const InstalledBuildInfo *candidate = installedCandidate();
    if (!candidate)
        return false;
    const InstalledBuildInfo build = *candidate;
    return activateInstalledBuild(build);
}

void RuntimeInstaller::checkForUpdates()
{
    if (m_state == State::Fetching)
        return;
    startCatalogFetch();
}

void RuntimeInstaller::startCatalogFetch()
{
    setState(State::Fetching);
    setBusy(true);
    setStatusMessage(tr("Checking for updates…"));

    const QString cacheDir = m_installState.paths().cacheDir();
    const QString stableDir = QDir(cacheDir).filePath(QStringLiteral("releases-stable"));
    const QString betaDir = QDir(cacheDir).filePath(QStringLiteral("releases-beta"));
    const QString base = m_catalogApiBase.isEmpty() ? QString::fromUtf8(ReleaseCatalog::kStableApiBase) : m_catalogApiBase;
    const bool beta = m_channel == QLatin1String("beta");

    auto future = QtConcurrent::run([beta, stableDir, betaDir, base]() -> CatalogLoad {
        QNetworkAccessManager nam;
        if (!beta) {
            const ReleaseFetch fetch = ReleaseCatalog::fetchStableReleaseLocal(&nam, stableDir, betaDir, ReleaseCatalog::kRequestTimeoutMs, base);
            return {fetch.releases, fetch.error, fetch.note.isEmpty() ? QString() : tr("Stable release unavailable (%1) — showing beta builds").arg(fetch.note)};
        }
        QString error;
        const QList<ReleaseInfo> rels = ReleaseCatalog::fetchReleasesLocal(&nam, betaDir, error, ReleaseCatalog::kRequestTimeoutMs, base + QStringLiteral("/releases?per_page=10"));
        return {rels, error, QString()};
    });

    future.then(this, [this](const CatalogLoad &load) { onCatalogLoaded(load.releases, load.error, load.note); });
}

void RuntimeInstaller::onCatalogLoaded(const QList<ReleaseInfo> &releases, const QString &error, const QString &note)
{
    setBusy(false);
    if (releases.isEmpty()) {
        m_installAfterCatalog = false;
        m_catalogWarning.clear();
        setStatusMessage(error.isEmpty() ? tr("No releases available") : error);
        setState(State::Error);
        emit catalogChanged();
        return;
    }

    m_releases = releases;
    const bool fromCache = !error.isEmpty();
    const bool betaFallback = !note.isEmpty();
    // The offline cache comes with the error that made the app fall back to it:
    // the check did not succeed, so the catalog keeps the age it had and the
    // status names the failure instead of claiming a fresh look (ADR 202). The
    // beta fallback list, though, is fresh beta data.
    if (!fromCache || betaFallback)
        m_lastCatalogAt = QDateTime::currentDateTimeUtc();
    if (m_selectedRelease >= m_releases.size())
        setSelectedRelease(0);

    recomputeHasUpdate();
    emit candidateChanged();

    QDate newest = QDate::fromString(m_releases.first().publishedAt.left(10), Qt::ISODate);
    const QString when = newest.isValid() ? newest.toString(Qt::ISODate) : QStringLiteral("—");
    if (betaFallback) {
        setStatusMessage(note);
        m_catalogWarning = note;
    } else if (fromCache) {
        const QString cached = tr("Showing the cached release list — the check failed: %1").arg(error);
        setStatusMessage(cached);
        m_catalogWarning = cached;
    } else {
        setStatusMessage(tr("Latest release: %1 (%2)").arg(taggedName(m_releases.first()), when));
        m_catalogWarning.clear();
    }
    setState(State::Ready);
    emit catalogChanged();

    if (m_installAfterCatalog) {
        m_installAfterCatalog = false;
        startDownloadAndInstall();
    }
}

QString RuntimeInstaller::checkedAtLabel() const
{
    if (!m_lastCatalogAt.isValid())
        return QString();
    return m_lastCatalogAt.time().toString(QStringLiteral("HH:mm"));
}

QString RuntimeInstaller::catalogWarning() const
{
    return m_catalogWarning;
}

void RuntimeInstaller::openReleasePage()
{
    if (m_releases.isEmpty())
        return;
    const QString tag = m_releases.at(qBound(0, m_selectedRelease, m_releases.size() - 1)).tagName;
    const QUrl url(QStringLiteral("https://github.com/ggml-org/llama.cpp/releases/tag/%1").arg(tag));
    QDesktopServices::openUrl(url);
}

void RuntimeInstaller::startDownloadAndInstall()
{
    if (m_state == State::Fetching || m_state == State::Downloading)
        return;
    if (m_releases.isEmpty()) {
        // Nobody fetched the release list yet — the wizard's first run does
        // not: fetch it and install as soon as it is here.
        m_installAfterCatalog = true;
        startCatalogFetch();
        return;
    }
    if (m_selectedRelease < 0 || m_selectedRelease >= m_releases.size()) {
        setStatusMessage(tr("No release selected — check for updates first"));
        setState(State::Error);
        return;
    }
    // The candidate build is on disk already: the build is switched to, not
    // fetched a second time (ADR 317).
    if (activateCandidate())
        return;
    QString lockError;
    if (!acquireInstallLock(lockError)) {
        setStatusMessage(lockError);
        setState(State::Error);
        return;
    }
    m_pendingBackend = m_backend;
    beginInstall(m_pendingBackend);
}

void RuntimeInstaller::beginInstall(const QString &backend)
{
    const ReleaseInfo &release = m_releases.at(m_selectedRelease);
    m_pendingMain = pickAsset(release, backend, /*cudart=*/false);
    if (m_pendingMain.downloadUrl.isEmpty()) {
        setStatusMessage(tr("No %1 build available for this platform in release %2").arg(backendDisplayName(backend), release.tagName));
        setState(State::Error);
        releaseInstallLock();
        return;
    }

    m_pendingCudart = pickAsset(release, backend, /*cudart=*/true);
    m_pendingHasCudart = !m_pendingCudart.downloadUrl.isEmpty();

    beginDownloads();
}

void RuntimeInstaller::beginDownloads()
{
    m_installState.ensureDirectories();
    setState(State::Downloading);
    setBusy(true);
    setProgress(0.0);
    setStatusMessage(tr("Downloading %1 …").arg(m_pendingMain.fileName));

    const QString targetDir = m_installState.paths().runtimeDir();
    QDir().mkpath(targetDir);

    m_group->begin();

    m_group->enqueue(DownloadTask::Request{QUrl(m_pendingMain.downloadUrl), targetDir, m_pendingMain.fileName, m_pendingMain.sha256, QString(), true});

    if (m_state != State::Downloading)
        return;

    if (m_pendingHasCudart) {
        m_group->enqueue(DownloadTask::Request{QUrl(m_pendingCudart.downloadUrl), targetDir, m_pendingCudart.fileName, m_pendingCudart.sha256, QString(), true});
    }
}

void RuntimeInstaller::maybeFinishDownloads()
{
    if (m_state != State::Downloading)
        return;
    if (m_group->failed()) {
        setBusy(false);
        // The task's own error names the real cause (checksum, no digest, HTTP
        // status); the generic connection hint only covers a missing one.
        const QString detail = m_group->firstError();
        setStatusMessage(detail.isEmpty() ? tr("Download failed — check your connection and try again") : tr("Download failed: %1").arg(detail));
        setState(State::Error);
        releaseInstallLock();
        return;
    }
    setState(State::Installing);
    runInstallAsync();
}

bool RuntimeInstaller::acquireInstallLock(QString &error)
{
    if (m_installLock.held())
        return true;
    if (!m_installLock.tryLock()) {
        error = tr("Another LLocr instance is installing a runtime right now; "
                   "try again in a moment.");
        return false;
    }
    return true;
}

void RuntimeInstaller::releaseInstallLock()
{
    m_installLock.release();
}

void RuntimeInstaller::runInstallAsync()
{
    setStatusMessage(tr("Installing %1 …").arg(backendDisplayName(m_pendingBackend)));

    const RuntimePaths currentPaths = m_installState.paths();
    const QString mainZip = QDir(currentPaths.runtimeDir()).filePath(m_pendingMain.fileName);
    const QString cudartZip = m_pendingHasCudart ? QDir(currentPaths.runtimeDir()).filePath(m_pendingCudart.fileName) : QString();
    const ReleaseAsset mainAsset = m_pendingMain;
    const bool hasCudart = m_pendingHasCudart;
    const RuntimePaths paths = currentPaths;

    QFuture<std::pair<InstallOutput, QString>> future = QtConcurrent::run([mainZip, mainAsset, paths]() -> std::pair<InstallOutput, QString> {
        InstallOutput out = InstallTransaction::start(mainZip, mainAsset, paths);
        return {out, out.warning};
    });

    future
        .then([cudartZip, hasCudart](std::pair<InstallOutput, QString> res) -> std::pair<InstallOutput, QString> {
            InstallOutput out = res.first;
            QString warning = res.second;
            if (out.ok && hasCudart) {
                const QString serverDir = QFileInfo(out.serverPath).absolutePath();
                const ExtractResult ex = ArchiveExtractor::extractZip(cudartZip, serverDir);
                if (!ex.error.isEmpty())
                    warning = tr("CUDA runtime extraction warning: %1").arg(ex.error);
            }
            return {out, warning};
        })
        .then(this, [this, currentPaths](const std::pair<InstallOutput, QString> &res) {
            const InstallOutput out = res.first;
            const QString warning = res.second;
            m_downloadedMainZip = QDir(currentPaths.runtimeDir()).filePath(m_pendingMain.fileName);
            m_downloadedCudartZip = m_pendingHasCudart ? QDir(currentPaths.runtimeDir()).filePath(m_pendingCudart.fileName) : QString();
            onInstallFinished(out, warning);
        });
}

void RuntimeInstaller::onInstallFinished(const InstallOutput &out, const QString &warning)
{
    QFile::remove(m_downloadedMainZip);
    if (!m_downloadedCudartZip.isEmpty())
        QFile::remove(m_downloadedCudartZip);

    setBusy(false);
    if (!out.ok) {
        setStatusMessage(out.error);
        setState(State::Error);
        releaseInstallLock();
        return;
    }

    if (warning.isEmpty())
        setStatusMessage(tr("Installed %1 (%2)").arg(out.build, backendDisplayName(m_pendingBackend)));
    else
        setStatusMessage(tr("Installed %1 (%2). %3").arg(out.build, backendDisplayName(m_pendingBackend), warning));

    m_settings.setServerPath(out.serverPath);
    m_settings.setInstalledBuild(out.build);
    const ReleaseInfo &rel = m_releases.at(m_selectedRelease);
    m_settings.setInstalledTag(rel.prerelease ? QString() : rel.tagName);
    m_settings.setRuntimeBackend(m_pendingBackend);
    m_settings.forceSave();
    emit installedChanged();

    rescanInstalledBuilds();
    recomputeHasUpdate();

    setState(State::Installed);
    releaseInstallLock();
}

void RuntimeInstaller::recomputeHasUpdate()
{
    const int latestBuild = m_releases.isEmpty() ? -1 : m_releases.first().build;
    const int cur = buildNumber(installedBuild());
    const bool upd = cur >= 0 && latestBuild > cur;
    if (m_hasUpdate != upd) {
        m_hasUpdate = upd;
        emit hasUpdateChanged();
    }
}

void RuntimeInstaller::cancelInstall()
{
    if (m_state != State::Downloading)
        return;
    m_downloads->cancelAll(true);
    setBusy(false);
    setStatusMessage(tr("Installation cancelled"));
    setState(State::Error);
    releaseInstallLock();
}

QString RuntimeInstaller::cleanupUnusedBuilds()
{
    if (installedBuild().isEmpty()) {
        const QString msg = tr("No active runtime build — cleanup would remove "
                               "every installed build. Install or activate a "
                               "build first.");
        setStatusMessage(msg);
        return msg;
    }

    QString lockError;
    if (!acquireInstallLock(lockError)) {
        setStatusMessage(lockError);
        return lockError;
    }

    QString keepTag;
    const QString serverPath = m_settings.serverPath();
    for (const InstalledBuildInfo &build : m_installedBuilds->builds()) {
        if (build.serverPath == serverPath) {
            keepTag = build.tag;
            break;
        }
    }
    if (keepTag.isEmpty() && !installedBuild().isEmpty()) {
        const QString prefix = QStringLiteral("llama.cpp-%1-%2").arg(installedBuild(), installedBackend());
        const QStringList dirs = QDir(m_installState.paths().runtimeDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : dirs) {
            if (name.startsWith(prefix)) {
                keepTag = name;
                break;
            }
        }
    }

    const QString summary = InstallTransaction::cleanupUnusedBuilds(m_installState.paths(), keepTag);
    setStatusMessage(summary);
    rescanInstalledBuilds();
    releaseInstallLock();
    return summary;
}

void RuntimeInstaller::rescanInstalledBuilds()
{
    m_installedBuilds->setBuilds(InstallTransaction::scanInstalledBuilds(m_installState.paths()));
    emit installedBuildsChanged();
    // What is on disk decides whether the candidate still needs a download.
    emit candidateChanged();
}

void RuntimeInstaller::reconcileInstalledBackend()
{
    const QString serverPath = m_settings.serverPath();
    if (serverPath.isEmpty())
        return;
    for (const InstalledBuildInfo &build : m_installedBuilds->builds()) {
        if (build.serverPath != serverPath)
            continue;
        if (!build.backend.isEmpty() && m_settings.runtimeBackend() != build.backend) {
            m_settings.setRuntimeBackend(build.backend);
            m_settings.forceSave();
        }
        return;
    }
}

QObject *RuntimeInstaller::installedBuilds() const
{
    return m_installedBuilds;
}

QString RuntimeInstaller::openBuildFolder(int index)
{
    const QList<InstalledBuildInfo> &builds = m_installedBuilds->builds();
    if (index < 0 || index >= builds.size())
        return tr("No such build");
    const InstalledBuildInfo &b = builds.at(index);
    if (b.serverPath.isEmpty())
        return tr("The build directory contains no llama-server binary");
    const QString dir = QFileInfo(b.serverPath).absolutePath();
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir)))
        return tr("Unable to open %1").arg(dir);
    return QString();
}

QString RuntimeInstaller::activateBuild(int index)
{
    if (m_busy)
        return tr("An install is in progress");
    if (index < 0 || index >= m_installedBuilds->rowCount())
        return tr("No such build");
    const InstalledBuildInfo &b = m_installedBuilds->builds().at(index);
    if (b.serverPath.isEmpty())
        return tr("The build directory contains no llama-server binary");

    activateInstalledBuild(b);
    return QString();
}

bool RuntimeInstaller::activateInstalledBuild(const InstalledBuildInfo &build)
{
    m_settings.setServerPath(build.serverPath);
    if (!build.build.isEmpty())
        m_settings.setInstalledBuild(build.build);
    m_settings.setInstalledTag(QString());
    if (!build.backend.isEmpty())
        m_settings.setRuntimeBackend(build.backend);
    m_settings.forceSave();

    const QString label = build.build.isEmpty() ? build.tag : build.build;
    const QString suffix = build.backend.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(backendDisplayName(build.backend));
    setStatusMessage(tr("Activated %1%2").arg(label, suffix));
    emit installedChanged();
    rescanInstalledBuilds();
    recomputeHasUpdate();
    // The switch is an installation from the wizard's point of view: a build
    // is installed and active.
    setState(State::Installed);
    return true;
}

QString RuntimeInstaller::removeBuild(const QString &tag)
{
    const QList<InstalledBuildInfo> &builds = m_installedBuilds->builds();
    for (int i = 0; i < builds.size(); ++i) {
        if (builds.at(i).tag == tag)
            return removeBuild(i);
    }
    return tr("No such build");
}

QString RuntimeInstaller::removeBuild(int index)
{
    if (m_busy) {
        const QString msg = tr("An install is in progress");
        setStatusMessage(msg);
        return msg;
    }
    if (index < 0 || index >= m_installedBuilds->rowCount())
        return tr("No such build");
    const InstalledBuildInfo &b = m_installedBuilds->builds().at(index);
    if (!b.serverPath.isEmpty() && RuntimePaths::normalized(b.serverPath) == RuntimePaths::normalized(m_settings.serverPath())) {
        const QString msg = tr("This build is active — activate another build first");
        setStatusMessage(msg);
        return msg;
    }

    QString lockError;
    if (!acquireInstallLock(lockError)) {
        setStatusMessage(lockError);
        return lockError;
    }

    if (!QDir(m_installState.paths().installDir(b.tag)).removeRecursively()) {
        const QString msg = tr("Unable to remove %1").arg(b.tag);
        setStatusMessage(msg);
        releaseInstallLock();
        return msg;
    }

    setStatusMessage(tr("Removed %1").arg(b.tag));
    rescanInstalledBuilds();
    recomputeHasUpdate();
    releaseInstallLock();
    return QString();
}

ReleaseAsset RuntimeInstaller::pickAsset(const ReleaseInfo &release, const QString &backend, bool wantCudart) const
{
    const PlatformInfo info = ReleaseCatalog::detectPlatform();
    for (const ReleaseAsset &a : release.assets) {
        if (a.cudart != wantCudart)
            continue;
        if (wantCudart) {
            if (a.os == info.osTag)
                return a;
            continue;
        }
        if (a.os == info.osTag && a.arch == info.arch && backendMatches(a.backend, backend))
            return a;
    }
    return ReleaseAsset();
}

}  // namespace llocr