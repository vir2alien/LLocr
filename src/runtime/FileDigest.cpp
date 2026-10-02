#include "runtime/FileDigest.h"

#include <QCryptographicHash>
#include <QFile>

namespace llocr {

QByteArray sha256File(const QString &path, bool *ok)
{
    if (ok)
        *ok = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (true) {
        const qint64 n = file.read(buffer.data(), buffer.size());
        if (n < 0)
            return {};
        if (n == 0)
            break;
        hash.addData(QByteArrayView(buffer.constData(), static_cast<int>(n)));
    }
    file.close();
    if (ok)
        *ok = true;
    return hash.result().toHex();
}

}  // namespace llocr