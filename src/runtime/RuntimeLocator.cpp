#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    if (!proc.waitForStarted(timeoutMs)) {
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

    // --version first (build number for the allowlist), then --help as a
    // refinement source and a fallback when --version is unknown (5.3 steps
    // 2-3). Each gets its own bounded probe so a hung binary can't stall UI.
    QString errVersion, errHelp;
    const QString versionText = runProbe(binaryPath, {QStringLiteral("--version")},
                                         timeoutMs, errVersion);
    const QString helpText =
        runProbe(binaryPath, {QStringLiteral("--help")}, timeoutMs, errHelp);

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
        // Neither --version nor --help produced usable output.
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
    // 1) System PATH via QStandardPaths::findExecutable.
    if (!QStandardPaths::findExecutable(QStringLiteral("llama-server")).isEmpty())
        return QStandardPaths::findExecutable(QStringLiteral("llama-server"));

    // 2) Common install roots (best-effort, order by likelihood).
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

}  // namespace llocr