#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QUuid>

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

}  // namespace

InstallOutput InstallTransaction::start(const QString &zipPath,
                                        const ReleaseAsset &asset,
                                        RuntimePaths paths, CommitFn commit)
{
    InstallOutput out;

    // 2. Verify size, then sha256 (when the release published one).
    const QFileInfo zfi(zipPath);
    if (!zfi.exists()) {
        out.error = QObject::tr("Downloaded archive missing: %1").arg(zipPath);
        return out;
    }
    if (asset.size > 0 && zfi.size() != asset.size) {
        out.error = QObject::tr("Downloaded archive size mismatch (expected %1, got %2)")
                        .arg(asset.size)
                        .arg(zfi.size());
        return out;
    }
    if (!asset.sha256.isEmpty()) {
        const QString actual = fileSha256(zipPath);
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

    const ExtractResult ex = ArchiveExtractor::extractZip(zipPath, stagingPath);
    if (!ex.error.isEmpty()) {
        out.error = ex.error;
        QDir(stagingPath).removeRecursively();
        return out;
    }
    if (out.warning.isEmpty())
        out.warning = ex.warning;

    // 5-6. Locate the binary and probe it.
    const QString serverAbs = locateServer(stagingPath);
    if (serverAbs.isEmpty()) {
        out.error = QObject::tr("No llama-server binary found in the release archive");
        QDir(stagingPath).removeRecursively();
        return out;
    }
    const ProbeResult probe = RuntimeLocator::probe(serverAbs);
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

    // 7. Atomic rename staging/<uuid> -> runtime/<finalTag>.
    const QString finalTag = QStringLiteral("llama.cpp-%1-%2-%3-%4")
                                 .arg(build, asset.backend, asset.os, asset.arch);
    const QString finalDir = paths.installDir(finalTag);
    if (QFileInfo::exists(finalDir)) {
        if (!QDir(finalDir).removeRecursively()) {
            out.error = QObject::tr("Unable to replace an existing install directory");
            QDir(stagingPath).removeRecursively();
            return out;
        }
    }
    if (!QDir().rename(stagingPath, finalDir)) {
        out.error = QObject::tr("Atomic rename of the install into place failed");
        QDir(stagingPath).removeRecursively();
        return out;
    }

    out.ok = true;
    out.build = build;
    out.tag = finalTag;
    out.serverPath = QDir(finalDir).filePath(kServerName);

    // 8. Commit settings last, after the install is live.
    if (commit)
        commit(out);
    return out;
}

void InstallTransaction::cleanupStaging(RuntimePaths paths)
{
    QDirIterator it{QDir(paths.stagingDir())};
    while (it.hasNext()) {
        it.next();
        if (it.fileInfo().isDir())
            QDir(it.fileInfo().absoluteFilePath()).removeRecursively();
    }
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

}  // namespace llocr