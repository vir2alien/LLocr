#include "app/OrientationController.h"

namespace llocr {

OrientationController::OrientationController(QObject *parent) : QObject(parent) {}

void OrientationController::check(const QList<int> &pages, const ImageProvider &provider, const Arbiter &arbiter)
{
    m_pending.clear();
    m_unclear.clear();

    for (const int index : pages) {
        const QImage image = provider ? provider(index) : QImage();
        if (image.isNull()) {
            m_unclear.append(index);
            continue;
        }
        const OrientationVerdict verdict = detectPageOrientation(image);
        if (!verdict.decided) {
            // The picture cannot read this page: the model settles it.
            if (!arbiter) {
                m_unclear.append(index);
                continue;
            }
            m_pending.append(index);
            arbiter(index);
            continue;
        }
        if (verdict.degrees != 0)
            turn(index, verdict.degrees);
    }

    complete();
}

void OrientationController::settle(int index, int degrees)
{
    if (!m_pending.removeAll(index))
        return;
    if (degrees < 0)
        m_unclear.append(index);
    else if (degrees != 0)
        turn(index, degrees);
    complete();
}

void OrientationController::turn(int index, int degrees)
{
    emit turned(index, degrees);
}

void OrientationController::complete()
{
    if (!m_pending.isEmpty())
        return;
    for (const int index : m_unclear)
        emit unclear(index);
    emit finished();
}

}  // namespace llocr
