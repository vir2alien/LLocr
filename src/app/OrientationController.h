#pragma once

#include <QImage>
#include <QObject>

#include <functional>

namespace llocr {

// The check that runs before the pages are recognized: a page that lies the
// wrong way up is found and reported, so the text can be read the right way
// round.
//
// The picture reads the axis of the text: a page whose text runs across lies
// straight, a page on its side lies at a quarter turn. Which way up the
// straight page is, the picture cannot see — the typographic signs of it
// carry the sign of the alphabet, and reading them turns the pages that lie
// the right way up (measured on the corpus of the project: 16 wrong turns
// against 7 right ones). So the check names the pages the user has to turn by
// hand and leaves them as they are.
class OrientationController : public QObject
{
    Q_OBJECT

public:
    // The picture of a page, as the recognition will see it.
    using ImageProvider = std::function<QImage(int index)>;

    explicit OrientationController(QObject *parent = nullptr);

    // Starts the check on a worker and returns at once; the findings arrive
    // through the signals on the thread the controller lives on, one run's
    // verdicts only after its last page. A check started while another is
    // still running replaces it: only the newest one reports.
    void check(const QList<int> &pages, const ImageProvider &provider);

signals:
    // The page lies on its side: the direction of the turn is not read, the
    // user turns it by hand.
    void sideways(int index);
    // The check read the axis and measured a skew worth straightening: the
    // correction is the angle the renderer applies to level the lines.
    void skewed(int index, double degrees);
    // The picture cannot read the page at all.
    void unclear(int index);
    // The page carries no text: there is no way up to get wrong.
    void noText(int index);
    void finished();

private:
    struct Findings {
        QList<int> sideways;
        QList<int> unclear;
        QList<int> noText;
        QList<QPair<int, double>> skewed;
    };

    void complete(const Findings &findings);

    quint64 m_runGeneration = 0;
};

}  // namespace llocr
