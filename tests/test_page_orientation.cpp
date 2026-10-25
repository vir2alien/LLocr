#include <QtTest>

#include <QDir>
#include <QImage>
#include <QPainter>

#include "app/PageOrientation.h"
#include "app/PageRotation.h"
#include "textpage.h"

using namespace llocr;

// The orientation check: the picture reads the axis of the text and names the
// turns the page may need. Which way is up is the model's business — the
// typographic signs of it carry the sign of the alphabet, and a picture that
// reads them turns the pages that lie the right way up.
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
        QCOMPARE(orientationCandidates(verdict), QList<int>({0, 180}));
    }

    void aPageOnItsSideRunsUpAndDown()
    {
        const QImage page = rotateImage(syntheticTextPage(), 90);
        const OrientationVerdict verdict = detectPageOrientation(page);
        QVERIFY(verdict.textLike);
        QVERIFY(verdict.axisKnown);
        QVERIFY(verdict.sideways);
        QCOMPARE(orientationCandidates(verdict), QList<int>({90, 270}));
    }

    void theOtherSideRunsUpAndDownToo()
    {
        const QImage page = rotateImage(syntheticTextPage(), 270);
        const OrientationVerdict verdict = detectPageOrientation(page);
        QVERIFY(verdict.textLike);
        QVERIFY(verdict.axisKnown);
        QVERIFY(verdict.sideways);
        QCOMPARE(orientationCandidates(verdict), QList<int>({90, 270}));
    }

    void theWayUpIsNotThePicturesBusiness()
    {
        // The picture tells an upside-down page from an upright one by
        // nothing: the sign of the typographic signs it would read carries the
        // sign of the alphabet (Russian has more descenders than ascenders,
        // English the other way round), so reading them turns the pages that
        // lie the right way up. The picture therefore says the same about both
        // — the model settles the way up.
        const OrientationVerdict upright = detectPageOrientation(syntheticTextPage());
        const OrientationVerdict upsideDown = detectPageOrientation(rotateImage(syntheticTextPage(), 180));
        QCOMPARE(upsideDown.textLike, upright.textLike);
        QCOMPARE(upsideDown.axisKnown, upright.axisKnown);
        QCOMPARE(upsideDown.sideways, upright.sideways);
        QCOMPARE(orientationCandidates(upsideDown), orientationCandidates(upright));
    }

    void aPageThePictureCannotReadKeepsAllTheTurns()
    {
        // A single line: the rows and the columns vary the same way, so the
        // axis is not read and the model may answer any of the four.
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
        QCOMPARE(orientationCandidates(verdict), QList<int>({0, 90, 180, 270}));
    }

    void aBlankPageSaysNothing()
    {
        QImage blank(800, 1000, QImage::Format_Grayscale8);
        blank.fill(255);
        const OrientationVerdict verdict = detectPageOrientation(blank);
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

    // The corpus of the project: ten real pages of a Russian book (equations,
    // tables and prose) and four turns of each. The axis has to be read the
    // right way round wherever the picture reads it at all, and the picture
    // must never claim which way is up — that is where the pages that lie the
    // right way up were being turned over.
    void theCorpusPagesKeepTheirAxis()
    {
        const QString dir = QStringLiteral(LLOCR_CORPUS_RENDERS);
        if (!QDir(dir).exists())
            QSKIP("this checkout carries no corpus");
        const QStringList files = QDir(dir).entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
        QVERIFY2(!files.isEmpty(), "the corpus carries no renders");

        int read = 0;
        int total = 0;
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
                QVERIFY2(verdict.sideways == sideways, qPrintable(QStringLiteral("%1 turned %2°: the axis was read the wrong way round").arg(file).arg(turn * 90)));
            }
        }
        // A page of tables reads both ways and stays undecided; the rest of
        // the corpus is read.
        QVERIFY2(read * 10 >= total * 8, qPrintable(QStringLiteral("the axis was read in %1 of %2 cases").arg(read).arg(total)));
    }
};

QTEST_MAIN(TestPageOrientation)
#include "test_page_orientation.moc"
