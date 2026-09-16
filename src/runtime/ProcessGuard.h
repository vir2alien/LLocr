#pragma once

#include <QProcess>

namespace llocr {

class ProcessGuard
{
public:
    static void install(QProcess &proc);
    static void attachParent(QProcess &proc);
    static qint64 currentPid();
};

}  // namespace llocr