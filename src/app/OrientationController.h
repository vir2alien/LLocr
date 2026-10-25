#pragma once

#include <QImage>
#include <QObject>

#include <functional>

#include "app/PageOrientation.h"

namespace llocr {

// The check that runs before the pages are recognized: a page that lies the
// wrong way up is turned first, so the model reads the text the right way
// round.
//
// The picture reads the axis of the text and hands the model the turns the
// page may need: a page whose text runs across lies straight, a page on its
// side lies at a quarter turn. The model settles the way up and the pages it
// is not sure about are left as they are; the run waits for its answers.
class OrientationController : public QObject
{
    Q_OBJECT

public:
    // The picture of a page, as the recognition will see it.
    using ImageProvider = std::function<QImage(int index)>;
    // The model settles the page: it answers later, through settle().
    using Arbiter = std::function<void(int index, const OrientationVerdict &verdict)>;

    explicit OrientationController(QObject *parent = nullptr);

    void check(const QList<int> &pages, const ImageProvider &provider, const Arbiter &arbiter);

    // The model's answer for a page: the degrees it has to be turned by, or
    // -1 when the model is not sure either.
    void settle(int index, int degrees);

signals:
    void turned(int index, int degrees);
    void unclear(int index);
    void noText(int index);
    void finished();

private:
    void turn(int index, int degrees);
    void complete();

    QList<int> m_pending;  // The pages waiting for the model's answer.
    QList<int> m_unclear;  // The pages whose orientation stayed unknown.
    QList<int> m_noText;   // The pages that carry no text at all.
};

}  // namespace llocr
