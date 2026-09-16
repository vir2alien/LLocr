#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QUuid>

#include <algorithm>

#include "runtime/ArchiveExtractor.h"
#include "runtime/InstallTransaction.h"
#include "runtime/RuntimeLocator.h"
#include "runtime/ServerCapabilities.h"

namespace llocr {

namespace {

// Streams a file into a SHA-256 digest (archives are large).
QString fileSha256(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray chunk;
    while (!f.atEnd()) {
        chunk = f.read(1 << 20);
        hash.addData(chunk);
        if (chunk.isEmpty())
            break;
    }
    return QString::fromLatin1(hash.result().toHex());
}

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

ProbeResult probeInstalledBinary(const QString &serverAbs)
{
    constexpr int kInstallProbeTimeoutMs = 120000;
    return RuntimeLocator::probe(serverAbs, kInstallProbeTimeoutMs);
}

}  // namespace

InstallOutput InstallTransaction::start(const QString &archivePath,
                                        const ReleaseAsset &asset,
                                        RuntimePaths paths, CommitFn commit)
{
    InstallOutput out;

    const QFileInfo zfi(archivePath);
    if (!zfi.exists()) {
        out.error = QObject::tr("Downloaded archive missing: %1").arg(archivePath);
        return out;
    }
    if (asset.size > 0 && zfi.size() != asset.size) {
        out.error = QObject::tr("Downloaded archive size mismatch (expected %1, got %2)")
                        .arg(asset.size)
                        .arg(zfi.size());
        return out;
    }
    if (!asset.sha256.isEmpty()) {
        const QString actual = fileSha256(archivePath);
        if (actual != asset.sha256) {
            out.error = QStringLiteral("sha256 mismatch for the downloaded archive");
            return out;
        }
    } else {
        out.warning = QObject::tr("This release did not publish a sha256 digest; "
                                  "integrity was not verified");
    }

    const QString uuid = QUuid::createUuid().toString();
    const QString stagingPath = QDir(paths.stagingDir()).filePath(uuid);
    if (!QDir().mkpath(stagingPath)) {
        out.error = QObject::tr("Unable to create the staging directory");
        return out;
    }

    const ExtractResult ex = ArchiveExtractor::extractArchive(archivePath, stagingPath);
    if (!ex.error.isEmpty()) {
        out.error = ex.error;
        QDir(stagingPath).removeRecursively();
        return out;
    }
    if (out.warning.isEmpty())
        out.warning = ex.warning;

    const QString serverAbs = locateServer(stagingPath);
    if (serverAbs.isEmpty()) {
        out.error = QObject::tr("No llama-server binary found in the release archive");
        QDir(stagingPath).removeRecursively();
        return out;
    }
    const ProbeResult probe = probeInstalledBinary(serverAbs);
    if (!probe.ok) {
        out.error = QObject::tr("Installed server failed the probe: %1").arg(probe.error);
        QDir(stagingPath).removeRecursively();
        return out;
    }
    if (probe.capabilities.belowMinimum) {
        out.error = QObject::tr("This release is below the minimum supported build (%1)")
                        .arg(QLatin1String(ServerCapabilities::kMinimumSupportedBuild));
        QDir(stagingPath).removeRecursively();
        return out;
    }
    QString build = probe.capabilities.build;
    if (build.isEmpty())
        build = asset.build;
    if (build.isEmpty())
        build = QStringLiteral("unknown");

    const QString finalTag = QStringLiteral("llama.cpp-%1-%2-%3-%4")
                                 .arg(build,
                                      asset.backend.isEmpty()
                                          ? QStringLiteral("cpu")
                                          : asset.backend,
                                      asset.os, asset.arch);
    const QString finalDir = paths.installDir(finalTag);
    QString backupDir;
    if (QFileInfo::exists(finalDir)) {
        backupDir = finalDir + QStringLiteral(".old-") + uuid;
        if (!QDir().rename(finalDir, backupDir)) {
            out.error = QObject::tr("Unable to move the existing install aside");
            QDir(stagingPath).removeRecursively();
            return out;
        }
    }
    if (!QDir().rename(stagingPath, finalDir)) {
        out.error = QObject::tr("Atomic rename of the install into place failed");
        if (!backupDir.isEmpty())
            QDir().rename(backupDir, finalDir);
        QDir(stagingPath).removeRecursively();
        return out;
    }
    if (!backupDir.isEmpty())
        QDir(backupDir).removeRecursively();

    out.ok = true;
    out.build = build;
    out.tag = finalTag;
    const QString relServer = QDir(stagingPath).relativeFilePath(serverAbs);
    out.serverPath = QDir(finalDir).filePath(relServer);

    if (commit)
        commit(out);
    return out;
}

void InstallTransaction::cleanupStaging(RuntimePaths paths)
{
    const QStringList names =
        QDir(paths.stagingDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names)
        QDir(QDir(paths.stagingDir()).filePath(name)).removeRecursively();
}

QString InstallTransaction::cleanupUnusedBuilds(RuntimePaths paths,
                                                const QString &keepTag)
{
    int removed = 0;
    const QStringList names =
        QDir(paths.runtimeDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        if (name == keepTag)
            continue;
        if (!QDir(paths.installDir(name)).removeRecursively())
            return QStringLiteral("Unable to remove %1").arg(name);
        ++removed;
    }
    return QStringLiteral("Removed %1 build(s)").arg(removed);
}

QList<InstalledBuildInfo>
InstallTransaction::scanInstalledBuilds(const RuntimePaths &paths)
{
    QList<InstalledBuildInfo> result;
    const QDir runtime(paths.runtimeDir());
    if (!runtime.exists())
        return result;

    const QString prefix = QStringLiteral("llama.cpp-");
    const QStringList dirs =
        runtime.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &name : dirs) {
        if (!name.startsWith(prefix))
            continue;   // staging/ and one-off leftovers are not installs
        InstalledBuildInfo info;
        info.tag = name;
        const QStringList parts = name.mid(prefix.size()).split(QLatin1Char('-'));
        if (!parts.isEmpty())
            info.build = parts.first();
        if (parts.size() >= 4) {
            info.backend = QStringList(parts.mid(1, parts.size() - 3))
                               .join(QLatin1Char('-'));
        } else if (parts.size() == 3) {
            info.backend = parts.at(1);
        }
        const QString server = locateServer(runtime.filePath(name));
        if (!server.isEmpty())
            info.serverPath = server;
        result.append(info);
    }

    std::sort(result.begin(), result.end(),
              [](const InstalledBuildInfo &a, const InstalledBuildInfo &b) {
                  const int ab = a.build.size() > 1 && a.build.startsWith(QLatin1Char('b'))
                                     ? a.build.mid(1).toInt() : -1;
                  const int bb = b.build.size() > 1 && b.build.startsWith(QLatin1Char('b'))
                                     ? b.build.mid(1).toInt() : -1;
                  if (ab != bb)
                      return ab > bb;
                  return a.tag < b.tag;
              });
    return result;
}

}  // namespace llocr