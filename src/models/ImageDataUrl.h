#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace llocr {

/// A `data:<format>;base64,<...>` URL for an image, built entirely in
/// QByteArray (ADR 111).
///
/// The old helper went QString -> Latin1 -> `QString::arg` -> QJsonDocument,
/// which is three extra copies of a multi-megabyte string per page (a base64
/// image is ~4/3 of the PNG size, and QString doubles it again).
QByteArray encodeImageDataUrl(const QImage &image, const QByteArray &format = QByteArrayLiteral("png"),
                              int quality = -1);

}  // namespace llocr
