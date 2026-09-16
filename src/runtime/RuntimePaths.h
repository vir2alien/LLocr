#pragma once

#include <QCoreApplication>
#include <QString>

namespace llocr {

class RuntimePaths
{
    Q_DECLARE_TR_FUNCTIONS(RuntimePaths)

public:
    explicit RuntimePaths(QString rootDir = QString(), QString modelsDir = QString());

    QString rootDir() const;
    QString modelsDir() const;

    QString runtimeDir() const;
    QString stagingDir() const;
    QString installDir(const QString &tag) const;
    QString logsDir() const;
    QString profilesDir() const;
    QString cacheDir() const;
    QString modelDir(const QString &repo) const;
    QString instanceLockPath() const;
    QString installLockPath() const;
    QString serverLogPath() const;

    QString ensureDirectories() const;

    static QString defaultRootDir();

private:
    QString m_rootDir;
    QString m_modelsDir;
};

}  // namespace llocr