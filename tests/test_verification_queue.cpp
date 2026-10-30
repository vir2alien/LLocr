#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

#include "app/DocumentModel.h"
#include "app/VerificationPromptStore.h"
#include "app/VerificationQueueController.h"
#include "config/RequestProfileStore.h"
#include "config/SettingsStore.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/RuntimeController.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// A block of the given type and text.
BoundingBox textBox(const QString &label, const QString &text)
{
    BoundingBox box;
    box.label = label;
    box.text = text;
    return box;
}

// The queue and the dependencies it reads. The runtime paths point inside the
// test's own directory, so the test never touches the user's profiles.
class QueueFixture
{
public:
    explicit QueueFixture(const QTemporaryDir &dir)
    {
        m_settings = std::make_unique<SettingsStore>();
        m_settings->setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        m_settings->setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        m_launchProfiles = std::make_unique<LaunchProfileStore>(*m_settings);
        m_checkProfiles = std::make_unique<RequestProfileStore>(*m_settings, ConnectionRole::BlockRecognition);
        m_fixProfiles = std::make_unique<RequestProfileStore>(*m_settings, ConnectionRole::Fix);
        m_runtime = std::make_unique<RuntimeController>(*m_settings, *m_launchProfiles);
        m_verification = std::make_unique<VerificationPromptStore>(*m_settings);
        m_document = std::make_unique<DocumentModel>();
        m_queue = std::make_unique<VerificationQueueController>(VerificationQueueController::Deps{
            *m_document, *m_verification, *m_checkProfiles, *m_fixProfiles, *m_settings, *m_runtime, [](int, int) { return QImage(); }, [] { return false; }, [](int, int) { return QString(); }, {}});
    }

    VerificationQueueController &queue() { return *m_queue; }
    VerificationPromptStore &verification() { return *m_verification; }
    DocumentModel &document() { return *m_document; }

private:
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<LaunchProfileStore> m_launchProfiles;
    std::unique_ptr<RequestProfileStore> m_checkProfiles;
    std::unique_ptr<RequestProfileStore> m_fixProfiles;
    std::unique_ptr<RuntimeController> m_runtime;
    std::unique_ptr<VerificationPromptStore> m_verification;
    std::unique_ptr<DocumentModel> m_document;
    std::unique_ptr<VerificationQueueController> m_queue;
};

}  // namespace

// The verification queue's work lists: what a stage of the auto pipeline
// collects for itself before the queue runs over it.
class TestVerificationQueue : public QObject
{
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;
    QTemporaryDir m_dir;

    // A one-page document carrying the given blocks.
    void makeDocument(DocumentModel &document, const QList<BoundingBox> &boxes)
    {
        QVERIFY(document.appendImage(m_dir.filePath(QStringLiteral("page.png"))));
        document.page(0).recognized = true;
        OcrPage page;
        page.boxes = boxes;
        document.page(0).result.pages = {page};
    }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_verification_queue"));

        QImage source(64, 48, QImage::Format_ARGB32);
        source.fill(Qt::white);
        QVERIFY(source.save(m_dir.filePath(QStringLiteral("page.png")), "PNG"));
    }

    void theBlocksWithoutTextAreTheRecognitionWork()
    {
        QueueFixture fixture(m_dir);
        makeDocument(fixture.document(), {textBox(QStringLiteral("text"), QString()), textBox(QStringLiteral("title"), QStringLiteral("already read"))});

        const auto tasks = fixture.queue().unrecognizedBlocks({0});
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.first().page, 0);
        QCOMPARE(tasks.first().box, 0);
    }

    void onlyTheEnabledTypesAreChecked()
    {
        QueueFixture fixture(m_dir);
        auto *model = qobject_cast<VerificationBlocksModel *>(fixture.verification().blockModel());
        QVERIFY(model);
        model->setEnabled(model->rowOfType(QStringLiteral("text")), false);

        makeDocument(fixture.document(), {textBox(QStringLiteral("text"), QStringLiteral("plain")), textBox(QStringLiteral("title"), QStringLiteral("heading"))});

        const auto tasks = fixture.queue().enabledBlocks({0});
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.first().box, 1);
    }

    void theFixWorkIsTheBlocksThatWaitForIt()
    {
        QueueFixture fixture(m_dir);
        BoundingBox mismatch = textBox(QStringLiteral("text"), QStringLiteral("wrong"));
        mismatch.checkStatus = BoxCheckStatus::Mismatch;
        BoundingBox review = textBox(QStringLiteral("text"), QStringLiteral("unreadable"));
        review.checkStatus = BoxCheckStatus::Review;
        BoundingBox ok = textBox(QStringLiteral("text"), QStringLiteral("right"));
        ok.checkStatus = BoxCheckStatus::Ok;

        makeDocument(fixture.document(), {mismatch, review, ok});

        const auto tasks = fixture.queue().fixableBlocks({0});
        QCOMPARE(tasks.size(), 2);
        QCOMPARE(tasks.at(0).box, 0);
        QCOMPARE(tasks.at(1).box, 1);
    }

    void theCorrectedLayerWithoutAVerdictIsTheSecondCheck()
    {
        QueueFixture fixture(m_dir);
        BoundingBox corrected = textBox(QStringLiteral("text"), QStringLiteral("ocr text"));
        corrected.correctedText = QStringLiteral("fixed text");
        BoundingBox refused = textBox(QStringLiteral("text"), QStringLiteral("ocr text"));
        refused.correctedText = QStringLiteral("damaged");
        refused.correctedCheckStatus = BoxCheckStatus::Review;

        makeDocument(fixture.document(), {corrected, refused});

        const auto tasks = fixture.queue().enabledBlocks({0});
        QCOMPARE(tasks.size(), 1);
        QCOMPARE(tasks.first().box, 0);
    }

    void theTypeFilterNarrowsTheCollection()
    {
        QueueFixture fixture(m_dir);
        BoundingBox textMismatch = textBox(QStringLiteral("text"), QStringLiteral("wrong"));
        textMismatch.checkStatus = BoxCheckStatus::Mismatch;
        BoundingBox titleMismatch = textBox(QStringLiteral("title"), QStringLiteral("wrong title"));
        titleMismatch.checkStatus = BoxCheckStatus::Mismatch;

        makeDocument(fixture.document(),
                     {textBox(QStringLiteral("text"), QString()),
                      textBox(QStringLiteral("title"), QString()),
                      textMismatch,
                      titleMismatch,
                      textBox(QStringLiteral("text"), QStringLiteral("plain")),
                      textBox(QStringLiteral("title"), QStringLiteral("heading"))});

        const VerificationQueueController::TypeFilter onlyTitle = [](const QString &type) { return type == QLatin1String("title"); };

        QCOMPARE(fixture.queue().unrecognizedBlocks({0}).size(), 2);
        QCOMPARE(fixture.queue().enabledBlocks({0}).size(), 2);
        QCOMPARE(fixture.queue().fixableBlocks({0}).size(), 2);

        const auto unrecognized = fixture.queue().unrecognizedBlocks({0}, onlyTitle);
        QCOMPARE(unrecognized.size(), 1);
        QCOMPARE(unrecognized.first().box, 1);

        const auto enabled = fixture.queue().enabledBlocks({0}, onlyTitle);
        QCOMPARE(enabled.size(), 1);
        QCOMPARE(enabled.first().box, 5);

        const auto fixable = fixture.queue().fixableBlocks({0}, onlyTitle);
        QCOMPARE(fixable.size(), 1);
        QCOMPARE(fixable.first().box, 3);
    }
};

QTEST_MAIN(TestVerificationQueue)
#include "test_verification_queue.moc"
