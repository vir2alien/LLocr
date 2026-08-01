#include "app/OcrImageProvider.h"

#include "app/AppController.h"

namespace llocr {

OcrImageProvider::OcrImageProvider(AppController* controller)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_controller(controller)
{
}

QImage OcrImageProvider::requestImage(const QString& id, QSize* size,
                                      const QSize& requestedSize)
{
    QString key = id;
    const int q = key.indexOf('?');
    if (q >= 0)
        key = key.left(q);

    QImage image;
    if (key.startsWith(QStringLiteral("page/"))) {
        bool ok = false;
        const int index = key.mid(5).toInt(&ok);
        if (ok)
            image = m_controller->pageImage(index);
    } else {
        image = m_controller->currentImage();
    }

    if (image.isNull())
        return image;

    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        image = image.scaled(requestedSize, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    if (size)
        *size = image.size();
    return image;
}

} // namespace llocr
