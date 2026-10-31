#include <QtTest>

#include <QDir>
#include <QImage>
#include <QPainter>
#include <QTransform>

#include <cmath>

#include "app/OrientationController.h"
#include "app/PageOrientation.h"
#include "app/PageRotation.h"
#include "textpage.h"

using namespace llocr;

namespace {

// The page turned by an arbitrary angle — the way a scan with an automatic
// document feeder or a phone photograph arrives, skewed by a degree or
// three. The quarter turns of PageRotation cannot express that.
QImage rotateBy(const QImage &page, double degrees)
{
    QTransform transform;
    transform.rotate(degrees);
    const QRect bounds = transform.mapRect(page.rect());
    QImage turned(bounds.size(), QImage::Format_Grayscale8);
    turned.fill(255);
    QPainter painter(&turned);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setTransform(QTransform::fromTranslate(-bounds.x(), -bounds.y()) * transform);
    painter.drawImage(0, 0, page);
    painter.end();
    return turned;
}

QImage blankPage()
{
    QImage blank(800, 1000, QImage::Format_Grayscale8);
    blank.fill(255);
    return blank;
}

}  // namespace

// The orientation check: the picture reads the axis of the text and says
// whether the page lies straight or on its side. Which way is up it does not
// read — the typographic signs of it carry the sign of the alphabet, and a
// picture that reads them turns the pages that lie the right way up.
class TestPageOrientation : public QObject
{
    Q_OBJECT

private slots:
    void anUprightPageRunsAcross()
    {
        const OrientationVerdict verdict = detectPageOrientation(syntheticTextPage());
        QVERIFY(verdict.textLike);
        QVERIFY(verdict.axisKnown);
        QVERIFY(!verdict.sideways);
    }

    void aPageOnItsSideRunsUpAndDown()
    {
        const QImage page = rotateImage(syntheticTextPage(), 90);
        const OrientationVerdict verdict = detectPageOrientation(page);
        QVERIFY(verdict.textLike);
        QVERIFY(verdict.axisKnown);
        QVERIFY(verdict.sideways);
    }

    void theOtherSideRunsUpAndDownToo()
    {
        const QImage page = rotateImage(syntheticTextPage(), 270);
        const OrientationVerdict verdict = detectPageOrientation(page);
        QVERIFY(verdict.textLike);
        QVERIFY(verdict.axisKnown);
        QVERIFY(verdict.sideways);
    }

    void aSkewedUprightPageStillRunsAcross()
    {
        // A few degrees of skew smear the lines over the profile of an
        // uncompensated read; the check deskews before it reads, so the axis
        // survives, and reports the correction it applied.
        const QImage page = rotateBy(syntheticTextPage(), 3.0);
        const OrientationVerdict verdict = detectPageOrientation(page);
        QVERIFY(verdict.textLike);
        QVERIFY(verdict.axisKnown);
        QVERIFY(!verdict.sideways);
        QVERIFY2(std::abs(verdict.deskewDegrees) >= 1.5 && std::abs(verdict.deskewDegrees) <= 4.5, qPrintable(QStringLiteral("the correction came out %1°").arg(verdict.deskewDegrees)));
    }

    void aSkewedSidePageStillRunsUpAndDown()
    {
        const QImage page = rotateBy(rotateImage(syntheticTextPage(), 90), 3.0);
        const OrientationVerdict verdict = detectPageOrientation(page);
        QVERIFY(verdict.textLike);
        QVERIFY(verdict.axisKnown);
        QVERIFY(verdict.sideways);
        QVERIFY2(std::abs(verdict.deskewDegrees) >= 1.5 && std::abs(verdict.deskewDegrees) <= 4.5, qPrintable(QStringLiteral("the correction came out %1°").arg(verdict.deskewDegrees)));
    }

