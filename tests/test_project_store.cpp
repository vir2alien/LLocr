#include <QtTest>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "app/ProjectStore.h"
#include "runtime/ArchiveExtractor.h"
#include "runtime/ZipWriter.h"

using namespace llocr;

namespace {

QByteArray pngBytes(int width, int height, QColor color)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QString writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();
    file.write(bytes);
    file.close();
    return path;
}

ProjectData sampleData(const QString &pngPath, const QString &pdfPath)
{
    ProjectData data;
    data.currentPage = 2;

    ProjectSource png;
    png.id = 0;
    png.originalName = QStringLiteral("scan page.png");
    png.typeName = QStringLiteral("image");
    png.sourcePath = pngPath;
    data.sources.append(png);

    ProjectSource pdf;
    pdf.id = 1;
    pdf.originalName = QStringLiteral("doc.pdf");
    pdf.typeName = QStringLiteral("pdf");
    pdf.sourcePath = pdfPath;
    data.sources.append(pdf);

    ProjectPageData imagePage;
    imagePage.sourceId = 0;
    imagePage.recognized = true;
    imagePage.edited = true;
    imagePage.hasDuplicates = true;
    imagePage.text = QStringLiteral("edited text");
    imagePage.baseline = QStringLiteral("recognized text");
    imagePage.parseNote = QStringLiteral("no layout tokens");
    BoundingBox box;
    box.text = QStringLiteral("block text");
    box.correctedText = QStringLiteral("corrected text");
    box.checkStatus = BoxCheckStatus::Fixed;
    box.duplicateSuspect = true;
    box.label = QStringLiteral("text");
    box.rect = QRectF(0.1, 0.2, 0.5, 0.25);
    imagePage.boxes.append(box);
    BoundingBox mismatched;
    mismatched.text = QStringLiteral("misread block");
    mismatched.checkStatus = BoxCheckStatus::Mismatch;
    mismatched.label = QStringLiteral("text");
    mismatched.rect = QRectF(0.6, 0.2, 0.3, 0.25);
    imagePage.boxes.append(mismatched);
    BoundingBox unpositioned;
    unpositioned.text = QStringLiteral("preamble");
    unpositioned.positioned = false;
    imagePage.boxes.append(unpositioned);
    data.pages.append(imagePage);

    ProjectPageData pdfPage;
    pdfPage.sourceId = 1;
    pdfPage.sourcePageIndex = 3;
    pdfPage.recognized = false;
    pdfPage.text = QString();
    data.pages.append(pdfPage);

    ProjectPageData imagePageAgain;
    imagePageAgain.sourceId = 0;
    imagePageAgain.recognized = false;
    data.pages.append(imagePageAgain);

    return data;
}

}  // namespace

class TestProjectStore : public QObject
{
    Q_OBJECT

private slots:
    void zipWriterRoundTripsThroughExtractor()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString zipPath = dir.filePath(QStringLiteral("a.llocr"));
        const QByteArray png = pngBytes(20, 10, Qt::red);
        const QByteArray payload(QStringLiteral("héllo wörld").toUtf8());

        QString error;
        {
            ZipWriter zip(zipPath, &error);
            QVERIFY(error.isEmpty());
            QVERIFY(zip.addEntry(QStringLiteral("sources/0_scan page.png"), png));
            QFile source(writeFile(dir.filePath(QStringLiteral("doc.pdf")), QByteArray("%PDF-nonsense")));
            QVERIFY(source.open(QIODevice::ReadOnly));
            QVERIFY(zip.addEntry(QStringLiteral("sources/1_doc.pdf"), source));
            QVERIFY(zip.addEntry(QStringLiteral("project.json"), QByteArray("{\"format\":\"llocr-project\"}")));
            QVERIFY(zip.finish(&error));
        }
        QVERIFY(QFile::exists(zipPath));

        const QString extractDir = dir.filePath(QStringLiteral("out"));
        QVERIFY(QDir().mkpath(extractDir));
        const ExtractResult extracted = ArchiveExtractor::extractZip(zipPath, extractDir);
        QVERIFY2(extracted.ok, qPrintable(extracted.error));
        QCOMPARE(extracted.fileCount, 3);

