#include "app/PageRotation.h"

#include <cmath>

#include <QPainter>
#include <QTransform>

namespace llocr {

namespace {

// Straightening below this is invisible and not worth a resample.
constexpr double kDeskewEpsilon = 0.05;

}  // namespace

int normalizeRotation(int degrees)
{
    const int turns = static_cast<int>(std::lround(static_cast<double>(degrees) / kRotationStep)) % 4;
    return (turns + 4) % 4 * kRotationStep;
}

QSize rotatedSize(const QSize &size, int degrees)
{
    const int turn = normalizeRotation(degrees);
    if (!size.isValid() || size.isEmpty())
        return size;
    if (turn == 90 || turn == 270)
        return QSize(size.height(), size.width());
    return size;
}

QImage rotateImage(const QImage &image, int degrees)
{
    const int turn = normalizeRotation(degrees);
    if (image.isNull() || turn == 0)
        return image;
    // QTransform::rotate turns the coordinate system counterclockwise, and the
    // image's y axis points down: the picture turns clockwise, which is the
    // direction the page's rotation counts in.
    QTransform transform;
    transform.rotate(turn);
    // The quarter turns land on the pixel grid, so the fast transformation
    // moves the pixels instead of resampling them: a scan stays sharp and the
    // geometry the model reports against it stays exact.
    return image.transformed(transform, Qt::FastTransformation);
}

QRect deskewedRect(const QSize &size, double degrees)
{
    if (!size.isValid() || size.isEmpty() || std::abs(degrees) < kDeskewEpsilon)
        return QRect(QPoint(0, 0), size);
    QTransform transform;
    transform.rotate(degrees);
    return transform.mapRect(QRectF(0.0, 0.0, size.width(), size.height())).toAlignedRect();
}

QSize deskewedSize(const QSize &size, double degrees)
{
    return deskewedRect(size, degrees).size();
}

QImage deskewImage(const QImage &image, double degrees)
{
    if (image.isNull() || std::abs(degrees) < kDeskewEpsilon)
        return image;
    QTransform transform;
    transform.rotate(degrees);
    const QRect bounds = transform.mapRect(QRectF(0.0, 0.0, image.width(), image.height())).toAlignedRect();
    // The straightened page grows: the corners the turn bares are paper.
    QImage turned(bounds.size(), QImage::Format_RGB32);
    turned.fill(Qt::white);
    QPainter painter(&turned);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setTransform(QTransform::fromTranslate(-bounds.x(), -bounds.y()) * transform);
    painter.drawImage(0, 0, image);
    painter.end();
    return turned;
}

}  // namespace llocr
