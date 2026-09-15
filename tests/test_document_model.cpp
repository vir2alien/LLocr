#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "app/DocumentModel.h"

using namespace llocr;

// DocumentModel::fullImage() decodes the source file lazily; the page handed
// to the decoder must carry the original sourcePath (regression: a fresh
// DocumentPage with an empty path was passed to decodeSource, so every full
// decode failed with "file not found" while thumbnails worked).
class TestDocumentModel : public QObject {
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
};

QTEST_MAIN(TestDocumentModel)
#include "test_document_model.moc"
