#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace llocr {

QByteArray encodeImageDataUrl(const QImage &image, const QByteArray &format = QByteArrayLiteral("png"), int quality = -1);

}  // namespace llocr