        QFile pngOut(QDir(extractDir).filePath(QStringLiteral("sources/0_scan page.png")));
        QVERIFY(pngOut.open(QIODevice::ReadOnly));
        QCOMPARE(pngOut.readAll(), png);
        QFile jsonOut(QDir(extractDir).filePath(QStringLiteral("project.json")));
        QVERIFY(jsonOut.open(QIODevice::ReadOnly));
        QCOMPARE(jsonOut.readAll(), QByteArray("{\"format\":\"llocr-project\"}"));
    }

    void zipWriterRejectsUnsafeEntryNames()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ZipWriter zip(dir.filePath(QStringLiteral("b.llocr")));
        QVERIFY(!zip.addEntry(QStringLiteral("../escape.png"), QByteArray("x")));
        QVERIFY(!zip.addEntry(QStringLiteral("/abs.png"), QByteArray("x")));
        QVERIFY(!zip.finish());
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("b.llocr"))));
    }

    void saveLoadRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pngPath = writeFile(dir.filePath(QStringLiteral("scan page.png")), pngBytes(31, 17, Qt::green));
        const QString pdfPath = writeFile(dir.filePath(QStringLiteral("doc.pdf")), QByteArray("%PDF-1.4 fake"));
        QVERIFY(!pngPath.isEmpty() && !pdfPath.isEmpty());

        const ProjectData saved = sampleData(pngPath, pdfPath);
        const QString projectPath = dir.filePath(QStringLiteral("project.llocr"));
        QString error;
        QVERIFY2(ProjectStore::save(projectPath, saved, &error), qPrintable(error));
        QVERIFY(QFile::exists(projectPath));
        QVERIFY(!QFile::exists(projectPath + QStringLiteral(".tmp")));

        const QString extractDir = dir.filePath(QStringLiteral("extract"));
        QVERIFY(QDir().mkpath(extractDir));
        const ProjectStore::LoadResult loaded = ProjectStore::load(projectPath, extractDir);
        QVERIFY2(loaded.error.isEmpty(), qPrintable(loaded.error));
        QVERIFY(loaded.warnings.isEmpty());

        QCOMPARE(loaded.data.currentPage, 2);
        QCOMPARE(loaded.data.sources.size(), 2);
        QCOMPARE(loaded.data.sources.at(0).originalName, QStringLiteral("scan page.png"));
        QCOMPARE(loaded.data.sources.at(0).typeName, QStringLiteral("image"));
        QVERIFY(QFileInfo::exists(loaded.data.sources.at(0).extractedPath));
        QCOMPARE(loaded.data.sources.at(1).typeName, QStringLiteral("pdf"));
        QVERIFY(QFileInfo::exists(loaded.data.sources.at(1).extractedPath));

        QCOMPARE(loaded.data.pages.size(), 3);
        const ProjectPageData &imagePage = loaded.data.pages.at(0);
        QVERIFY(imagePage.recognized);
        QVERIFY(imagePage.edited);
        QVERIFY(imagePage.hasDuplicates);
        QCOMPARE(imagePage.text, QStringLiteral("edited text"));
        QCOMPARE(imagePage.baseline, QStringLiteral("recognized text"));
        QCOMPARE(imagePage.parseNote, QStringLiteral("no layout tokens"));
        QCOMPARE(imagePage.boxes.size(), 3);
        QCOMPARE(imagePage.boxes.at(0).text, QStringLiteral("block text"));
        QCOMPARE(imagePage.boxes.at(0).correctedText, QStringLiteral("corrected text"));
        QCOMPARE(imagePage.boxes.at(0).checkStatus, BoxCheckStatus::Fixed);
        QCOMPARE(imagePage.boxes.at(0).label, QStringLiteral("text"));
        QCOMPARE(imagePage.boxes.at(0).rect, QRectF(0.1, 0.2, 0.5, 0.25));
        QVERIFY(imagePage.boxes.at(0).positioned);
        QVERIFY(imagePage.boxes.at(0).duplicateSuspect);
        // The decision-stage status survives the container round-trip.
        QCOMPARE(imagePage.boxes.at(1).text, QStringLiteral("misread block"));
        QCOMPARE(imagePage.boxes.at(1).checkStatus, BoxCheckStatus::Mismatch);
        QVERIFY(imagePage.boxes.at(2).text == QStringLiteral("preamble"));
        QVERIFY(!imagePage.boxes.at(2).positioned);
        QVERIFY(!imagePage.boxes.at(2).duplicateSuspect);

        const ProjectPageData &pdfPage = loaded.data.pages.at(1);
        QCOMPARE(pdfPage.sourceId, 1);
        QCOMPARE(pdfPage.sourcePageIndex, 3);
        QVERIFY(!pdfPage.recognized);
    }

    void embeddedSourceBytesSurviveTheRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pngPath = writeFile(dir.filePath(QStringLiteral("p.png")), pngBytes(9, 9, Qt::blue));
        ProjectData data;
        ProjectSource source;
        source.id = 0;
        source.originalName = QStringLiteral("p.png");
        source.typeName = QStringLiteral("image");
        source.sourcePath = pngPath;
        data.sources.append(source);
        ProjectPageData page;
        page.sourceId = 0;
        page.text = QStringLiteral("t");
        data.pages.append(page);

        const QString projectPath = dir.filePath(QStringLiteral("p.llocr"));
        QString error;
        QVERIFY(ProjectStore::save(projectPath, data, &error));

        const QString extractDir = dir.filePath(QStringLiteral("x"));
        QVERIFY(QDir().mkpath(extractDir));
        const ProjectStore::LoadResult loaded = ProjectStore::load(projectPath, extractDir);
        QVERIFY2(loaded.error.isEmpty(), qPrintable(loaded.error));
        QFile embedded(loaded.data.sources.at(0).extractedPath);
        QVERIFY(embedded.open(QIODevice::ReadOnly));
        QCOMPARE(embedded.readAll(), pngBytes(9, 9, Qt::blue));
    }

    void fallbackImageIsEmbeddedWhenTheSourceFileIsGone()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProjectData data;
        ProjectSource source;
        source.id = 0;
        source.originalName = QStringLiteral("gone.png");
        source.typeName = QStringLiteral("image");
        source.fallbackImage = QImage(12, 6, QImage::Format_RGB32);
        source.fallbackImage.fill(Qt::magenta);
        data.sources.append(source);
        ProjectPageData page;
        page.sourceId = 0;
        data.pages.append(page);

        const QString projectPath = dir.filePath(QStringLiteral("g.llocr"));
        QString error;
        QVERIFY2(ProjectStore::save(projectPath, data, &error), qPrintable(error));

        const QString extractDir = dir.filePath(QStringLiteral("x"));
        QVERIFY(QDir().mkpath(extractDir));
        const ProjectStore::LoadResult loaded = ProjectStore::load(projectPath, extractDir);
        QVERIFY2(loaded.error.isEmpty(), qPrintable(loaded.error));
        QFile embedded(loaded.data.sources.at(0).extractedPath);
        QVERIFY(embedded.open(QIODevice::ReadOnly));
        QVERIFY(embedded.readAll().startsWith(QByteArray("\x89PNG", 4)));
    }

    void saveFailsWhenASourceIsMissingWithoutAFallback()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ProjectData data;
        ProjectSource source;
        source.id = 0;
        source.originalName = QStringLiteral("lost.pdf");
        source.typeName = QStringLiteral("pdf");
        source.sourcePath = dir.filePath(QStringLiteral("lost.pdf"));  // never created
        data.sources.append(source);
        ProjectPageData page;
        page.sourceId = 0;
        data.pages.append(page);

        QString error;
        QVERIFY(!ProjectStore::save(dir.filePath(QStringLiteral("l.llocr")), data, &error));
        QVERIFY(error.contains(QStringLiteral("lost.pdf")));
    }

    void loadRejectsFilesThatAreNotProjects()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString extractDir = dir.filePath(QStringLiteral("x"));
        QVERIFY(QDir().mkpath(extractDir));

        const QString garbage = writeFile(dir.filePath(QStringLiteral("garbage.llocr")), QByteArray("not a zip"));
        const ProjectStore::LoadResult bad = ProjectStore::load(garbage, extractDir);
        QVERIFY(!bad.error.isEmpty());

        // A valid zip without a project manifest.
        const QString zipPath = dir.filePath(QStringLiteral("empty.llocr"));
        ZipWriter zip(zipPath);
        QVERIFY(zip.addEntry(QStringLiteral("sources/0_a.png"), pngBytes(4, 4, Qt::gray)));
        QVERIFY(zip.finish());
        const ProjectStore::LoadResult noManifest = ProjectStore::load(zipPath, extractDir);
        QVERIFY(!noManifest.error.isEmpty());
    }

    void loadRejectsUnknownFormatAndNewerVersions()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString extractDir = dir.filePath(QStringLiteral("x"));
        QVERIFY(QDir().mkpath(extractDir));

        const auto makeProject = [&](const QJsonObject &manifest) {
            const QString path = dir.filePath(QStringLiteral("m%1.llocr").arg(QFileInfo(QJsonDocument(manifest).toJson()).size()));
            ZipWriter w(path);
            w.addEntry(QStringLiteral("project.json"), QJsonDocument(manifest).toJson());
            w.finish();
            return path;
        };

        const ProjectStore::LoadResult wrongFormat = ProjectStore::load(makeProject(QJsonObject{{"format", "someone-elses-project"}, {"version", 1}}), extractDir);
        QVERIFY(!wrongFormat.error.isEmpty());

        const ProjectStore::LoadResult newer = ProjectStore::load(makeProject(QJsonObject{{"format", "llocr-project"}, {"version", ProjectStore::kSchemaVersion + 1}}), extractDir);
        QVERIFY(!newer.error.isEmpty());

        const ProjectStore::LoadResult current = ProjectStore::load(makeProject(QJsonObject{{"format", "llocr-project"}, {"version", ProjectStore::kSchemaVersion}}), extractDir);
        QVERIFY2(current.error.isEmpty(), qPrintable(current.error));
        QCOMPARE(current.data.pages.size(), 0);
    }
};

QTEST_MAIN(TestProjectStore)
#include "test_project_store.moc"
