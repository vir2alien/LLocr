#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "app/AvailabilityPresenter.h"
#include "app/BlockWorkList.h"
#include "app/DocumentModel.h"
#include "app/PageListModel.h"
#include "app/VerificationPromptStore.h"
#include "config/RequestProfileStore.h"
#include "config/SettingsStore.h"
#include "runtime/LaunchProfileStore.h"
#include "runtime/RuntimeController.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// A block without text: the markup pass writes it and the reader takes it.
BoundingBox markupBox(const QString &label)
{
    BoundingBox box;
    box.label = label;
    return box;
}

// The presenter and the dependencies it reads. The runtime paths point inside
// the test's own directory, so the test never touches the user's profiles.
class AvailabilityFixture
{
public:
    explicit AvailabilityFixture(const QTemporaryDir &dir)
    {
        m_settings = std::make_unique<SettingsStore>();
        m_settings->setRuntimeRootDir(dir.filePath(QStringLiteral("runtime")));
        m_settings->setRuntimeModelsDir(dir.filePath(QStringLiteral("models")));
        m_launchProfiles = std::make_unique<LaunchProfileStore>(*m_settings);
        m_checkProfiles = std::make_unique<RequestProfileStore>(*m_settings, ConnectionRole::BlockRecognition);
        m_runtime = std::make_unique<RuntimeController>(*m_settings, *m_launchProfiles);
        m_verification = std::make_unique<VerificationPromptStore>(*m_settings);
        m_document = std::make_unique<DocumentModel>();
        m_pageModel = std::make_unique<PageListModel>();
        m_workList = std::make_unique<BlockWorkList>(BlockWorkList::Deps{*m_document, *m_verification, *m_checkProfiles});
        m_presenter = std::make_unique<AvailabilityPresenter>(AvailabilityPresenter::Deps{*m_document,
                                                                                          *m_pageModel,
                                                                                          *m_workList,
                                                                                          *m_runtime,
                                                                                          *m_verification,
                                                                                          [this] { return m_currentPage; },
                                                                                          [] { return false; },
                                                                                          [] { return false; },
                                                                                          [] { return false; },
                                                                                          [] { return false; }},
                                                              nullptr);
    }

    AvailabilityPresenter &presenter() { return *m_presenter; }
    PageListModel &pageModel() { return *m_pageModel; }
    DocumentModel &document() { return *m_document; }
    void setCurrentPage(int index) { m_currentPage = index; }

private:
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<LaunchProfileStore> m_launchProfiles;
    std::unique_ptr<RequestProfileStore> m_checkProfiles;
    std::unique_ptr<RuntimeController> m_runtime;
    std::unique_ptr<VerificationPromptStore> m_verification;
    std::unique_ptr<DocumentModel> m_document;
    std::unique_ptr<PageListModel> m_pageModel;
    std::unique_ptr<BlockWorkList> m_workList;
    std::unique_ptr<AvailabilityPresenter> m_presenter;
    int m_currentPage = 0;
};

}  // namespace

// The availability presenter: the flags the toolbar's buttons read and the
// announcement when one of them flips.
class TestAvailabilityPresenter : public QObject
{
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_availability_presenter"));

        QImage source(64, 48, QImage::Format_ARGB32);
        source.fill(Qt::white);
        QVERIFY(source.save(m_dir.filePath(QStringLiteral("page.png")), "PNG"));
    }

    // A page of the given markup blocks: marked up, but without text.
    void makePage(AvailabilityFixture &fixture, int blockCount)
    {
        QVERIFY(fixture.document().appendImage(m_dir.filePath(QStringLiteral("page.png"))));
        const int index = fixture.document().pageCount() - 1;
        fixture.document().page(index).recognized = true;
        OcrPage page;
        for (int i = 0; i < blockCount; ++i)
            page.boxes.push_back(markupBox(QStringLiteral("text")));
        fixture.document().page(index).result.pages = {page};
    }

    // The count of the blocks the reader would take follows the scope: when
    // the scope moves to a page with a different number of blocks, the
    // presenter announces it. Nothing else changes then — the same number of
    // selected pages, the same empty check, the same markup-only mode — so
    // the count is the only flag that flips, and the toolbar's number must
    // not stay behind.
    void theScopeBlockCountIsAnnounced()
    {
        AvailabilityFixture fixture(m_dir);
        makePage(fixture, 2);
        makePage(fixture, 3);
        fixture.pageModel().appendPages(2);

        fixture.pageModel().selectSingle(0);
        fixture.presenter().refresh({1, 0, 1, 0});
        QCOMPARE(fixture.presenter().scopedReadBlockCount(), 2);

        QSignalSpy spy(&fixture.presenter(), &AvailabilityPresenter::changed);
        fixture.pageModel().selectSingle(1);
        fixture.presenter().refresh({1, 0, 2, 0});

        QCOMPARE(fixture.presenter().scopedReadBlockCount(), 3);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestAvailabilityPresenter)
#include "test_availability_presenter.moc"
