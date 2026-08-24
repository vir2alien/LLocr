#include <QDir>
#include <QStandardPaths>

#include "runtime/RuntimePaths.h"

namespace llocr {

RuntimePaths::RuntimePaths(QString rootDir, QString modelsDir)
    : m_rootDir(rootDir.isEmpty() ? defaultRootDir() : rootDir)
    , m_modelsDir(modelsDir.isEmpty()
                      ? QDir(m_rootDir).filePath(QStringLiteral("models"))
                      : modelsDir)
{
}

QString RuntimePaths::defaultRootDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString RuntimePaths::rootDir() const
{
    return m_rootDir;
}

QString RuntimePaths::modelsDir() const
{
    return m_modelsDir;
}

QString RuntimePaths::runtimeDir() const
{
    return QDir(m_rootDir).filePath(QStringLiteral("runtime"));
}

QString RuntimePaths::stagingDir() const
{
    return QDir(runtimeDir()).filePath(QStringLiteral("staging"));
}

QString RuntimePaths::installDir(const QString &tag) const
{
    return QDir(runtimeDir()).filePath(tag);
}

QString RuntimePaths::logsDir() const
{
    return QDir(m_rootDir).filePath(QStringLiteral("logs"));
}

QString RuntimePaths::cacheDir() const
{
    return QDir(m_rootDir).filePath(QStringLiteral("cache"));
}

QString RuntimePaths::modelDir(const QString &repo) const
{
    return QDir(m_modelsDir).filePath(repo);
}

QString RuntimePaths::instanceLockPath() const
{
    return QDir(m_rootDir).filePath(QStringLiteral(".instance.lock"));
}

QString RuntimePaths::serverLogPath() const
{
    return QDir(logsDir()).filePath(QStringLiteral("llama-server.log"));
}

QString RuntimePaths::ensureDirectories() const
{
    const QStringList dirs = {m_rootDir,   m_modelsDir,           runtimeDir(),
                              stagingDir(), logsDir(),            cacheDir()};
    for (const QString &dir : dirs) {
        if (!QDir().mkpath(dir))
            return QObject::tr("Unable to create directory: %1").arg(dir);
    }
    return QString();
}

}  // namespace llocr