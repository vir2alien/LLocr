#include "app/OcrImageProvider.h"

#include "app/AppController.h"

namespace llocr {

// Every consumer of this provider is an asynchronous QML Image, so
// requestImage runs on an image-loader worker — for the thumbnails and for
// the page preview alike. What it touches is therefore guarded — the
// thumbnail and preview caches by their own mutexes, the document by the
// document lock — and the rendering uses the document handles of the calling
// thread (ADR 209); the full preview render itself starts on the GUI thread,
// where previewImage queues it from a worker.
OcrImageProvider::OcrImageProvider(AppController *controller) : QQuickImageProvider(QQuickImageProvider::Image), m_controller(controller) {}

QImage OcrImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
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
            image = m_controller->pageThumbnail(index);
    } else {
        image = m_controller->previewImage(m_controller->currentPage());
        if (image.isNull())
            image = m_controller->pageThumbnail(m_controller->currentPage());
    }

    if (image.isNull())
        return image;

    if (requestedSize.isValid() && !requestedSize.isEmpty()) {
        // Twice the requested size, so that zooming into the page does not
        // show a blown-up preview.
        const QSize want = image.size().boundedTo(requestedSize * 2);
        image = image.scaled(want, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (size)
        *size = image.size();
    return image;
}

}  // namespace llocr
