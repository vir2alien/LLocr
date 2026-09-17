#include <QtTest>

#include <QDir>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <limits>

#include "app/DjVuDocument.h"
#include "app/DocumentModel.h"

using namespace llocr;

namespace {
QString fixture(const char* name)
{
    return QFINDTESTDATA(QStringLiteral("fixtures/djvu/") + QString::fromLatin1(name));
}

// Sample well inside each quadrant, away from compression/scale boundaries.
void compareQuadrants(const QImage& image, const QList<QColor>& expected)
{
    QVERIFY(!image.isNull());
    QCOMPARE(expected.size(), 4);
    for (int i = 0; i < 4; ++i) {
        const QColor actual = image.pixelColor(image.width() * (i % 2 ? 3 : 1) / 4,
                                              image.height() * (i / 2 ? 3 : 1) / 4);
        const QColor color = expected[i];
        QVERIFY2(qAbs(actual.red() - color.red()) <= 12
                     && qAbs(actual.green() - color.green()) <= 12
                     && qAbs(actual.blue() - color.blue()) <= 12,
                 qPrintable(QStringLiteral("Quadrant %1: expected %2, got %3")
                                .arg(i).arg(color.name(), actual.name())));
    }
}

QList<QColor> colors(int page)
{
    if (page == 1)
        return {Qt::green, Qt::white, Qt::red, Qt::blue}; // Native 90-degree CCW rotation.
    if (page == 2)
        return {Qt::cyan, Qt::magenta, Qt::yellow, Qt::black};
    return {Qt::red, Qt::green, Qt::blue, Qt::white};
}

QSize nativeSize(int page)
{
    if (page == 1)
        return {57, 81};
    if (page == 2)
        return {63, 45};
    return {81, 57};
}
}

class TestDjVuDocument : public QObject {
    Q_OBJECT

private slots:
    void singlePageColorAndRowOrder()
    {
        DjVuDocument document;
        QCOMPARE(document.pageCount(), 0);
        QString error = QStringLiteral("stale error");
        QVERIFY2(document.open(fixture("quadrants.djvu"), &error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(document.pageCount(), 1);
        QCOMPARE(document.pageSize(0, &error), QSize(81, 57));
        QVERIFY(error.isEmpty());
        const QImage image = document.render(0, QSize(81, 57), &error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(image.size(), QSize(81, 57));
        QCOMPARE(image.format(), QImage::Format_RGB888);
        // Odd width exercises QImage row padding as well as RGB vs BGR ordering.
        QVERIFY(image.bytesPerLine() > image.width() * 3);
        compareQuadrants(image, colors(0));
        const QImage scaled = document.render(0, QSize(37, 25), &error);
        QCOMPARE(scaled.size(), QSize(37, 25));
        compareQuadrants(scaled, colors(0));
    }

    void multipageOrderAndNativeRotation()
    {
        DjVuDocument document;
        QString error;
        QVERIFY2(document.open(fixture("multipage.djvu"), &error), qPrintable(error));
        QCOMPARE(document.pageCount(), 3);
        for (int page : {2, 0, 1, 0}) {
            QCOMPARE(document.pageSize(page, &error), nativeSize(page));
            QVERIFY(error.isEmpty());
            const QImage image = document.render(page, nativeSize(page), &error);
            QVERIFY2(!image.isNull(), qPrintable(error));
            QCOMPARE(image.size(), nativeSize(page));
            compareQuadrants(image, colors(page));
        }
        QVERIFY(document.open(fixture("rotated.djvu"), &error));
        QCOMPARE(document.pageCount(), 1);
        QCOMPARE(document.pageSize(0), nativeSize(1));
        compareQuadrants(document.render(0, nativeSize(1)), colors(1));
    }

    void unicodePath()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString folder = dir.filePath(QString::fromUtf8("\xd0\xa1\xd0\xba\xd0\xb0\xd0\xbd\xd1\x8b \xe6\x96\x87\xe6\xa1\xa3"));
        QVERIFY(QDir().mkpath(folder));
        const QString path = folder + QString::fromUtf8("/\xd1\x81\xd1\x82\xd1\x80\xd0\xb0\xd0\xbd\xd0\xb8\xd1\x86\xd0\xb0 \xe9\xa1\xb5.DjVu");
        QVERIFY(QFile::copy(fixture("quadrants.djvu"), path));
        DjVuDocument document;
        QString error;
        QVERIFY2(document.open(path, &error), qPrintable(error));
        QCOMPARE(document.pageSize(0), nativeSize(0));
        compareQuadrants(document.render(0, nativeSize(0)), colors(0));
        DocumentModel model;
        QVERIFY2(model.appendFile(path, &error), qPrintable(error));
        QCOMPARE(model.page(0).sourcePath, path);
        QCOMPARE(model.page(0).sourceType, DocumentSource::DjVu);
        compareQuadrants(model.fullImage(0, &error), colors(0));
    }

    void missingMalformedAndReopen()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString malformed = dir.filePath(QStringLiteral("malformed.djvu"));
        QFile file(malformed);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("Not a DjVu document\n"), qint64(20));
        file.close();
        const QString empty = dir.filePath(QStringLiteral("empty.djvu"));
        QFile emptyFile(empty);
        QVERIFY(emptyFile.open(QIODevice::WriteOnly));
        emptyFile.close();
        DjVuDocument document;
        QString error;
        for (const QString& path : {malformed, empty, dir.filePath(QStringLiteral("missing.djvu")), dir.path()}) {
            QVERIFY(document.open(fixture("multipage.djvu")));
            QVERIFY(!document.open(path, &error));
            QVERIFY(!error.isEmpty());
            QVERIFY(error.contains(path));
            QCOMPARE(document.pageCount(), 0);
            QVERIFY(document.pageSize(0, &error).isEmpty());
            QVERIFY(!error.isEmpty());
            QVERIFY(document.render(0, QSize(10, 10), &error).isNull());
            QVERIFY(!error.isEmpty());
            QVERIFY(!document.open(path)); // Optional error output.
        }
        QVERIFY(document.open(fixture("quadrants.djvu"), &error));
        QVERIFY(error.isEmpty());
        compareQuadrants(document.render(0, nativeSize(0)), colors(0));
    }

