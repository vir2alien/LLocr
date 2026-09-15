#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QTime>
#include <QUrl>

#include <QtConcurrent>

#include "app/SettingsStore.h"
#include "runtime/ArchiveExtractor.h"
#include "runtime/DownloadGroup.h"
#include "runtime/DownloadManager.h"
#include "runtime/DownloadTask.h"
#include "runtime/InstallTransaction.h"
#include "runtime/ReleaseCatalog.h"
#include "runtime/RuntimeInstaller.h"

namespace llocr {

namespace {

QStringList backendsForPlatform(const PlatformInfo &info)
{
    switch (info.os) {
    case PlatformOs::Windows:
        return {QStringLiteral("cpu"), QStringLiteral("cuda"), QStringLiteral("vulkan")};
    case PlatformOs::macOS:
        return {QStringLiteral("metal"), QStringLiteral("cpu")};
    default:
        return {QStringLiteral("cpu"), QStringLiteral("vulkan"), QStringLiteral("cuda")};
    }
}

QString osLabel(const PlatformInfo &info)
{
    switch (info.os) {
    case PlatformOs::Windows:
        return RuntimeInstaller::tr("Windows");
    case PlatformOs::macOS:
        return RuntimeInstaller::tr("macOS");
    default:
        return RuntimeInstaller::tr("Linux");
    }
}

// True when the asset's backend token belongs to the requested backend
// (exact or prefixed: "cuda" also matches "cuda-cu12" release assets). An
// empty token is a universal build (e.g. `...-bin-macos-arm64`) that fits any
// backend request.
bool backendMatches(const QString &assetBackend, const QString &requested)
{
    if (assetBackend.isEmpty())
        return true;
    if (assetBackend == requested)
        return true;
    return assetBackend.startsWith(requested + QLatin1Char('-'));
}

}  // namespace

RuntimeInstaller::RuntimeInstaller(SettingsStore &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_paths(settings.runtimeRootDir(), settings.runtimeModelsDir())
    , m_installLock(m_paths.installLockPath())
    , m_downloads(new DownloadManager(this))
    , m_group(new DownloadGroup(m_downloads, this))
{
    const PlatformInfo info = ReleaseCatalog::detectPlatform();
    m_platformLabel = QStringLiteral("%1 %2").arg(osLabel(info), info.arch);
    m_recommendedBackend = info.backend;
    m_recommendationReason = info.backendReason;
    m_availableBackends = backendsForPlatform(info);
    if (m_availableBackends.contains(m_recommendedBackend))
        m_backend = m_recommendedBackend;
    else if (!m_availableBackends.isEmpty())
        m_backend = m_availableBackends.first();

    connect(m_group, &DownloadGroup::progressChanged, this, [this]() {
        setProgress(m_group->progress());
        if (m_state == State::Downloading) {
            setStatusMessage(tr("Downloading %1 …")
                                 .arg(m_pendingMain.fileName.isEmpty()
                                          ? tr("runtime")
                                          : m_pendingMain.fileName));
        }
    });
    connect(m_group, &DownloadGroup::allFinished, this, [this](bool) {
        maybeFinishDownloads();
    });

    // §H.6: dedicated install lock, independent of the app instance lock, so
    // a second GUI instance can use External while installs stay exclusive.

    m_paths.ensureDirectories();
    rescanInstalledBuilds();
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

// ---------------------------------------------------------------------------
// State / property helpers
// ---------------------------------------------------------------------------

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

void RuntimeInstaller::setBackend(const QString &backend)
{
    if (m_backend == backend)
        return;
    m_backend = backend;
    emit backendChanged();
}

void RuntimeInstaller::setSelectedRelease(int index)
{
    if (m_selectedRelease == index)
        return;
    m_selectedRelease = index;
    emit selectedReleaseChanged();
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
        setStatusMessage(tr("Downloading %1 …")
                             .arg(m_pendingMain.fileName.isEmpty()
                                      ? tr("runtime")
                                      : m_pendingMain.fileName));
        break;
    case State::Installing:
        setStatusMessage(tr("Installing %1 …").arg(backendDisplayName(m_pendingBackend)));
        break;
    default:
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

    // e.g. "cuda-cu12" → "CUDA 12".
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
    const ReleaseInfo &r = m_releases.at(index);
    QDate date = QDate::fromString(r.publishedAt.left(10), Qt::ISODate);
    const QString when = date.isValid() ? date.toString(Qt::ISODate) : QStringLiteral("—");
    return QStringLiteral("%1 · %2").arg(r.tagName, when);
}

int RuntimeInstaller::releaseBuild(int index) const
{
    if (index < 0 || index >= m_releases.size())
        return -1;
    return m_releases.at(index).build;
}

// ---------------------------------------------------------------------------
// Catalog fetch
// ---------------------------------------------------------------------------

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

    // The fetch + cache write is synchronous inside ReleaseCatalog; run it on the
    // pool and marshal the (copyable) result back to the main thread via .then().
    const QString cacheDir = m_paths.cacheDir();
    auto future = QtConcurrent::run([cacheDir]() -> QPair<QList<ReleaseInfo>, QString> {
        QNetworkAccessManager nam;
        QString error;
        QList<ReleaseInfo> rels = ReleaseCatalog::fetchReleasesLocal(&nam, cacheDir, error);
        return {rels, error};
    });

    future.then(this, [this](const QPair<QList<ReleaseInfo>, QString> &res) {
        onCatalogLoaded(res.first, res.second);
    });
}

void RuntimeInstaller::onCatalogLoaded(const QList<ReleaseInfo> &releases,
                                       const QString &error)
{
    setBusy(false);
    if (releases.isEmpty()) {
        setStatusMessage(error.isEmpty() ? tr("No releases available") : error);
        setState(State::Error);
        return;
    }

    m_releases = releases;
    m_lastCatalogAt = QDateTime::currentDateTime();
    if (m_selectedRelease >= m_releases.size())
        setSelectedRelease(0);

    recomputeHasUpdate();

    QDate newest = QDate::fromString(m_releases.first().publishedAt.left(10), Qt::ISODate);
    setStatusMessage(tr("Latest release: %1 (%2)")
                         .arg(m_releases.first().tagName,
                              newest.isValid() ? newest.toString(Qt::ISODate)
                                               : QStringLiteral("—")));
    setState(State::Ready);
    emit catalogChanged();
}

QString RuntimeInstaller::updateBuild() const
{
    if (m_releases.isEmpty())
        return QString();
    return m_releases.first().tagName;
}

QString RuntimeInstaller::updateTimestampLabel() const
{
    if (!m_lastCatalogAt.isValid())
        return QString();
    return m_lastCatalogAt.time().toString(QStringLiteral("HH:mm"));
}

void RuntimeInstaller::openReleasePage()
{
    if (m_releases.isEmpty())
        return;
    const QString tag = m_releases.first().tagName;
    const QUrl url(QStringLiteral("https://github.com/ggml-org/llama.cpp/releases/tag/%1")
                       .arg(tag));
    QDesktopServices::openUrl(url);
}

void RuntimeInstaller::installUpdate()
{
    if (m_releases.isEmpty())
        return;
    setSelectedRelease(0);
    startDownloadAndInstall();
}

// ---------------------------------------------------------------------------
// Download + install pipeline
// ---------------------------------------------------------------------------

void RuntimeInstaller::startDownloadAndInstall()
{
    if (m_state == State::Fetching || m_state == State::Downloading)
        return;
    if (m_selectedRelease < 0 || m_selectedRelease >= m_releases.size()) {
        setStatusMessage(tr("No release selected — check for updates first"));
        setState(State::Error);
        return;
    }
    // §H.6: refuse when another instance is mid-install.
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
        setStatusMessage(tr("No %1 build available for this platform in release %2")
                             .arg(backendDisplayName(backend), release.tagName));
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
    m_paths.ensureDirectories();
    setState(State::Downloading);
    setBusy(true);
    setProgress(0.0);
    setStatusMessage(tr("Downloading %1 …").arg(m_pendingMain.fileName));

    // Archives land beside the runtime (ADR 40: .part lives in the target
    // volume) and are removed after the install commits.
    const QString targetDir = m_paths.runtimeDir();
    QDir().mkpath(targetDir);

    m_group->begin();

    m_group->enqueue(DownloadTask::Request{
        QUrl(m_pendingMain.downloadUrl), targetDir, m_pendingMain.fileName,
        m_pendingMain.sha256, QString()});

    // A synchronous main-download failure already ran maybeFinishDownloads()
    // (state → Error, lock released); do not enqueue the companion cudart
    // download with an inconsistent pending count.
    if (m_state != State::Downloading)
        return;

    if (m_pendingHasCudart) {
        m_group->enqueue(DownloadTask::Request{
            QUrl(m_pendingCudart.downloadUrl), targetDir, m_pendingCudart.fileName,
            m_pendingCudart.sha256, QString()});
    }
}

void RuntimeInstaller::maybeFinishDownloads()
{
    if (m_state != State::Downloading)
        return;
    if (m_group->failed()) {
        setBusy(false);
        setStatusMessage(tr("Download failed — check your connection and try again"));
        setState(State::Error);
        releaseInstallLock();
        return;
    }
    // All archives landed; verify then install on a worker thread.
    setState(State::Installing);
    runInstallAsync();
}

bool RuntimeInstaller::acquireInstallLock(QString &error)
{
    if (m_installLockHeld)
        return true;
    if (!m_installLock.tryLock(0)) {
        error = tr("Another LLocr instance is installing a runtime right now; "
                   "try again in a moment.");
        return false;
    }
    m_installLockHeld = true;
    return true;
}

void RuntimeInstaller::releaseInstallLock()
{
    if (!m_installLockHeld)
        return;
    m_installLock.unlock();
    m_installLockHeld = false;
}

void RuntimeInstaller::runInstallAsync()
{
    setStatusMessage(tr("Installing %1 …").arg(backendDisplayName(m_pendingBackend)));

    const QString mainZip = QDir(m_paths.runtimeDir()).filePath(m_pendingMain.fileName);
    const QString cudartZip = m_pendingHasCudart
                                  ? QDir(m_paths.runtimeDir()).filePath(m_pendingCudart.fileName)
                                  : QString();
    const ReleaseAsset mainAsset = m_pendingMain;
    const bool hasCudart = m_pendingHasCudart;
    const QString installDir = m_paths.runtimeDir();
    const RuntimePaths paths = m_paths;

    QFuture<QPair<InstallOutput, QString>> future =
        QtConcurrent::run([mainZip, mainAsset, paths]() -> QPair<InstallOutput, QString> {
            InstallOutput out = InstallTransaction::start(mainZip, mainAsset, paths);
            return {out, out.warning};
        });

    future.then([cudartZip, hasCudart](QPair<InstallOutput, QString> res)
                    -> QPair<InstallOutput, QString> {
        InstallOutput out = res.first;
        QString warning = res.second;
        // CUDA: unpack the cudart runtime into the same directory as the server
        // binary (additive; archive names differ, so no files from the main build
        // are overwritten). Extracting into runtimeDir() would leave
        // cudart64_*.dll away from llama-server and break CUDA loading.
        if (out.ok && hasCudart) {
            const QString serverDir = QFileInfo(out.serverPath).absolutePath();
            const ExtractResult ex = ArchiveExtractor::extractZip(cudartZip, serverDir);
            if (!ex.error.isEmpty())
                warning = tr("CUDA runtime extraction warning: %1").arg(ex.error);
        }
        return {out, warning};
    }).then(this, [this](const QPair<InstallOutput, QString> &res) {
        const InstallOutput out = res.first;
        const QString warning = res.second;
        m_downloadedMainZip = QDir(m_paths.runtimeDir()).filePath(m_pendingMain.fileName);
        m_downloadedCudartZip = m_pendingHasCudart
                                    ? QDir(m_paths.runtimeDir()).filePath(m_pendingCudart.fileName)
                                    : QString();
        onInstallFinished(out, warning);
    });
}

void RuntimeInstaller::onInstallFinished(const InstallOutput &out, const QString &warning)
{
    // Remove the downloaded archives first; the installed build is already live
    // by the time this runs.
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
        setStatusMessage(tr("Installed %1 (%2)")
                             .arg(out.build, backendDisplayName(m_pendingBackend)));
    else
        setStatusMessage(tr("Installed %1 (%2). %3")
                             .arg(out.build, backendDisplayName(m_pendingBackend), warning));

    // Settings are the very last step (§7.2 / Stage D task 5).
    m_settings.setServerPath(out.serverPath);
    m_settings.setServerPathIsManaged(true);
    m_settings.setInstalledBuild(out.build);
    m_settings.setRuntimeBackend(m_pendingBackend);
    m_settings.forceSave();
    emit installedChanged();

    // The new build directory just appeared on disk; refresh the scan so the
    // installed-builds list (and its active flag) is current.
    rescanInstalledBuilds();
    recomputeHasUpdate();

    setState(State::Installed);
    releaseInstallLock();
}

void RuntimeInstaller::recomputeHasUpdate()
{
    const int latestBuild = m_releases.isEmpty() ? -1 : m_releases.first().build;
    const int cur = installedBuild().startsWith(QLatin1Char('b'))
                        ? installedBuild().mid(1).toInt()
                        : -1;
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
    // Safety guard: with no active build (e.g. right after a settings reset)
    // the sweep would have no keep-tag and would delete EVERY downloaded
    // build. Refuse instead — activation is a one click away.
    if (installedBuild().isEmpty()) {
        const QString msg = tr("No active runtime build — cleanup would remove "
                               "every installed build. Install or activate a "
                               "build first.");
        setStatusMessage(msg);
        return msg;
    }

    // §H.6: sweeping the runtime dir must be exclusive vs a concurrent install.
    QString lockError;
    if (!acquireInstallLock(lockError)) {
        setStatusMessage(lockError);
        return lockError;
    }

    // Keep the build that matches the currently installed tag; sweep the rest.
    QString keepTag;
    if (!installedBuild().isEmpty()) {
        const QString prefix =
            QStringLiteral("llama.cpp-%1-%2").arg(installedBuild(), installedBackend());
        const QStringList dirs =
            QDir(m_paths.runtimeDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : dirs) {
            if (name.startsWith(prefix)) {
                keepTag = name;
                break;
            }
        }
    }

    const QString summary = InstallTransaction::cleanupUnusedBuilds(m_paths, keepTag);
    setStatusMessage(summary);
    releaseInstallLock();
    return summary;
}

// ---------------------------------------------------------------------------
// Installed-builds scan / activation
// ---------------------------------------------------------------------------

QString RuntimeInstaller::normalizedPath(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

void RuntimeInstaller::rescanInstalledBuilds()
{
    // Fresh paths from the current settings: the root-dir override can change
    // at runtime (settings reset), the scan must follow it rather than the
    // copy captured at construction.
    const RuntimePaths paths(m_settings.runtimeRootDir(),
                             m_settings.runtimeModelsDir());
    const QList<InstalledBuildInfo> builds =
        InstallTransaction::scanInstalledBuilds(paths);
    if (builds == m_installedBuilds)
        return;
    m_installedBuilds = builds;
    emit installedBuildsChanged();
}

QVariantMap RuntimeInstaller::installedBuildInfo(int index) const
{
    QVariantMap map;
    if (index < 0 || index >= m_installedBuilds.size())
        return map;
    const InstalledBuildInfo &b = m_installedBuilds.at(index);
    map.insert(QStringLiteral("tag"), b.tag);
    map.insert(QStringLiteral("build"), b.build);
    map.insert(QStringLiteral("backend"), b.backend);
    map.insert(QStringLiteral("backendDisplay"),
               b.backend.isEmpty() ? QString() : backendDisplayName(b.backend));
    map.insert(QStringLiteral("serverPath"), b.serverPath);
    map.insert(QStringLiteral("binaryFound"), !b.serverPath.isEmpty());
    // Active = this build's binary is the currently selected server path.
    map.insert(QStringLiteral("active"),
               !b.serverPath.isEmpty() && !m_settings.serverPath().isEmpty()
               && normalizedPath(b.serverPath)
                      == normalizedPath(m_settings.serverPath()));
    return map;
}

QString RuntimeInstaller::activateBuild(int index)
{
    if (m_busy)
        return tr("An install is in progress");
    if (index < 0 || index >= m_installedBuilds.size())
        return tr("No such build");
    const InstalledBuildInfo &b = m_installedBuilds.at(index);
    if (b.serverPath.isEmpty())
        return tr("The build directory contains no llama-server binary");

    // A settings-only commit (the directory itself is untouched): point the
    // managed runtime at this build's binary, same as the install commit does
    // (§7.2). The QML side stops a running server before calling this.
    m_settings.setServerPath(b.serverPath);
    m_settings.setServerPathIsManaged(true);
    if (!b.build.isEmpty())
        m_settings.setInstalledBuild(b.build);
    if (!b.backend.isEmpty())
        m_settings.setRuntimeBackend(b.backend);
    m_settings.forceSave();

    setStatusMessage(tr("Activated %1%2")
                         .arg(b.build.isEmpty() ? b.tag : b.build,
                              b.backend.isEmpty()
                                  ? QString()
                                  : QStringLiteral(" (%1)")
                                        .arg(backendDisplayName(b.backend))));
    emit installedChanged();
    rescanInstalledBuilds();   // refresh the active flags in the list
    recomputeHasUpdate();
    return QString();
}

ReleaseAsset RuntimeInstaller::pickAsset(const ReleaseInfo &release,
                                         const QString &backend,
                                         bool wantCudart) const
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