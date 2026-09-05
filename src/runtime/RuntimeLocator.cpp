#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

#include "runtime/RuntimeLocator.h"
#include "runtime/ServerCapabilities.h"

namespace llocr {

QString RuntimeLocator::runProbe(const QString &binaryPath, QStringList args,
                                 int timeoutMs, QString &error)
{
    QProcess proc;
    proc.setProgram(binaryPath);
    proc.setArguments(args);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QIODevice::ReadOnly);
    // Spawning a freshly-launched binary is fast in practice (fork/exec, page-in
    // of the loader); a 2.5 s bound on the startup wait catches a hung exec
    // without holding the calling (UI) thread for the full timeout (§H.7 task
    // 1 — avoids stalls on slow/large disks). The remaining budget goes to the
    // actual run.
    const int spawnBudget = qMin(2500, timeoutMs);
    if (!proc.waitForStarted(spawnBudget)) {
        error = QObject::tr("Failed to start the binary: %1").arg(proc.errorString());
        return QString();
    }
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(2000);
        error = QObject::tr("Probe timed out after %1 ms").arg(timeoutMs);
        return QString();
    }
    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    error = QString();
    return out;
}

ProbeResult RuntimeLocator::probe(const QString &binaryPath, int timeoutMs)
{
    // Always a fresh probe: the user-facing "Check" button must reflect the
    // current on-disk state even when only permissions changed (mtime-stable).
    return probeImpl(binaryPath, timeoutMs);
}

ProbeResult RuntimeLocator::probeCached(const QString &binaryPath, int timeoutMs)
{
    // For the manage startServer() path, an unchanged binary (same path, mtime,
    // size) is not re-probed — no subprocess spawn and no main-thread stall on
    // repeated recognition starts. A changed file misses and re-probes.
    ProbeResult cached;
    if (probeFromCache(binaryPath, cached))
        return cached;
    const ProbeResult r = probeImpl(binaryPath, timeoutMs);
    cacheProbe(binaryPath, r);
    return r;
}

ProbeResult RuntimeLocator::probeCached(const QString &binaryPath, const QString &cacheDir,
                                        int timeoutMs)
{
    // Same as above, plus a persistent JSON cache (ServerCapabilities::
    // cacheFileName) so an unchanged binary is not re-spawned across app runs:
    // a fresh process misses the in-memory slot but serves from disk. Only
    // successful probes are written, so a transient failure re-probes next time.
    ProbeResult cached;
    if (probeFromCache(binaryPath, cached))
        return cached;
    if (probeFromDiskCache(binaryPath, cacheDir, cached)) {
        cacheProbe(binaryPath, cached);  // refresh the in-memory slot too
        return cached;
    }
    const ProbeResult r = probeImpl(binaryPath, timeoutMs);
    cacheProbe(binaryPath, r);
    writeDiskCache(binaryPath, cacheDir, r);
    return r;
}

ProbeResult RuntimeLocator::probeImpl(const QString &binaryPath, int timeoutMs)
{
    ProbeResult r;
    if (binaryPath.trimmed().isEmpty()) {
        r.error = QObject::tr("No server binary selected");
        return r;
    }
    const QFileInfo fi(binaryPath);
    if (!fi.exists() || !fi.isFile()) {
        r.error = QObject::tr("File not found: %1").arg(binaryPath);
        return r;
    }

    // One wall-clock budget shared by both spawns: --version gets the full
    // amount, --help the remainder — the whole probe can never take longer than
    // timeoutMs (plus the bounded kill grace on a genuinely hung spawn), instead
    // of 2 × timeoutMs when each ran with a fresh budget. If --version consumed
    // everything, --help is skipped and the flags fall back to the build
    // allowlist (same as a build that prints no help). §H.7 follow-up.
    QElapsedTimer budget;
    budget.start();
    QString errVersion, errHelp;
    const QString versionText =
        runProbe(binaryPath, {QStringLiteral("--version")}, timeoutMs, errVersion);
    const int remaining = qMax(0, timeoutMs - int(budget.elapsed()));
    QString helpText;
    if (remaining > 0) {
        helpText = runProbe(binaryPath, {QStringLiteral("--help")}, remaining, errHelp);
    } else {
        errHelp = QObject::tr("skipped (probe budget exhausted)");
    }

    r.version = versionText;
    r.capabilities = ServerCapabilities::detect(versionText, helpText);
    r.ok = r.capabilities.ok;

    if (r.ok) {
        if (r.capabilities.belowMinimum) {
            r.error = QObject::tr("Requires llama.cpp %1 or newer").arg(
                QLatin1String(ServerCapabilities::kMinimumSupportedBuild));
        } else {
            r.error.clear();
        }
    } else {
        const QString versionDetail =
            versionText.isEmpty() ? (errVersion.isEmpty() ? QObject::tr("no output") : errVersion)
                                  : QObject::tr("version ok");
        const QString helpDetail =
            helpText.isEmpty() ? (errHelp.isEmpty() ? QObject::tr("no output") : errHelp)
                               : QObject::tr("help ok");
        r.error = QObject::tr("The binary did not answer (%1; %2)")
                      .arg(versionDetail, helpDetail);
    }
    return r;
}

QString RuntimeLocator::probeSummary(const ProbeResult &r)
{
    if (r.capabilities.belowMinimum) {
        return QObject::tr("Build %1 is below the minimum (%2) — update it")
            .arg(r.capabilities.build, QLatin1String(ServerCapabilities::kMinimumSupportedBuild));
    }
    if (!r.ok)
        return r.error;
    const QString build = r.capabilities.build;
    return build.isEmpty()
               ? QObject::tr("Valid llama-server (build unknown)")
               : QObject::tr("Valid llama-server %1").arg(build);
}