    void invalidIndices()
    {
        DjVuDocument document;
        QString error;
        QVERIFY(document.pageSize(0, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(document.render(0, QSize(10, 10), &error).isNull());
        QVERIFY(!error.isEmpty());
        QVERIFY(document.open(fixture("multipage.djvu")));
        for (int index : {-1, 3, std::numeric_limits<int>::max() - 1}) {
            error.clear();
            QVERIFY(document.pageSize(index, &error).isEmpty());
            QVERIFY(!error.isEmpty());
            error.clear();
            QVERIFY(document.render(index, QSize(10, 10), &error).isNull());
            QVERIFY(!error.isEmpty());
            QVERIFY(document.render(index, QSize(10, 10)).isNull());
        }
        QCOMPARE(document.pageSize(0, &error), nativeSize(0));
        QVERIFY(error.isEmpty());
    }

    void invalidRenderSizes_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::newRow("default") << QSize();
        QTest::newRow("zero-width") << QSize(0, 20);
        QTest::newRow("zero-height") << QSize(20, 0);
        QTest::newRow("negative") << QSize(-1, 20);
        QTest::newRow("wide") << QSize(16385, 1);
        QTest::newRow("tall") << QSize(1, 16385);
        QTest::newRow("over-40mp") << QSize(8000, 5001);
        QTest::newRow("integer-overflow") << QSize(std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
    }

    void invalidRenderSizes()
    {
        QFETCH(QSize, size);
        DjVuDocument document;
        QVERIFY(document.open(fixture("quadrants.djvu")));
        QString error;
        QVERIFY(document.render(0, size, &error).isNull());
        QVERIFY(!error.isEmpty());
        QVERIFY(document.render(0, size).isNull());
        QVERIFY(!document.render(0, QSize(1, 1), &error).isNull());
        QVERIFY(error.isEmpty());
    }

    void boundedNativeSize_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QSize>("source");
        QTest::newRow("side-width") << QStringLiteral("wide-info.djvu") << QSize(24000, 1200);
        QTest::newRow("side-height") << QStringLiteral("tall-info.djvu") << QSize(1200, 24000);
        QTest::newRow("pixel-count") << QStringLiteral("large-info.djvu") << QSize(10000, 8000);
    }

    void boundedNativeSize()
    {
        QFETCH(QString, name);
        QFETCH(QSize, source);
        DjVuDocument document;
        QString error;
        QVERIFY2(document.open(fixture(qPrintable(name)), &error), qPrintable(error));
        const QSize size = document.pageSize(0, &error);
        QVERIFY2(!size.isEmpty(), qPrintable(error));
        QVERIFY(error.isEmpty());
        QVERIFY(size.width() <= 16384);
        QVERIFY(size.height() <= 16384);
        const qint64 area = qint64(size.width()) * size.height();
        QVERIFY(area <= 40000000);
        QVERIFY(qAbs(double(size.width()) / size.height() - double(source.width()) / source.height()) < 0.03);
        if (source.width() > 16384 || source.height() > 16384)
            QCOMPARE(qMax(size.width(), size.height()), 16384);
        else
            QVERIFY(area > 39900000); // Bound, do not fall back to thumbnail/PDF DPI resolution.
    }

    void damagedLaterPage()
    {
        DjVuDocument document;
        QString error;
        QVERIFY2(document.open(fixture("bad-second-page.djvu"), &error), qPrintable(error));
        QCOMPARE(document.pageCount(), 3);
        compareQuadrants(document.render(0, nativeSize(0), &error), colors(0));
        QVERIFY(document.pageSize(1, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void modelLazyFullCache()
    {
        DocumentPage defaults;
        QCOMPARE(defaults.sourceType, DocumentSource::Image);
        QCOMPARE(defaults.sourcePageIndex, -1);
        DocumentModel model;
        const QString path = fixture("multipage.djvu");
        QString error;
        QVERIFY2(model.appendDjVu(path, &error), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(model.pageCount(), 3);
        for (int i = 0; i < 3; ++i) {
            const auto& page = model.page(i);
            QCOMPARE(page.sourceType, DocumentSource::DjVu);
            QCOMPARE(page.sourcePageIndex, i);
            QCOMPARE(page.sourcePath, path);
            QCOMPARE(page.pixelSize, nativeSize(i));
            QVERIFY(page.image.isNull());
            QVERIFY(!page.recognized);
            QVERIFY(!page.thumb.isNull());
            QVERIFY(page.thumb.width() <= 220);
            QVERIFY(page.thumb.height() <= 300);
            compareQuadrants(model.thumbnail(i), colors(i));
        }
        const QImage first = model.fullImage(0, &error);
        QCOMPARE(first.size(), nativeSize(0));
        compareQuadrants(first, colors(0));
        const qint64 key = first.cacheKey();
        QCOMPARE(model.fullImage(0, &error).cacheKey(), key);
        QVERIFY(error.isEmpty());
        QVERIFY(model.page(1).image.isNull());
        QVERIFY(model.page(2).image.isNull());

        // Exceed the existing four-full-image cache without needing a large fixture.
        QVERIFY(model.appendDjVu(path));
        for (int i = 1; i <= 4; ++i)
            compareQuadrants(model.fullImage(i, &error), colors(i % 3));
        QVERIFY(model.page(0).image.isNull());
        QVERIFY(!model.thumbnail(0).isNull());
        QVERIFY(model.page(5).image.isNull());
        compareQuadrants(model.fullImage(0, &error), colors(0));
        QVERIFY(error.isEmpty());
        QVERIFY(model.fullImage(-1, &error).isNull());
        QVERIFY(!error.isEmpty());
        QVERIFY(model.fullImage(model.pageCount(), &error).isNull());
        QVERIFY(!error.isEmpty());
    }

    void modelReorderRemoveAndClear()
    {
        DocumentModel model;
        QVERIFY(model.appendDjVu(fixture("multipage.djvu")));
        compareQuadrants(model.fullImage(1), colors(1));
        model.page(1).recognized = true;
        QVERIFY(model.movePage(2, 0));
        for (int i = 0; i < 3; ++i) {
            const int source = (i + 2) % 3;
            QCOMPARE(model.page(i).sourcePageIndex, source);
            QCOMPARE(model.page(i).sourceType, DocumentSource::DjVu);
            compareQuadrants(model.fullImage(i), colors(source));
        }
        QVERIFY(model.page(2).recognized);
        QVERIFY(model.removePage(1));
        QCOMPARE(model.pageCount(), 2);
        QCOMPARE(model.page(1).sourcePageIndex, 1);
        compareQuadrants(model.fullImage(1), colors(1));
        QVERIFY(!model.removePage(-1));
        QVERIFY(!model.removePage(2));
        QVERIFY(!model.movePage(-1, 0));
        QVERIFY(!model.movePage(0, 2));
        QCOMPARE(model.pageCount(), 2);
        model.clear();
        QVERIFY(model.isEmpty());
        QVERIFY(model.thumbnail(0).isNull());
        QVERIFY(model.appendDjVu(fixture("quadrants.djvu")));
        QCOMPARE(model.pageCount(), 1);
        QCOMPARE(model.page(0).sourcePageIndex, 0);
        compareQuadrants(model.fullImage(0), colors(0));
    }

    void modelAppendFailureIsAtomic()
    {
        const QString broken = fixture("bad-second-page.djvu");
        // damagedLaterPage proves this fails after decoding a valid first page.
        QString error;
        DocumentModel model;
        QVERIFY(!model.appendDjVu(broken, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains(broken));
        QVERIFY(model.isEmpty());
        QVERIFY(model.appendDjVu(fixture("rotated.djvu")));
        model.page(0).recognized = true;
        const DocumentPage before = model.page(0);
        const QImage cached = model.fullImage(0);
        const qint64 thumbKey = model.thumbnail(0).cacheKey();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const QString& path : {broken, dir.filePath(QStringLiteral("missing.djvu"))}) {
            for (bool dispatch : {false, true}) {
                error.clear();
                QVERIFY(!(dispatch ? model.appendFile(path, &error) : model.appendDjVu(path, &error)));
                QVERIFY(!error.isEmpty());
                QVERIFY(error.contains(path));
                QCOMPARE(model.pageCount(), 1);
                QCOMPARE(model.page(0).sourcePath, before.sourcePath);
                QCOMPARE(model.page(0).sourceType, before.sourceType);
                QCOMPARE(model.page(0).sourcePageIndex, before.sourcePageIndex);
                QCOMPARE(model.page(0).pixelSize, before.pixelSize);
                QVERIFY(model.page(0).recognized);
                QCOMPARE(model.thumbnail(0).cacheKey(), thumbKey);
                QCOMPARE(model.fullImage(0).cacheKey(), cached.cacheKey());
            }
        }
        QVERIFY(model.appendDjVu(fixture("multipage.djvu"), &error));
        QVERIFY(error.isEmpty());
        QCOMPARE(model.pageCount(), 4);
        compareQuadrants(model.fullImage(3), colors(2));
    }

    void fileDispatch_data()
    {
        QTest::addColumn<QString>("suffix");
        for (const char* suffix : {"djvu", "djv", "DJVU", "DJV", "DjVu", "dJv"})
            QTest::newRow(suffix) << QString::fromLatin1(suffix);
    }

    void fileDispatch()
    {
        QFETCH(QString, suffix);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("scan.") + suffix);
        QVERIFY(QFile::copy(fixture("multipage.djvu"), path));
        DocumentModel model;
        QString error;
        QVERIFY2(model.appendFile(path, &error), qPrintable(error));
        QCOMPARE(model.pageCount(), 3);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(model.page(i).sourceType, DocumentSource::DjVu);
            QCOMPARE(model.page(i).sourcePageIndex, i);
            compareQuadrants(model.fullImage(i), colors(i));
        }
    }

    void mixedRasterPdfDjVu()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString raster = dir.filePath(QStringLiteral("raster.PNG"));
        QImage source(30, 20, QImage::Format_RGB32);
        source.fill(Qt::yellow);
        QVERIFY(source.save(raster, "PNG"));
        const QString pdf = dir.filePath(QStringLiteral("document.PdF"));
        {
            QPdfWriter writer(pdf);
            QPainter painter(&writer);
            QVERIFY(painter.isActive());
            painter.fillRect(0, 0, writer.width(), writer.height(), Qt::magenta);
            QVERIFY(writer.newPage());
            painter.fillRect(0, 0, writer.width(), writer.height(), Qt::cyan);
        }
        DocumentModel model;
        QString error;
        QVERIFY2(model.appendFile(raster, &error), qPrintable(error));
        QVERIFY2(model.appendFile(pdf, &error), qPrintable(error));
        QVERIFY2(model.appendFile(fixture("multipage.djvu"), &error), qPrintable(error));
        QCOMPARE(model.pageCount(), 6);
        QCOMPARE(model.page(0).sourceType, DocumentSource::Image);
        QCOMPARE(model.page(0).sourcePageIndex, -1);
        QCOMPARE(model.page(1).sourceType, DocumentSource::Pdf);
        QCOMPARE(model.page(1).sourcePageIndex, 0);
        QCOMPARE(model.page(2).sourceType, DocumentSource::Pdf);
        QCOMPARE(model.page(2).sourcePageIndex, 1);
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(model.page(i + 3).sourceType, DocumentSource::DjVu);
            QCOMPARE(model.page(i + 3).sourcePageIndex, i);
        }
        QCOMPARE(model.fullImage(0), source);
        compareQuadrants(model.fullImage(1), {Qt::magenta, Qt::magenta, Qt::magenta, Qt::magenta});
        compareQuadrants(model.fullImage(2), {Qt::cyan, Qt::cyan, Qt::cyan, Qt::cyan});
        compareQuadrants(model.fullImage(4), colors(1));
        QVERIFY(model.movePage(4, 0));
        QVERIFY(model.removePage(2)); // Remove PDF page 0, not a DjVu source page.
        QCOMPARE(model.pageCount(), 5);
        QCOMPARE(model.page(0).sourceType, DocumentSource::DjVu);
        QCOMPARE(model.page(0).sourcePageIndex, 1);
        compareQuadrants(model.fullImage(0), colors(1));
        QCOMPARE(model.fullImage(1), source);
        QCOMPARE(model.page(2).sourceType, DocumentSource::Pdf);
        QCOMPARE(model.page(2).sourcePageIndex, 1);
        compareQuadrants(model.fullImage(2), {Qt::cyan, Qt::cyan, Qt::cyan, Qt::cyan});
        compareQuadrants(model.fullImage(3), colors(0));
        compareQuadrants(model.fullImage(4), colors(2));
    }
};

QTEST_MAIN(TestDjVuDocument)
#include "test_djvu_document.moc"
