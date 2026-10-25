#include <QtTest>

#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include "app/DocumentModel.h"
#include "app/PageRotation.h"

using namespace llocr;

namespace {

// A page with a mark in the corner, so a turn is visible without comparing
// every pixel.
QImage markedPage(int width, int height)
{
    QImage page(width, height, QImage::Format_ARGB32);
    page.fill(Qt::white);
    QPainter painter(&page);
    painter.fillRect(QRect(0, 0, qMax(1, width / 8), qMax(1, height / 8)), Qt::red);
    painter.end();
    return page;
}

}  // namespace

// The page's turn: the angles land on the quarter grid, the picture turns
// clockwise, the turn moves the pixels instead of resampling them, and the
// document renders the turned page.
class TestPageRotation : public QObject
{
    Q_OBJECT

private slots:
    void anglesLandOnTheQuarterGrid()
    {
        QCOMPARE(normalizeRotation(0), 0);
        QCOMPARE(normalizeRotation(90), 90);
        QCOMPARE(normalizeRotation(180), 180);
        QCOMPARE(normalizeRotation(270), 270);
        QCOMPARE(normalizeRotation(360), 0);
        QCOMPARE(normalizeRotation(-90), 270);
        QCOMPARE(normalizeRotation(450), 90);
        // A corrupted angle cannot skew the page: it snaps to the nearest turn.
        QCOMPARE(normalizeRotation(45), 90);
        QCOMPARE(normalizeRotation(-45), 270);
    }

    void theSizeFollowsTheTurn()
    {
        const QSize source(120, 80);
        QCOMPARE(rotatedSize(source, 0), source);
        QCOMPARE(rotatedSize(source, 180), source);
        QCOMPARE(rotatedSize(source, 90), QSize(80, 120));
        QCOMPARE(rotatedSize(source, 270), QSize(80, 120));
    }

    void theTurnIsClockwise()
    {
        const QImage page = markedPage(120, 80);
        const QImage turned = rotateImage(page, 90);
        QCOMPARE(turned.width(), 80);
        QCOMPARE(turned.height(), 120);
        // The mark in the top-left corner ends up in the top-right one.
        QCOMPARE(turned.pixelColor(turned.width() - 1, 0), QColor(Qt::red));
        QCOMPARE(turned.pixelColor(0, 0), QColor(Qt::white));

        const QImage flipped = rotateImage(page, 180);
        QCOMPARE(flipped.size(), page.size());
        QCOMPARE(flipped.pixelColor(flipped.width() - 1, flipped.height() - 1), QColor(Qt::red));

        const QImage counter = rotateImage(page, 270);
        QCOMPARE(counter.width(), 80);
        QCOMPARE(counter.height(), 120);
        QCOMPARE(counter.pixelColor(0, counter.height() - 1), QColor(Qt::red));
    }

    void theTurnMovesThePixels()
    {
        // Every pixel carries its own value: a resampling turn would blend
        // them, and the model's geometry against the page would drift.
        const int width = 37;
        const int height = 23;
        QImage page(width, height, QImage::Format_RGB32);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x)
                page.setPixel(x, y, qRgb(x * 7 % 256, y * 11 % 256, (x + y) * 3 % 256));
        }

        const QImage turned = rotateImage(page, 90);
        QCOMPARE(turned.width(), height);
        QCOMPARE(turned.height(), width);
        for (int y = 0; y < width; ++y) {
            for (int x = 0; x < height; ++x)
                QCOMPARE(turned.pixel(x, y), page.pixel(y, height - 1 - x));
        }
    }

    void fourTurnsComeBackToTheSource()
    {
        const QImage page = markedPage(60, 40);
        QImage turned = page;
        for (int i = 0; i < 4; ++i)
            turned = rotateImage(turned, 90);
        QCOMPARE(turned.size(), page.size());
        QCOMPARE(turned, page);
    }

    void theRenderedPageIsTurned()
    {
        QImage source(64, 48, QImage::Format_ARGB32);
        source.fill(Qt::red);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("page.png"));
        QVERIFY(source.save(path, "PNG"));

        DocumentModel model;
        QVERIFY(model.appendImage(path));
        QCOMPARE(model.page(0).rotation, 0);

        QString error;
        const QImage upright = model.fullImage(0, &error);
        QVERIFY2(!upright.isNull(), qPrintable(error));
        QCOMPARE(upright.size(), QSize(64, 48));

        QVERIFY(model.setRotation(0, 90));
        const QImage turned = model.fullImage(0, &error);
        QVERIFY2(!turned.isNull(), qPrintable(error));
        QCOMPARE(turned.size(), QSize(48, 64));
        QCOMPARE(model.page(0).displaySize(), QSize(48, 64));

        QVERIFY(model.setRotation(0, 180));
        QCOMPARE(model.fullImage(0, &error).size(), QSize(64, 48));
    }

    void theTurnDropsTheRenderedCopy()
    {
        QImage source(64, 48, QImage::Format_ARGB32);
        source.fill(Qt::red);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("page.png"));
        QVERIFY(source.save(path, "PNG"));

        DocumentModel model;
        QVERIFY(model.appendImage(path));
        QString error;
        QCOMPARE(model.fullImage(0, &error).size(), QSize(64, 48));

        // The rendered copy belongs to the old turn: it must not survive it.
        QVERIFY(model.setRotation(0, 90));
        QCOMPARE(model.fullImage(0, &error).size(), QSize(48, 64));
        QCOMPARE(model.page(0).rotation, 90);

        // The turn is an angle, not a step: the caller says where the page
        // stands.
        QVERIFY(model.setRotation(0, 0));
        QCOMPARE(model.page(0).rotation, 0);
        QCOMPARE(model.fullImage(0, &error).size(), QSize(64, 48));
    }

    void theThumbnailKeepsTheSourceAspect()
    {
        const QSize source(2480, 3508);
        const QSize upright = DocumentModel::thumbnailSizeFor(source, 0);
        QVERIFY(upright.width() < upright.height());

        // A turned page is rendered from the source, so the render target keeps
        // the source's aspect; the thumbnail as it is shown fits the box.
        const QSize turned = DocumentModel::thumbnailSizeFor(source, 90);
        QVERIFY(turned.width() < turned.height());
        const QSize shown = rotatedSize(turned, 90);
        QVERIFY(shown.width() <= 220);
        QVERIFY(shown.height() <= 300);
        // The source's aspect survives the round trip.
        QVERIFY(qAbs(qreal(turned.height()) / turned.width() - qreal(source.height()) / source.width()) < 0.01);
    }
};

QTEST_MAIN(TestPageRotation)
#include "test_page_rotation.moc"
