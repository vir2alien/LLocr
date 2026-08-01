#pragma once

#include <QQuickImageProvider>

namespace llocr {

class AppController;

class OcrImageProvider : public QQuickImageProvider {
public:
    explicit OcrImageProvider(AppController* controller);

    QImage requestImage(const QString& id, QSize* size,
                        const QSize& requestedSize) override;

private:
    AppController *m_controller;
};

} // namespace llocr
