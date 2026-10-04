#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QUuid>

#include <algorithm>

#include "runtime/ArchiveExtractor.h"
#include "runtime/FileDigest.h"
#include "runtime/InstallTransaction.h"

#include "runtime/ReleaseCatalog.h"
#include "runtime/RuntimeLocator.h"
#include "runtime/ServerCapabilities.h"
#include "runtime/StagedInstall.h"

namespace llocr {

namespace {

const QString kServerName =
#ifdef Q_OS_WIN
    QStringLiteral("llama-server.exe");
#else
    QStringLiteral("llama-server");
#endif

QString locateServer(const QString &root)
{
    QStringList stack;
    stack.append(root);
    while (!stack.isEmpty()) {
        const QString dir = stack.takeLast();
        QDirIterator it{QDir(dir)};
        while (it.hasNext()) {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isDir()) {
                const QString name = fi.fileName();
                if (name != QStringLiteral(".") && name != QStringLiteral(".."))
                    stack.append(fi.absoluteFilePath());
            } else if (fi.fileName() == kServerName) {
                return fi.absoluteFilePath();
            }
        }
    }
    return QString();
}

qint64 directorySize(const QString &root)
{
    qint64 total = 0;
    QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

ProbeResult probeInstalledBinary(const QString &serverAbs)
{
    constexpr int kInstallProbeTimeoutMs = 120000;
    return RuntimeLocator::probe(serverAbs, kInstallProbeTimeoutMs);
}

}  // namespace

InstallOutput InstallTransaction::start(const QString &archivePath, const ReleaseAsset &asset, RuntimePaths paths, CommitFn commit)
{
    InstallOutput out;

    const QFileInfo zfi(archivePath);
    if (!zfi.exists()) {
        out.error = QObject::tr("Downloaded archive missing: %1").arg(archivePath);
        return out;
    }
    if (asset.size > 0 && zfi.size() != asset.size) {
        out.error = QObject::tr("Downloaded archive size mismatch (expected %1, got %2)").arg(asset.size).arg(zfi.size());
        return out;
    }
    if (!asset.sha256.isEmpty()) {
        const QString actual = QString::fromLatin1(sha256File(archivePath));
        if (actual != asset.sha256) {
            out.error = QStringLiteral("sha256 mismatch for the downloaded archive");
            return out;
        }
    } else {
        out.warning = QObject::tr("This release did not publish a sha256 digest; "
                                  "integrity was not verified");
    }

    const QString uuid = QUuid::createUuid().toString();
    StagedInstall staged(StagedInstall::stagingPathFor(paths.stagingDir(), uuid),
                         QString());  // the final tag is only known after the probe
    if (!staged.isValid()) {
        out.error = staged.error();
        return out;
    }

    const ExtractResult ex = ArchiveExtractor::extractArchive(archivePath, staged.stagingPath());
    if (!ex.error.isEmpty()) {
        out.error = ex.error;
        return out;
    }
    if (out.warning.isEmpty())
        out.warning = ex.warning;

    const QString serverAbs = locateServer(staged.stagingPath());
    if (serverAbs.isEmpty()) {
        out.error = QObject::tr("No llama-server binary found in the release archive");
        return out;
    }
    const ProbeResult probe = probeInstalledBinary(serverAbs);
    if (!probe.ok) {
        out.error = QObject::tr("Installed server failed the probe: %1").arg(probe.error);
        return out;
    }
    if (probe.capabilities.belowMinimum) {
        out.error = QObject::tr("This release is below the minimum supported build (%1)").arg(QLatin1String(ServerCapabilities::kMinimumSupportedBuild));
        return out;
    }
    QString build = probe.capabilities.build;
    if (build.isEmpty())
        build = asset.build;
    if (build.isEmpty())
        build = QStringLiteral("unknown");

    const QString backend = asset.backend.isEmpty() ? ReleaseCatalog::defaultBackendFor(asset.os, asset.arch) : asset.backend;
    const QString finalTag = QStringLiteral("llama.cpp-%1-%2-%3-%4").arg(build, backend, asset.os, asset.arch);
    const QString finalDir = paths.installDir(finalTag);
    staged.setFinalPath(finalDir);
    if (!staged.commit(&out.error))
        return out;

    out.ok = true;
    out.build = build;
    out.tag = finalTag;
    const QString relServer = QDir(staged.stagingPath()).relativeFilePath(serverAbs);
    out.serverPath = QDir(finalDir).filePath(relServer);

    if (commit)
        commit(out);
    return out;
}

void InstallTransaction::cleanupStaging(RuntimePaths paths, bool keepModelStaging)
{
    const QStringList names = QDir(paths.stagingDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        if (keepModelStaging && name.startsWith(QLatin1String("model-")))
            continue;
        QDir(QDir(paths.stagingDir()).filePath(name)).removeRecursively();
    }
}

QString InstallTransaction::cleanupUnusedBuilds(RuntimePaths paths, const QString &keepTag)
{
    if (keepTag.isEmpty()) {
        return QCoreApplication::translate("llocr::InstallTransaction", "Cannot tell which build is in use — nothing was removed. Install or activate a build first.");
    }
    int removed = 0;
    const QStringList names = QDir(paths.runtimeDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        if (name == keepTag)
            continue;
        if (!QDir(paths.installDir(name)).removeRecursively())
            return QStringLiteral("Unable to remove %1").arg(name);
        ++removed;
    }
    return QStringLiteral("Removed %1 build(s)").arg(removed);
}

QList<InstalledBuildInfo> InstallTransaction::scanInstalledBuilds(const RuntimePaths &paths)
{
    QList<InstalledBuildInfo> result;
    const QDir runtime(paths.runtimeDir());
    if (!runtime.exists())
        return result;

    const QString prefix = QStringLiteral("llama.cpp-");
    const QStringList dirs = runtime.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : dirs) {
        if (!name.startsWith(prefix))
            continue;  // staging/ and one-off leftovers are not installs
        InstalledBuildInfo info;
        info.tag = name;
        const QStringList parts = name.mid(prefix.size()).split(QLatin1Char('-'));
        if (!parts.isEmpty())
            info.build = parts.first();
        if (parts.size() >= 4) {
            info.backend = QStringList(parts.mid(1, parts.size() - 3)).join(QLatin1Char('-'));
            if (info.backend == QLatin1String("cpu") && ReleaseCatalog::defaultBackendFor(parts.at(parts.size() - 2), parts.last()) == QLatin1String("metal"))
                info.backend = QStringLiteral("metal");
        } else if (parts.size() == 3) {
            info.backend = parts.at(1);
        }
        const QString server = locateServer(runtime.filePath(name));
        if (!server.isEmpty())
            info.serverPath = server;
        info.sizeBytes = directorySize(runtime.filePath(name));
        result.append(info);
    }

    std::sort(result.begin(), result.end(), [](const InstalledBuildInfo &a, const InstalledBuildInfo &b) {
        const int ab = a.build.size() > 1 && a.build.startsWith(QLatin1Char('b')) ? a.build.mid(1).toInt() : -1;
        const int bb = b.build.size() > 1 && b.build.startsWith(QLatin1Char('b')) ? b.build.mid(1).toInt() : -1;
        if (ab != bb)
            return ab > bb;
        return a.tag < b.tag;
    });
    return result;
}

}  // namespace llocr