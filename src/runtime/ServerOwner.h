#pragma once

#include <QString>

namespace llocr {

struct ServerOwnerRecord {
    qint64 pid = 0;        ///< llama-server process id
    qint64 parentPid = 0;  ///< the LLocr process that started it
    int port = 0;          ///< port it was serving on
    QString program;       ///< absolute path of the binary

    bool isValid() const { return pid > 0; }
};

class ServerOwner
{
public:
    static ServerOwnerRecord read(const QString &ownerJsonPath);

    static bool isOrphan(const ServerOwnerRecord &record);

    static bool findOrphan(const QString &ownerJsonPath, ServerOwnerRecord *out);

    static void clear(const QString &ownerJsonPath);
};

}  // namespace llocr