QString RuntimeLocator::autoDiscover(int timeoutMs)
{
    const QString pathCandidate =
        QStandardPaths::findExecutable(QStringLiteral("llama-server"));
    if (!pathCandidate.isEmpty() && probe(pathCandidate, timeoutMs).ok)
        return pathCandidate;

    QStringList roots;
#ifdef Q_OS_WIN
    const QString localApp = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    roots << QDir(localApp).filePath(QStringLiteral("llama.cpp"));
#else
    roots << QStringLiteral("/usr/local/bin") << QStringLiteral("/opt/homebrew/bin")
          << QStringLiteral("/opt/local/bin");
#endif

    for (const QString &root : roots) {
        const QString candidate =
            QDir(root).filePath(QStringLiteral("llama-server"));
        if (QFileInfo::exists(candidate) && probe(candidate, timeoutMs).ok)
            return candidate;
    }
    return QString();
}

QString RuntimeLocator::ensureExecutable(const QString &binaryPath, bool pathManaged,
                                         bool &needsConfirmation)
{
    needsConfirmation = false;
    const QFileInfo fi(binaryPath);
    if (!fi.exists())
        return QObject::tr("File not found: %1").arg(binaryPath);
#ifdef Q_OS_UNIX
    if (fi.isExecutable())
        return QString();  // already runnable
    if (!pathManaged) {
        // A manually chosen file outside our runtime root: confirm before
        // changing permissions (the UI shows the prompt). §5 task 1.
        needsConfirmation = true;
        return QObject::tr("The selected file is not executable");
    }
    // Inside the managed runtime root only: restore the executable bit.
    QFile f(binaryPath);
    if (!f.setPermissions(f.permissions() | QFileDevice::ExeUser | QFileDevice::ExeGroup
                                        | QFileDevice::ExeOther))
        return QObject::tr("Unable to make the binary executable");
#endif
    return QString();
}

// --- §H.7 probe cache ------------------------------------------------

namespace {
// A single managed server binary is used at a time, so one cached probe (with
// an exact `path + mtime + size` match) is enough to skip the redundant
// --version/--help spawn on consecutive starts. Invalidation: an on-disk change
// (mtime/size) or a different path naturally misses and re-probes (review 3.6:
// the old 4-slot LRU re-ordered a QVector for what is a single binary).
// Main-thread only: probeCached() is called from the QML startServer()/
// launchCommandPreview() paths; the install flow uses probe() (always fresh)
// and does not touch this.
struct ProbeCacheSlot {
    RuntimeLocator::ProbeKey key;
    ProbeResult result;
    bool valid = false;
};
ProbeCacheSlot s_probeCache;

}  // namespace

bool RuntimeLocator::probeFromCache(const QString &binaryPath, ProbeResult &out)
{
    const QFileInfo fi(binaryPath);
    if (!fi.exists() || !fi.isFile())
        return false;
    const ProbeKey key{fi.absoluteFilePath(), fi.lastModified().toMSecsSinceEpoch(),
                       fi.size()};
    if (s_probeCache.valid && s_probeCache.key == key) {
        out = s_probeCache.result;
        return true;
    }
    return false;
}

void RuntimeLocator::cacheProbe(const QString &binaryPath, const ProbeResult &result)
{
    const QFileInfo fi(binaryPath);
    if (!fi.exists() || !fi.isFile())
        return;
    s_probeCache.key = ProbeKey{fi.absoluteFilePath(),
                                fi.lastModified().toMSecsSinceEpoch(), fi.size()};
    s_probeCache.result = result;
    s_probeCache.valid = true;
}

// ---------------------------------------------------------------------------
// §5.3 step 4 / ADR 41: persistent capabilities cache (capabilities-<sha1>.json)
// ---------------------------------------------------------------------------

bool RuntimeLocator::probeFromDiskCache(const QString &binaryPath, const QString &cacheDir,
                                        ProbeResult &out)
{
    if (cacheDir.isEmpty())
        return false;
    QFile f(ServerCapabilities::cacheFileName(cacheDir, binaryPath));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return false;
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("schemaVersion")).toInt() != 1)
        return false;  // future schema versions are re-probed, not misread
    const ServerCapabilities caps = ServerCapabilities::fromJson(o);
    out.capabilities = caps;
    out.version = caps.versionText;
    out.ok = caps.ok;
    // Mirror probeImpl()'s diagnostics for a below-minimum cached build.
    if (caps.ok && caps.belowMinimum) {
        out.error = QObject::tr("Requires llama.cpp %1 or newer").arg(
            QLatin1String(ServerCapabilities::kMinimumSupportedBuild));
    } else {
        out.error.clear();
    }
    return true;
}

void RuntimeLocator::writeDiskCache(const QString &binaryPath, const QString &cacheDir,
                                    const ProbeResult &result)
{
    // Never cache failed/aborted probes: a transient error (busy system,
    // timeout) must not mask a later-fixed binary until its mtime/size changes.
    if (cacheDir.isEmpty() || !result.ok)
        return;
    if (!QDir().mkpath(cacheDir))
        return;
    QSaveFile f(ServerCapabilities::cacheFileName(cacheDir, binaryPath));
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QJsonDocument(result.capabilities.toJson()).toJson(QJsonDocument::Compact));
    f.commit();
}

}  // namespace llocr