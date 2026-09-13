#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QThread>
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

// Depth-first search for the server binary beneath `root`.
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

// Probes a freshly-extracted server binary with a LONG timeout. On macOS the
// FIRST run of a brand-new llama.cpp build compiles the Metal shader cache
// (GGML metal library init writes ~20-30 MB under com.apple.metal) and can
// block for many seconds (observed 17 s cold on this machine) before --version
// prints. The regular 5 s probe is far too short, and retrying with a short
// timeout would kill the compiler mid-flight every time, so the cache would
// never finish building (ADR 53). One patient run lets the cache complete; all
// subsequent probes (UI Check, managed start) then answer in milliseconds.
ProbeResult probeInstalledBinary(const QString &serverAbs)
{
    // A generous budget: covers cold Metal shader compilation on slow machines
    // and busy systems; a truly frozen binary still aborts the install.
    constexpr int kInstallProbeTimeoutMs = 120000;
    return RuntimeLocator::probe(serverAbs, kInstallProbeTimeoutMs);
}

}  // namespace

InstallOutput InstallTransaction::start(const QString &archivePath,
                                        const ReleaseAsset &asset,
                                        RuntimePaths paths, CommitFn commit)
{
    InstallOutput out;

    // 2. Verify size, then sha256 (when the release published one).
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

    // 3-4. Extract into a unique staging dir with the hardened extractor.
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

    // 5-6. Locate the binary and probe it. The first exec of a fresh macOS
    // binary may have to compile the Metal shader cache (tens of seconds) —
    // probeInstalledBinary() uses a long timeout so the cache can complete
    // instead of timing out (ADR 53).
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

    // 7. Atomic rename staging/<uuid> -> runtime/<finalTag>. When a previous
    // install exists, move it aside first so a failed rename cannot destroy
    // the last good build (ADR 39 transactionality). Universal assets (empty
    // backend token, e.g. `...-bin-macos-arm64`) get a stable "cpu" label so
    // the tag never carries a double separator.
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
        // Roll back: restore the previous install if it was moved aside.
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
    // The binary may live under a top-level build folder (e.g.
    // `llama-b10825/llama-server` in the macOS tarballs); record the path
    // relative to the staging root so it resolves inside the renamed install.
    const QString relServer = QDir(stagingPath).relativeFilePath(serverAbs);
    out.serverPath = QDir(finalDir).filePath(relServer);

    // 8. Commit settings last, after the install is live.
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
        // Tag layout: llama.cpp-<build>-<backend>-<os>-<arch>, where the
        // backend itself may carry a hyphen ("cuda-cu12"), so os/arch are
        // taken from the tail and everything in between is the backend token.
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

    // Newest first; unparsable build numbers ("unknown", malformed tags) sort
    // last, then alphabetically by tag for a stable order.
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