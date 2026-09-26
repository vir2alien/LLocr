#include "models/ImageDataUrl.h"

#include <QBuffer>

namespace llocr {

QByteArray encodeImageDataUrl(const QImage &image, const QByteArray &format, int quality)
{
    if (image.isNull())
        return {};

    QByteArray raw;
    QBuffer buffer(&raw);
    if (!buffer.open(QIODevice::WriteOnly))
        return {};
    const QByteArray writerFormat = format.isEmpty() ? QByteArrayLiteral("png") : format.toLower();
    if (!image.save(&buffer, writerFormat.constData(), quality))
        return {};
    buffer.close();

    QByteArray url;
    url.reserve(24 + writerFormat.size() + 1 + raw.size() * 4 / 3 + 8);
    url += "data:image/";
    url += writerFormat;
    url += ";base64,";
    url += raw.toBase64();
    return url;
}

}  // namespace llocr
