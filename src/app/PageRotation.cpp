#include "app/PageRotation.h"

#include <cmath>

#include <QTransform>

namespace llocr {

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

}  // namespace llocr
