#include "core/ByteSize.h"

namespace llocr {

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    const double gib = 1073741824.0;
    const double mib = 1048576.0;
    if (bytes >= gib)
        return QString::number(bytes / gib, 'f', 1) + QStringLiteral(" GiB");
    if (bytes >= mib)
        return QString::number(bytes / mib, 'f', 1) + QStringLiteral(" MiB");
    return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KiB");
}

}  // namespace llocr
