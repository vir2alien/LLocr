#include "app/OrientationController.h"

namespace llocr {

OrientationController::OrientationController(QObject *parent) : QObject(parent) {}

void OrientationController::check(const QList<int> &pages, const ImageProvider &provider, const Arbiter &arbiter)
{
    m_pending.clear();
    m_unclear.clear();
    m_noText.clear();

    for (const int index : pages) {
        const QImage image = provider ? provider(index) : QImage();
        if (image.isNull()) {
            m_unclear.append(index);
            continue;
        }
        const OrientationVerdict verdict = detectPageOrientation(image);
        if (!verdict.textLike) {
            // A page without text has no way up to get wrong: it stays as it
            // is and the model is not asked about it.
            m_noText.append(index);
            continue;
        }
        if (!arbiter) {
            m_unclear.append(index);
            continue;
        }
        m_pending.append(index);
        arbiter(index, verdict);
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
    for (const int index : m_noText)
        emit noText(index);
    for (const int index : m_unclear)
        emit unclear(index);
    emit finished();
}

}  // namespace llocr
