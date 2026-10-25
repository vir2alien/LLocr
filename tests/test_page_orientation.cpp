#include <QtTest>

#include <QImage>
#include <QPainter>

#include "app/PageOrientation.h"
#include "app/PageRotation.h"
#include "textpage.h"

using namespace llocr;

// The orientation check: the text's direction gives the axis, and the shape of
// the lines says which way is up.
class TestPageOrientation : public QObject
{
    Q_OBJECT

private slots:
    void anUprightPageReadsZero()
    {
        const OrientationVerdict verdict = detectPageOrientation(syntheticTextPage());
        QVERIFY(verdict.textLike);
        QVERIFY2(verdict.decided, "the check did not read an upright page");
        QCOMPARE(verdict.degrees, 0);
    }

    void anUpsideDownPageReadsOneEighty()
    {
        const QImage page = rotateImage(syntheticTextPage(), 180);
        const OrientationVerdict verdict = detectPageOrientation(page);
        QVERIFY(verdict.textLike);
        QVERIFY2(verdict.decided, "the check did not read an upside-down page");
        QCOMPARE(verdict.degrees, 180);
    }

    void aPageTurnedClockwiseReadsTwoSeventy()
    {
        // The page was turned a quarter turn clockwise: bringing it back takes
        // three quarter turns the same way.
        const QImage page = rotateImage(syntheticTextPage(), 90);
        const OrientationVerdict verdict = detectPageOrientation(page);
        QVERIFY(verdict.textLike);
        QVERIFY2(verdict.decided, "the check did not read a page on its side");
        QCOMPARE(verdict.degrees, 270);
    }

    void theOtherSideReadsNinety()
    {
        const QImage page = rotateImage(syntheticTextPage(), 270);
        const OrientationVerdict verdict = detectPageOrientation(page);
        QVERIFY(verdict.textLike);
        QVERIFY2(verdict.decided, "the check did not read the other side");
        QCOMPARE(verdict.degrees, 90);
    }

    void theCheckIsSharpEnoughToAct()
    {
        // The reading has to be clear enough that the page is turned on it:
        // the gap between the ascenders and the descenders is the whole
        // margin of the decision.
        const OrientationVerdict verdict = detectPageOrientation(syntheticTextPage());
        QVERIFY(verdict.decided);
        QVERIFY2(verdict.confidence >= 0.2, qPrintable(QStringLiteral("confidence %1").arg(verdict.confidence)));
    }

    void aBlankPageSaysNothing()
    {
        QImage blank(800, 1000, QImage::Format_Grayscale8);
        blank.fill(255);
        const OrientationVerdict verdict = detectPageOrientation(blank);
        QVERIFY(!verdict.textLike);
        QVERIFY(!verdict.decided);
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
        QVERIFY(!verdict.decided);
    }

    void aSingleLineSaysNothing()
    {
        QImage page(900, 1200, QImage::Format_Grayscale8);
        page.fill(255);
        QPainter painter(&page);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0));
        painter.drawRect(QRect(80, 560, 740, 20));
        painter.end();
        const OrientationVerdict verdict = detectPageOrientation(page);
        QVERIFY(!verdict.decided);
    }

    void aNullImageSaysNothing()
    {
        const OrientationVerdict verdict = detectPageOrientation(QImage());
        QVERIFY(!verdict.textLike);
        QVERIFY(!verdict.decided);
    }
};

QTEST_MAIN(TestPageOrientation)
#include "test_page_orientation.moc"
