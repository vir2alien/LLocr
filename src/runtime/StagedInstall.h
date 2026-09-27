#pragma once

#include <QString>

namespace llocr {

class StagedInstall
{
public:
    StagedInstall(const QString &stagingPath, const QString &finalDir);
    ~StagedInstall();

    StagedInstall(const StagedInstall &) = delete;
    StagedInstall &operator=(const StagedInstall &) = delete;

    bool isValid() const { return m_error.isEmpty(); }
    const QString &error() const { return m_error; }
    const QString &stagingPath() const { return m_stagingPath; }
    const QString &finalPath() const { return m_finalPath; }

    void setFinalPath(const QString &finalDir) { m_finalPath = finalDir; }

    bool commit(QString *error = nullptr);

    void discard();
    static QString stagingPathFor(const QString &stagingRoot, const QString &name);

private:
    QString m_stagingPath;
    QString m_finalPath;
    QString m_error;
    bool m_committed = false;
};

}  // namespace llocr
