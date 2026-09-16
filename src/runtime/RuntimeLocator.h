#pragma once

#include <QCoreApplication>
#include <QString>

#include "runtime/ServerCapabilities.h"

namespace llocr {

struct ProbeResult {
    bool ok = false;
    QString version;   // trimmed `--version` stdout, if any
    QString error;     // human-readable when !ok
    ServerCapabilities capabilities;
};

class RuntimeLocator
{
    Q_DECLARE_TR_FUNCTIONS(RuntimeLocator)

public:
    static ProbeResult probe(const QString &binaryPath, int timeoutMs = 5000);
    static ProbeResult probeCached(const QString &binaryPath, int timeoutMs = 5000);
    static ProbeResult probeCached(const QString &binaryPath, const QString &cacheDir,
                                   int timeoutMs = 5000);
    static bool cachedProbe(const QString &binaryPath, const QString &cacheDir,
                            ProbeResult &out);
    static QString probeSummary(const ProbeResult &r);
    static QString autoDiscover(int timeoutMs = 5000);
    static QString ensureExecutable(const QString &binaryPath, bool pathManaged,
                                    bool &needsConfirmation);

private:
    static QString runProbe(const QString &binaryPath, QStringList args,
                            int timeoutMs, QString &error);

    static bool probeFromCache(const QString &binaryPath, ProbeResult &out);
    static void cacheProbe(const QString &binaryPath, const ProbeResult &result);
    static bool probeFromDiskCache(const QString &binaryPath, const QString &cacheDir,
                                   ProbeResult &out);
    static void writeDiskCache(const QString &binaryPath, const QString &cacheDir,
                               const ProbeResult &result);
    static ProbeResult probeImpl(const QString &binaryPath, int timeoutMs);
};

}  // namespace llocr