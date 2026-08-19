#include "app/OcrImageProvider.h"

#include <QStringList>

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
    } else if (key.startsWith(QStringLiteral("crop/"))) {
        const QStringList parts = key.mid(5).split(QLatin1Char('/'));
        if (parts.size() == 2) {
            bool okPage = false, okBox = false;
            const int pageIndex = parts.at(0).toInt(&okPage);
            const int boxIndex = parts.at(1).toInt(&okBox);
            if (okPage && okBox)
                image = m_controller->croppedImage(pageIndex, boxIndex);
        } else if (parts.size() == 1) {
            bool okBox = false;
            const int boxIndex = parts.at(0).toInt(&okBox);
            if (okBox)
                image = m_controller->croppedImage(m_controller->currentPage(), boxIndex);
        }
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