    void theWayUpIsNotThePicturesBusiness()
    {
        // The picture tells an upside-down page from an upright one by
        // nothing: the sign of the typographic signs it would read carries the
        // sign of the alphabet (Russian has more descenders than ascenders,
        // English the other way round), so reading them turns the pages that
        // lie the right way up. The picture therefore says the same about both
        // — the page is left to the user.
        const OrientationVerdict upright = detectPageOrientation(syntheticTextPage());
        const OrientationVerdict upsideDown = detectPageOrientation(rotateImage(syntheticTextPage(), 180));
        QCOMPARE(upsideDown.textLike, upright.textLike);
        QCOMPARE(upsideDown.axisKnown, upright.axisKnown);
        QCOMPARE(upsideDown.sideways, upright.sideways);
    }

    void aPageThePictureCannotReadSaysNothing()
    {
        // A single line: the rows and the columns vary the same way, so the
        // axis is not read and the page is left to the user.
        QImage page(900, 1200, QImage::Format_Grayscale8);
        page.fill(255);
        QPainter painter(&page);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0));
        painter.drawRect(QRect(80, 560, 740, 20));
        painter.end();
        const OrientationVerdict verdict = detectPageOrientation(page);
        QVERIFY(!verdict.textLike);
        QVERIFY(!verdict.axisKnown);
    }

    void aBlankPageSaysNothing()
    {
        const OrientationVerdict verdict = detectPageOrientation(blankPage());
        QVERIFY(!verdict.textLike);
        QVERIFY(!verdict.axisKnown);
    }

    void aPhotoSaysNothing()
    {
        // A photograph has ink everywhere: it carries no lines to read.
        QImage photo(800, 1000, QImage::Format_Grayscale8);
        for (int y = 0; y < photo.height(); ++y) {
            for (int x = 0; x < photo.width(); ++x)
                photo.setPixel(x, y, qGray((x * 3 + y * 5) % 256));
        }
        const OrientationVerdict verdict = detectPageOrientation(photo);
        QVERIFY(!verdict.textLike);
        QVERIFY(!verdict.axisKnown);
    }

    void aNullImageSaysNothing()
    {
        const OrientationVerdict verdict = detectPageOrientation(QImage());
        QVERIFY(!verdict.textLike);
        QVERIFY(!verdict.axisKnown);
    }

    // The check runs away from the caller's thread: the verdicts of one run
    // arrive only after its last page, the collections follow the page order
    // of the scope, and the signal order is the fixed one — skewed, sideways,
    // unclear, noText, finished.
    void theFindingsArriveInPageOrderInTheFixedSignalOrder()
    {
        OrientationController controller;
        QStringList order;
        QList<int> skewedPages;
        QList<int> sidewaysPages;
        QList<int> unclearPages;
        QList<int> noTextPages;
        int finishedCount = 0;
        connect(&controller, &OrientationController::skewed, [&](int index, double) {
            order << QStringLiteral("skewed");
            skewedPages.append(index);
        });
        connect(&controller, &OrientationController::sideways, [&](int index) {
            order << QStringLiteral("sideways");
            sidewaysPages.append(index);
        });
        connect(&controller, &OrientationController::unclear, [&](int index) {
            order << QStringLiteral("unclear");
            unclearPages.append(index);
        });
        connect(&controller, &OrientationController::noText, [&](int index) {
            order << QStringLiteral("noText");
            noTextPages.append(index);
        });
        connect(&controller, &OrientationController::finished, [&] { ++finishedCount; });

        // The scope is shuffled on purpose: 0 and 1 lie on their side, 2 is a
        // picture the check cannot read, 3 is blank, 4 is skewed.
        controller.check({2, 1, 4, 3, 0}, [](int index) {
            if (index == 0 || index == 1)
                return rotateImage(syntheticTextPage(), 90);
            if (index == 2)
                return QImage();
            if (index == 3)
                return blankPage();
            return rotateBy(syntheticTextPage(), 3.0);
        });

        QTRY_COMPARE(finishedCount, 1);
        QCOMPARE(order, (QStringList{QStringLiteral("skewed"), QStringLiteral("sideways"), QStringLiteral("sideways"), QStringLiteral("unclear"), QStringLiteral("noText")}));
        QCOMPARE(skewedPages, (QList<int>{4}));
        QCOMPARE(sidewaysPages, (QList<int>{1, 0}));
        QCOMPARE(unclearPages, (QList<int>{2}));
        QCOMPARE(noTextPages, (QList<int>{3}));
    }

    void anEmptyScopeFinishesWithNothingElse()
    {
        OrientationController controller;
        QStringList order;
        connect(&controller, &OrientationController::skewed, [&](int, double) { order << QStringLiteral("skewed"); });
        connect(&controller, &OrientationController::sideways, [&](int) { order << QStringLiteral("sideways"); });
        connect(&controller, &OrientationController::unclear, [&](int) { order << QStringLiteral("unclear"); });
        connect(&controller, &OrientationController::noText, [&](int) { order << QStringLiteral("noText"); });
        connect(&controller, &OrientationController::finished, [&] { order << QStringLiteral("finished"); });

        controller.check({}, [](int) { return QImage(); });

        QTRY_COMPARE(order.size(), 1);
        QCOMPARE(order, (QStringList{QStringLiteral("finished")}));
    }

    // A check started while another is still running replaces it: the run in
    // flight reports nothing at all, neither its findings nor its completion.
    void aSecondCheckSupersedesTheRunningOne()
    {
        OrientationController controller;
        QList<int> sidewaysPages;
        int finishedCount = 0;
        connect(&controller, &OrientationController::sideways, [&](int index) { sidewaysPages.append(index); });
        connect(&controller, &OrientationController::finished, [&] { ++finishedCount; });

        controller.check({0}, [](int) { return rotateImage(syntheticTextPage(), 90); });
        controller.check({0}, [](int) { return syntheticTextPage(); });

        QTRY_COMPARE(finishedCount, 1);
        QVERIFY(sidewaysPages.isEmpty());
        QCOMPARE(finishedCount, 1);
    }

    // The corpus of the project: ten real pages (equations, tables and prose)
    // and four turns of each. The axis has to be read the right way round
    // wherever the picture reads it at all, and the picture must never claim
    // which way is up — that is where the pages that lie the right way up were
    // being turned over.
    void theCorpusPagesKeepTheirAxis()
    {
        const QString dir = QStringLiteral(LLOCR_CORPUS_RENDERS);
        if (!QDir(dir).exists())
            QSKIP("this checkout carries no corpus");
        const QStringList files = QDir(dir).entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
        QVERIFY2(!files.isEmpty(), "the corpus carries no renders");

        int read = 0;
        int total = 0;
        QList<QString> wrong;
        for (const QString &file : files) {
            const QImage page(dir + QLatin1Char('/') + file);
            QVERIFY2(!page.isNull(), qPrintable(file));
            for (int turn = 0; turn < 4; ++turn) {
                const OrientationVerdict verdict = detectPageOrientation(rotateImage(page, turn * 90));
                ++total;
                if (!verdict.axisKnown)
                    continue;
                ++read;
                // The text runs across the page when the page lies straight
                // and up and down when it lies on its side.
                const bool sideways = turn == 1 || turn == 3;
                if (verdict.sideways != sideways)
                    wrong.append(QStringLiteral("%1 turned %2° (confidence %3)").arg(file).arg(turn * 90).arg(verdict.confidence));
            }
        }
        // A page of tables reads both ways and stays undecided; the rest of
        // the corpus is read. Every page that is read at all is read the
        // right way round: a wrong turn is costlier than no answer.
        QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("the axis was read the wrong way round: %1").arg(wrong.join(QStringLiteral("; ")))));
        QVERIFY2(read * 10 >= total * 8, qPrintable(QStringLiteral("the axis was read in %1 of %2 cases").arg(read).arg(total)));
    }
};

QTEST_MAIN(TestPageOrientation)
#include "test_page_orientation.moc"
