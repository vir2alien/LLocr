#include <QtTest>

#include <algorithm>
#include <QFile>
#include <QMarginsF>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QtConcurrent/QtConcurrentMap>
#include <QTemporaryDir>

#ifdef __APPLE__
#include <mach/mach.h>
#include <malloc/malloc.h>
#endif

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

#ifdef __APPLE__
// Resident bytes of this process — the same quantity the operator reads off
// `footprint`/`vmmap`, so the numbers below are comparable with the reports in
// docs/optimization-plan.
qint64 residentBytes()
{
    mach_task_basic_info info = {};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return -1;
    return qint64(info.resident_size);
}

void relieveAllocator()
{
    malloc_zone_pressure_relief(nullptr, 0);
}
#endif

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

    // Measurement, and the guard for the render-cache bounds.
    //
    // The Stage 3 report showed ~199 MB of MALLOC_LARGE (empty) after listing 30
    // pages — free memory the allocator kept — and neither removing the repeated
    // PDF re-parse nor shrinking the render target moved it. This harness
    // reproduces the access pattern (render 30 pages, keep the last two as the
    // preview cache does) and reports how far the resident set moves, across the
    // two factors that could plausibly explain it: a different target size per
    // render, and a buffer allocated on a worker but released on the GUI thread
    // (the preview worker fills the cache, the GUI thread evicts it).
    //
    // What it actually locks in is the property that does matter and that this
    // suite can see: 30 renders of a 17 MB page must not leave 30 pages behind.
    // The measured growth is a rounding error, so the bound is deliberately wide
    // and the per-variant numbers are printed rather than asserted.
    void renderFootprintFollowsTargetSize()
    {
#ifndef __APPLE__
        QSKIP("macOS-only measurement");
#else
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("book.pdf"));
        QVERIFY(writePdf(path, 3));

        DocumentModel model;
        QVERIFY(model.appendPdf(path));
        const QSize fullSize = model.page(0).pixelSize.scaled(2481, 3508, Qt::KeepAspectRatio);
        const QSize thumbSize = DocumentModel::thumbnailSizeFor(fullSize);

        const auto run = [&](const QSize &size, int &nullRenders) -> qint64 {
            QList<QImage> kept;
            // Warm up the decoder and the allocator, then hand the free memory
            // back so the baseline is a clean steady state.
            for (int i = 0; i < 5; ++i) {
                DocumentModel::RenderRequest request = model.renderRequestFor(i % model.pageCount());
                request.page.pixelSize = size;
                kept.prepend(DocumentModel::renderDetached(request));
            }
            kept.clear();
            relieveAllocator();
            const qint64 baseline = residentBytes();

            qint64 peak = baseline;
            for (int i = 0; i < 30; ++i) {
                DocumentModel::RenderRequest request = model.renderRequestFor(i % model.pageCount());
                request.page.pixelSize = size;
                const QImage image = DocumentModel::renderDetached(request);
                if (image.isNull() || image.size() != size) {
                    ++nullRenders;
                    continue;
                }
                kept.prepend(image);
                if (kept.size() > 2)
                    kept.removeLast();
                peak = std::max(peak, residentBytes());
            }
            return peak - baseline;
        };

        int nullRenders = 0;
        const qint64 fullGrowth = run(fullSize, nullRenders);
        QVERIFY2(nullRenders == 0, qPrintable(QStringLiteral("%1 full-size renders returned an unexpected image").arg(nullRenders)));
        const qint64 thumbGrowth = run(thumbSize, nullRenders);
        QVERIFY2(nullRenders == 0, qPrintable(QStringLiteral("%1 thumbnail renders returned an unexpected image").arg(nullRenders)));

        qInfo(
            "render target %dx%d (%lld KB): resident growth over 30 renders %lld KB", fullSize.width(), fullSize.height(), fullSize.width() * qint64(fullSize.height()) * 4 / 1024, fullGrowth / 1024);
        qInfo("render target %dx%d (%lld KB): resident growth over 30 renders %lld KB",
              thumbSize.width(),
              thumbSize.height(),
              thumbSize.width() * qint64(thumbSize.height()) * 4 / 1024,
              thumbGrowth / 1024);

        // The run above reuses one target size, which the allocator reuses
        // perfectly. The app renders *different* pages, so the two remaining
        // suspects are a different size per render and a buffer allocated on a
        // worker but released on the GUI thread (the preview worker fills the
        // cache, the GUI thread evicts it).
        const auto sizeForIndex = [&](int i) { return fullSize.scaled(2400 + (i % 17) * 5, 3508, Qt::KeepAspectRatio); };

        const auto runVaried = [&](const QSize &base, bool offThread) -> qint64 {
            QList<QImage> kept;
            const auto renderOne = [&](int i) {
                DocumentModel::RenderRequest request = model.renderRequestFor(i % model.pageCount());
                request.page.pixelSize = sizeForIndex(i).scaled(base, Qt::KeepAspectRatio);
                return DocumentModel::renderDetached(request);
            };
            for (int i = 0; i < 5; ++i)
                kept.prepend(renderOne(i));
            kept.clear();
            relieveAllocator();
            const qint64 baseline = residentBytes();
            qint64 peak = baseline;

            for (int i = 0; i < 30; ++i) {
                QImage image;
                if (offThread) {
                    // Allocated on a worker, released below on the GUI thread,
                    // exactly like AppController::previewImage.
                    QtConcurrent::blockingMap(QList<int>{i}, [&renderOne, &image](int index) { image = renderOne(index); });
                } else {
                    image = renderOne(i);
                }
                if (image.isNull())
                    continue;
                kept.prepend(image);
                if (kept.size() > 2)
                    kept.removeLast();
                peak = std::max(peak, residentBytes());
            }
            return peak - baseline;
        };

        const qint64 variedOnThread = runVaried(fullSize, false);
        const qint64 variedOffThread = runVaried(fullSize, true);
        const qint64 thumbVaried = runVaried(thumbSize, true);
        qInfo("varied sizes, GUI thread:        %lld KB", variedOnThread / 1024);
        qInfo("varied sizes, worker -> GUI:     %lld KB", variedOffThread / 1024);
        qInfo("varied sizes, worker, thumb size: %lld KB", thumbVaried / 1024);

        const qint64 worst = std::max({fullGrowth, variedOnThread, variedOffThread});
        qInfo("worst resident growth across variants: %lld KB for %lld KB of render traffic", worst / 1024, 30ll * fullSize.width() * fullSize.height() * 4 / 1024);
        QVERIFY2(worst < 32ll * 1024 * 1024, "resident memory grows with the number of renders: a render cache is no longer releasing what it drops");
#endif
    }
};

QTEST_MAIN(TestDocumentModel)
#include "test_document_model.moc"
