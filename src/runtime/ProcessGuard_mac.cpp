#include <QCoreApplication>
#include <QProcess>

#include "runtime/ProcessGuard.h"

namespace llocr {

void ProcessGuard::install(QProcess &)
{
    // No child-process modifier on macOS: there is nothing reliable to do
    // in the child. The owner.json handshake + next-start detection cover it.
}

void ProcessGuard::attachParent(QProcess &)
{
    // No strong attach available;
}

qint64 ProcessGuard::currentPid()
{
    return QCoreApplication::applicationPid();
}

}  // namespace llocr