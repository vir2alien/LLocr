#include "runtime/StagedInstall.h"

#include <QDir>
#include <QFileInfo>

namespace llocr {

StagedInstall::StagedInstall(const QString &stagingPath, const QString &finalDir)
    : m_stagingPath(stagingPath)
    , m_finalPath(finalDir)
{
    // A leftover staging directory (previous crash) is resumed, not merged.
    QDir().mkpath(QFileInfo(m_stagingPath).absolutePath());
    if (!QDir().mkpath(m_stagingPath))
        m_error = QObject::tr("Unable to create the staging directory %1").arg(m_stagingPath);
}

StagedInstall::~StagedInstall()
{
    if (!m_committed)
        discard();
}

QString StagedInstall::stagingPathFor(const QString &stagingRoot, const QString &name)
{
    return QDir(stagingRoot).filePath(name);
}

bool StagedInstall::commit(QString *error)
{
    auto fail = [this, error](const QString &message) {
        m_error = message;
        if (error)
            *error = message;
        return false;
    };
    if (!m_error.isEmpty())
        return fail(m_error);
    if (m_committed)
        return fail(QObject::tr("The install was already committed"));

    QString backupDir;
    if (QFileInfo::exists(m_finalPath)) {
        backupDir = m_finalPath + QStringLiteral(".old-")
                    + QString::number(QDateTime::currentMSecsSinceEpoch());
        if (!QDir().rename(m_finalPath, backupDir))
            return fail(QObject::tr("Unable to move the existing install aside"));
    }
    if (!QDir().rename(m_stagingPath, m_finalPath)) {
        if (!backupDir.isEmpty())
            QDir().rename(backupDir, m_finalPath);  // roll back
        return fail(QObject::tr("Atomic rename of the install into place failed"));
    }
    m_committed = true;
    if (!backupDir.isEmpty())
        QDir(backupDir).removeRecursively();
    return true;
}

void StagedInstall::discard()
{
    if (m_stagingPath.isEmpty())
        return;
    QDir dir(m_stagingPath);
    if (dir.exists())
        dir.removeRecursively();
}

}  // namespace llocr
