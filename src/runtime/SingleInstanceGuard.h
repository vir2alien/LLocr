#pragma once

#include <QLockFile>
#include <QObject>
#include <QString>

namespace llocr {

class SingleInstanceGuard : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceGuard(QString lockFilePath, QObject *parent = nullptr);

    bool tryAcquire(QString &errorMessage);
    void release();
    bool holdsLock() const { return m_holdsLock; }

private:
    QLockFile m_lockFile;
    bool m_holdsLock = false;
};

}  // namespace llocr