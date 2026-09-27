#include <QtTest>

#include <QFile>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QtConcurrent/QtConcurrentMap>
#include <QTemporaryDir>

#include "app/DocumentModel.h"

using namespace llocr;

namespace {

QSize pdfPoints(int page)
{
    return page == 1 ? QSize(57, 81) : QSize(81, 57);
}

QList<QColor> pdfColors(int page)
{
    return page == 1 ? QList<QColor>{Qt::red, Qt::green, Qt::blue, Qt::white} : QList<QColor>{Qt::white, Qt::blue, Qt::green, Qt::red};
}

bool writePdf(const QString &path, int pages)
{
    const auto layoutFor = [](int page) { return QPageLayout(QPageSize(QSizeF(pdfPoints(page)), QPageSize::Point), QPageLayout::Portrait, QMarginsF(0, 0, 0, 0), QPageLayout::Point); };
    QPdfWriter writer(path);
    writer.setResolution(72);
    writer.setPageLayout(layoutFor(0));
    QPainter painter;
    if (!painter.begin(&writer))
        return false;
    for (int page = 0; page < pages; ++page) {
        if (page > 0) {
            writer.setPageLayout(layoutFor(page));
            writer.newPage();
        }
        const QSizeF points(pdfPoints(page));
        painter.fillRect(QRectF(0, 0, points.width(), points.height()), Qt::white);
        const QList<QColor> pageColors = pdfColors(page);
        for (int i = 0; i < 4; ++i)
            painter.fillRect(QRectF((i % 2) * points.width() / 2, (i / 2) * points.height() / 2, points.width() / 2, points.height() / 2), pageColors.at(i));
    }
    painter.end();
    return true;
}

}  // namespace

// DocumentModel::fullImage() decodes the source file lazily; the page handed
// to the decoder must carry the original sourcePath (regression: a fresh
// DocumentPage with an empty path reached the decoder, so every full decode
// failed with "file not found" while thumbnails worked).
class TestDocumentModel : public QObject
{
    Q_OBJECT

private slots:
    void fullImageDecodesRasterPage()
    {
        QImage source(64, 48, QImage::Format_ARGB32);
        source.fill(Qt::red);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("page.png"));
        QVERIFY(source.save(path, "PNG"));

        DocumentModel model;
        QVERIFY(model.loadImage(path));

        QString error;
        const QImage image = model.fullImage(0, &error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QCOMPARE(image.width(), 64);
        QCOMPARE(image.height(), 48);
        QVERIFY(error.isEmpty());
    }

    void fullImageReportsMissingFile()
    {
        QImage valid(10, 10, QImage::Format_ARGB32);
        valid.fill(Qt::blue);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("exists.png"));
        QVERIFY(valid.save(path, "PNG"));

        DocumentModel model;
        QVERIFY(model.loadImage(path));
        QVERIFY(QFile::remove(path));

        QString error;
        const QImage image = model.fullImage(0, &error);
        QVERIFY(image.isNull());
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains(path));
    }

    void fullImageReusesDecodedPage()
    {
        QImage source(32, 32, QImage::Format_ARGB32);
        source.fill(Qt::green);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("page.png"));
        QVERIFY(source.save(path, "PNG"));

        DocumentModel model;
        QVERIFY(model.loadImage(path));
        QVERIFY(!model.fullImage(0).isNull());
        QVERIFY(QFile::remove(path));

        QString error;
        QVERIFY(!model.fullImage(0, &error).isNull());
        QVERIFY(error.isEmpty());
    }

    // Every PDF render used to re-parse the file, because renderDetached()
    // worked from a private QPdfDocument. The imported handle is now carried in
    // the render request, which is only useful if it is genuinely shared.
    void pdfHandleIsSharedAcrossRenders()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("shared.pdf"));
        QVERIFY(writePdf(path, 3));

        DocumentModel model;
        QVERIFY(model.appendPdf(path));
        QCOMPARE(model.pageCount(), 3);

        const DocumentModel::RenderRequest first = model.renderRequestFor(1);
        const DocumentModel::RenderRequest second = model.renderRequestFor(1);
        QVERIFY(first.pdf);
        QCOMPARE(first.pdf.get(), second.pdf.get());
        QCOMPARE(first.pdf.get(), model.renderRequestFor(2).pdf.get());

        const DocumentModel::RenderRequest thumb = model.thumbnailRequestFor(1);
        QVERIFY(thumb.pdf);
        QCOMPARE(thumb.pdf.get(), first.pdf.get());
        QCOMPARE(thumb.page.pixelSize, DocumentModel::thumbnailSizeFor(model.page(1).pixelSize));

        QString error;
        const QImage full = DocumentModel::renderDetached(first, &error);
        QVERIFY2(!full.isNull(), qPrintable(error));
        QCOMPARE(full.size(), model.page(1).pixelSize);
        const QImage small = DocumentModel::renderDetached(thumb, &error);
        QVERIFY2(!small.isNull(), qPrintable(error));
        QCOMPARE(small.size(), thumb.page.pixelSize);
        QVERIFY(small.size().width() <= 220);

        // A request without a handle still renders: the model may have evicted
        // the file, and hand-built requests must keep working.
        DocumentModel::RenderRequest detached = first;
        detached.pdf.reset();
        QVERIFY(!DocumentModel::renderDetached(detached, &error).isNull());
        QVERIFY(error.isEmpty());

        model.clear();
        QVERIFY(!model.renderRequestFor(0).pdf);
    }

    void pdfHandleRendersConcurrently()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("shared.pdf"));
        QVERIFY(writePdf(path, 3));

        DocumentModel model;
        QVERIFY(model.appendPdf(path));
        QList<DocumentModel::RenderRequest> requests{model.renderRequestFor(0), model.renderRequestFor(1), model.renderRequestFor(2)};

        // Pre-sized, and each worker writes its own slot: appending from
        // several threads would race on the container, not on the document.
        QList<int> indices{0, 1, 2};
        QList<QImage> results(requests.size());
        QtConcurrent::blockingMap(indices, [&results, &requests](int index) { results[index] = DocumentModel::renderDetached(requests.at(index)); });

        for (int i = 0; i < results.size(); ++i) {
            QVERIFY(!results.at(i).isNull());
            QCOMPARE(results.at(i).size(), model.page(i).pixelSize);
        }
    }
};

QTEST_MAIN(TestDocumentModel)
#include "test_document_model.moc"
