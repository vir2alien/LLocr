#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

#include "app/BlockWorkList.h"
#include "app/DocumentModel.h"
#include "app/FixController.h"
#include "app/VerificationPromptStore.h"
#include "config/RequestProfileStore.h"
#include "config/SettingsStore.h"
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

// The value the request carries for the named parameter, or -1 when it is absent.
int parameterValue(const QList<RequestParameter> &parameters, const QString &name)
{
    for (const RequestParameter &parameter : parameters) {
        if (parameter.name == name)
            return parameter.value.toInt();
    }
    return -1;
}

// The work list and the dependencies it reads. The runtime paths point
// inside the test's own directory, so the test never touches the user's
// profiles.
class QueueFixture
{
public:
    explicit QueueFixture(const QTemporaryDir &dir)
    {
        m_settings = std::make_unique<SettingsStore>();
        m_settings->setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        m_settings->setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        m_checkProfiles = std::make_unique<RequestProfileStore>(*m_settings, ConnectionRole::BlockRecognition);
        m_verification = std::make_unique<VerificationPromptStore>(*m_settings);
        m_document = std::make_unique<DocumentModel>();
        m_workList = std::make_unique<BlockWorkList>(BlockWorkList::Deps{*m_document, *m_verification, *m_checkProfiles});
    }

    BlockWorkList &workList() { return *m_workList; }
    VerificationPromptStore &verification() { return *m_verification; }
    DocumentModel &document() { return *m_document; }

private:
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<RequestProfileStore> m_checkProfiles;
    std::unique_ptr<VerificationPromptStore> m_verification;
    std::unique_ptr<DocumentModel> m_document;
    std::unique_ptr<BlockWorkList> m_workList;
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

        const auto tasks = fixture.workList().unrecognizedBlocks({0});
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

        const auto tasks = fixture.workList().enabledBlocks({0});
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

        const auto tasks = fixture.workList().fixableBlocks({0});
        QCOMPARE(tasks.size(), 2);
        QCOMPARE(tasks.at(0).box, 0);
        QCOMPARE(tasks.at(1).box, 1);
    }

    // The statuses that ask for a re-run: the decision stage's rejections and
    // the blocks it could not transcribe; an answered block is not a problem.
    void theProblemWorkIsTheBlocksTheDecisionRejected()
    {
        QueueFixture fixture(m_dir);
        BoundingBox mismatch = textBox(QStringLiteral("text"), QStringLiteral("wrong"));
        mismatch.checkStatus = BoxCheckStatus::Mismatch;
        BoundingBox review = textBox(QStringLiteral("text"), QStringLiteral("unreadable"));
        review.checkStatus = BoxCheckStatus::Review;
        BoundingBox ok = textBox(QStringLiteral("text"), QStringLiteral("right"));
        ok.checkStatus = BoxCheckStatus::Ok;
        BoundingBox plain = textBox(QStringLiteral("text"), QStringLiteral("unchecked"));

        makeDocument(fixture.document(), {mismatch, review, ok, plain});

        const auto tasks = fixture.workList().problemBlocks({0});
        QCOMPARE(tasks.size(), 2);
        QCOMPARE(tasks.at(0).box, 0);
        QCOMPARE(tasks.at(1).box, 1);
    }

    // The reader takes every text block of the page — recognized or not —
    // but not the image placeholders nobody transcribes.
    void theReadableWorkIsEveryTextBoxOfThePage()
    {
        QueueFixture fixture(m_dir);
        BoundingBox withText = textBox(QStringLiteral("text"), QStringLiteral("read me"));
        BoundingBox withoutText = textBox(QStringLiteral("title"), QString());
        BoundingBox image = textBox(QStringLiteral("image"), QString());

        makeDocument(fixture.document(), {withText, withoutText, image});

        const auto tasks = fixture.workList().readableBlocks({0});
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

        const auto tasks = fixture.workList().enabledBlocks({0});
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

        const BlockWorkList::TypeFilter onlyTitle = [](const QString &type) { return type == QLatin1String("title"); };

        QCOMPARE(fixture.workList().unrecognizedBlocks({0}).size(), 2);
        QCOMPARE(fixture.workList().enabledBlocks({0}).size(), 2);
        QCOMPARE(fixture.workList().fixableBlocks({0}).size(), 2);

        const auto unrecognized = fixture.workList().unrecognizedBlocks({0}, onlyTitle);
        QCOMPARE(unrecognized.size(), 1);
        QCOMPARE(unrecognized.first().box, 1);

        const auto enabled = fixture.workList().enabledBlocks({0}, onlyTitle);
        QCOMPARE(enabled.size(), 1);
        QCOMPARE(enabled.first().box, 5);

        const auto fixable = fixture.workList().fixableBlocks({0}, onlyTitle);
        QCOMPARE(fixable.size(), 1);
        QCOMPARE(fixable.first().box, 3);
    }

    void reasoningBudgetFollowsTheFixTaskProfile()
    {
        auto settings = std::make_unique<SettingsStore>();
        settings->setRuntimeRootDir(m_dir.filePath(QStringLiteral("runtime")));
        settings->setRuntimeModelsDir(m_dir.filePath(QStringLiteral("models")));
        RequestProfileStore store(*settings, ConnectionRole::Fix);
        QCOMPARE(store.activeProfileId(), QStringLiteral("qwen3.5-4b"));

        settings->setFixReasoningBudgetTokens(1024);
        QList<RequestParameter> parameters;
        applyReasoningBudget(parameters, CheckRequest::FixProtocol::FullBlock, store, *settings);
        QCOMPARE(parameters.size(), 1);
        QCOMPARE(parameterValue(parameters, QLatin1String(kReasoningBudgetParameter)), 1024);

        settings->setReadReasoningBudgetTokens(0);
        applyReasoningBudget(parameters, CheckRequest::FixProtocol::ReadBlock, store, *settings);
        QCOMPARE(parameters.size(), 1);
        QCOMPARE(parameterValue(parameters, QLatin1String(kReasoningBudgetParameter)), 0);

        settings->setFixRequestProfileId(QStringLiteral("qwen3-vl-4b"));
        QCOMPARE(store.activeProfileId(), QStringLiteral("qwen3-vl-4b"));
        applyReasoningBudget(parameters, CheckRequest::FixProtocol::FullBlock, store, *settings);
        QCOMPARE(parameterValue(parameters, QLatin1String(kReasoningBudgetParameter)), -1);

        settings->setFixRequestProfileId(QStringLiteral("qwen3.5-4b"));
        applyReasoningBudget(parameters, CheckRequest::FixProtocol::ReadBlock, store, *settings);
        QCOMPARE(parameters.size(), 1);
        QCOMPARE(parameterValue(parameters, QLatin1String(kReasoningBudgetParameter)), 0);
    }
};

QTEST_MAIN(TestVerificationQueue)
#include "test_verification_queue.moc"
