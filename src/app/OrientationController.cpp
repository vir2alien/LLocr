#include "app/OrientationController.h"

#include "app/PageOrientation.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

namespace llocr {

namespace {

// A straightening below this is invisible and not worth a resample of the
// page; the check itself cannot resolve less.
constexpr double kAutoDeskewDegrees = 0.5;

}  // namespace

OrientationController::OrientationController(QObject *parent) : QObject(parent) {}

void OrientationController::check(const QList<int> &pages, const ImageProvider &provider)
{
    const quint64 generation = ++m_runGeneration;

    auto *watcher = new QFutureWatcher<Findings>(this);
    connect(watcher, &QFutureWatcher<Findings>::finished, this, [this, watcher, generation]() {
        watcher->deleteLater();
        if (generation != m_runGeneration)
            return;
        complete(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([pages, provider]() {
        Findings findings;
        for (const int index : pages) {
            const QImage image = provider ? provider(index) : QImage();
            if (image.isNull()) {
                findings.unclear.append(index);
                continue;
            }
            const OrientationVerdict verdict = detectPageOrientation(image);
            if (verdict.axisKnown && std::abs(verdict.deskewDegrees) >= kAutoDeskewDegrees) {
                // The page is skewed: the correction levels the lines before the
                // text is read. A page on its side is straightened too — the
                // quarter turn it still waits for is a different question.
                findings.skewed.append({index, verdict.deskewDegrees});
            }
            if (!verdict.textLike) {
                // A page without text has no way up to get wrong: it stays as it
                // is and nothing is reported about it.
                findings.noText.append(index);
                continue;
            }
            if (!verdict.axisKnown) {
                // The picture cannot tell whether the page lies straight or on its
                // side: the page is left as it is and reported.
                findings.unclear.append(index);
                continue;
            }
            if (verdict.sideways) {
                // The page lies on its side. The direction of the turn is not read
                // by the picture — it would turn the pages that lie the right way
                // up — so the page is left to the user.
                findings.sideways.append(index);
            }
        }
        return findings;
    }));
}

void OrientationController::complete(const Findings &findings)
{
    for (const auto &[index, degrees] : findings.skewed)
        emit skewed(index, degrees);
    for (const int index : findings.sideways)
        emit sideways(index);
    for (const int index : findings.unclear)
        emit unclear(index);
    for (const int index : findings.noText)
        emit noText(index);
    emit finished();
}

}  // namespace llocr
