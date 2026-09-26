#include "runtime/ServerOwner.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include "runtime/ProcessGuard.h"

namespace llocr {

ServerOwnerRecord ServerOwner::read(const QString &ownerJsonPath)
{
    ServerOwnerRecord record;
    if (ownerJsonPath.isEmpty() || !QFile::exists(ownerJsonPath))
        return record;

    QFile file(ownerJsonPath);
    if (!file.open(QIODevice::ReadOnly))
        return record;
    const QByteArray raw = file.readAll();
    file.close();

    // A half-written record (crash between write and commit) is not an orphan
    // we may act on.
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return record;

    const QJsonObject object = document.object();
    record.pid = static_cast<qint64>(object.value(QStringLiteral("pid")).toDouble());
    record.parentPid = static_cast<qint64>(object.value(QStringLiteral("parentPid")).toDouble());
    record.port = object.value(QStringLiteral("port")).toInt();
    record.program = object.value(QStringLiteral("program")).toString();
    return record;
}

bool ServerOwner::isOrphan(const ServerOwnerRecord &record)
{
    if (!record.isValid())
        return false;
    // Our own server: not an orphan.
    if (record.parentPid == ProcessGuard::currentPid())
        return false;
    if (!ProcessGuard::isProcessAlive(record.pid))
        return false;

    const QString image = ProcessGuard::processImagePath(record.pid);
    if (!image.isEmpty() && !record.program.isEmpty()) {
        // Both sides can be symlinks (llocr.real vs llocr, a shim, /opt/homebrew
        // links), so compare the resolved paths and fall back to the file name.
        const QFileInfo live(image);
        const QFileInfo recorded(record.program);
        if (live.canonicalFilePath() == recorded.canonicalFilePath())
            return true;
        return live.fileName() == recorded.fileName();
    }

    // The image could not be determined on this platform: the record's parent
    // being gone is then the only evidence, and it is a strong one (our own
    // server always has a live parent — ours).
    return record.parentPid > 0 && !ProcessGuard::isProcessAlive(record.parentPid);
}

bool ServerOwner::findOrphan(const QString &ownerJsonPath, ServerOwnerRecord *out)
{
    const ServerOwnerRecord record = read(ownerJsonPath);
    if (isOrphan(record)) {
        if (out)
            *out = record;
        return true;
    }
    return false;
}

void ServerOwner::clear(const QString &ownerJsonPath)
{
    if (!ownerJsonPath.isEmpty())
        QFile::remove(ownerJsonPath);
}

}  // namespace llocr
