#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QtTest>

#include "app/DocumentModel.h"
#include "app/PageListModel.h"

using namespace llocr;

namespace {

// One box per scenario: the label decides whether the filter makes it a
// subject, the flags decide whether it is answered.
BoundingBox box(const QString &label, bool suspect = false)
{
    BoundingBox box;
    box.label = label;
    box.duplicateSuspect = suspect;
    box.text = QStringLiteral("block text");
    return box;
}

const OcrPage &singlePage(const QList<BoundingBox> &boxes)
{
    static OcrPage page;
    page.boxes = boxes;
    return page;
}

}  // namespace

class TestPageStatus : public QObject
{
    Q_OBJECT

private slots:
    void everySubjectAnsweredMeansClean()
    {
        const auto always = [](const QString &) { return true; };
        BoundingBox checkedText = box(QStringLiteral("text"));
        checkedText.checkStatus = BoxCheckStatus::Ok;
        BoundingBox checkedTitle = box(QStringLiteral("title"));
        checkedTitle.checkStatus = BoxCheckStatus::Ok;
        QCOMPARE(PageStatus::verificationAggregate(singlePage({checkedText, checkedTitle}), always), PageVerification::Clean);
    }

    void oneMismatchIsEnoughForErrors()
    {
        const auto always = [](const QString &) { return true; };
        BoundingBox checkedTitle = box(QStringLiteral("title"));
        checkedTitle.checkStatus = BoxCheckStatus::Ok;
        BoundingBox rejected = box(QStringLiteral("text"));
        rejected.checkStatus = BoxCheckStatus::Mismatch;
        QCOMPARE(PageStatus::verificationAggregate(singlePage({checkedTitle, rejected}), always), PageVerification::HasErrors);
    }

    void reviewCountsAsAnError()
    {
        const auto always = [](const QString &) { return true; };
        BoundingBox unreadable = box(QStringLiteral("text"));
        unreadable.checkStatus = BoxCheckStatus::Review;
        QCOMPARE(PageStatus::verificationAggregate(singlePage({unreadable}), always), PageVerification::HasErrors);
    }

    void aDisabledTypeDoesNotBlock()
    {
        // Only "title" is enabled: the unchecked "text" block is nobody's
        // subject and the page verifies clean.
        const auto titlesOnly = [](const QString &label) { return label == QStringLiteral("title"); };
        BoundingBox checkedTitle = box(QStringLiteral("title"));
        checkedTitle.checkStatus = BoxCheckStatus::Ok;
        QCOMPARE(PageStatus::verificationAggregate(singlePage({box(QStringLiteral("text")), checkedTitle}), titlesOnly), PageVerification::Clean);
    }

    void aDuplicateSuspectIsAlwaysASubject()
    {
        const auto titlesOnly = [](const QString &label) { return label == QStringLiteral("title"); };
        BoundingBox suspect = box(QStringLiteral("text"), true);  // type disabled, suspect
        QCOMPARE(PageStatus::verificationAggregate(singlePage({suspect}), titlesOnly), PageVerification::Incomplete);

        suspect.correctedCheckStatus = BoxCheckStatus::Ok;
        QCOMPARE(PageStatus::verificationAggregate(singlePage({suspect}), titlesOnly), PageVerification::Clean);
    }

    void anUnansweredSubjectLeavesThePageIncomplete()
    {
        const auto always = [](const QString &) { return true; };
        QCOMPARE(PageStatus::verificationAggregate(singlePage({box(QStringLiteral("text"))}), always), PageVerification::Incomplete);

        // A verdict and a new block: the page drops back to Incomplete.
        BoundingBox answered = box(QStringLiteral("text"));
        answered.checkStatus = BoxCheckStatus::Ok;
        QCOMPARE(PageStatus::verificationAggregate(singlePage({answered, box(QStringLiteral("table"))}), always), PageVerification::Incomplete);
    }

    void noSubjectsAtAllIsIncomplete()
    {
        const auto nothing = [](const QString &) { return false; };
        QCOMPARE(PageStatus::verificationAggregate(singlePage({box(QStringLiteral("text"))}), nothing), PageVerification::Incomplete);
    }

    void theCorrectionLayerVerdictIsWhatCounts()
    {
        const auto always = [](const QString &) { return true; };
        BoundingBox corrected = box(QStringLiteral("text"));
        corrected.checkStatus = BoxCheckStatus::Mismatch;  // the OCR text was rejected
        corrected.correctedText = QStringLiteral("fixed");
        corrected.correctedCheckStatus = BoxCheckStatus::Ok;  // the correction was confirmed
        QCOMPARE(PageStatus::verificationAggregate(singlePage({corrected}), always), PageVerification::Clean);

        corrected.correctedCheckStatus = BoxCheckStatus::Review;  // the fix refused
        QCOMPARE(PageStatus::verificationAggregate(singlePage({corrected}), always), PageVerification::HasErrors);
    }

    void markupOnlyCompletesWhenEveryTextBlockCarriesText()
    {
        const auto everythingButImages = [](const BoundingBox &b) { return b.label != QStringLiteral("image"); };

        BoundingBox text = box(QStringLiteral("text"));
        BoundingBox placeholder;
        placeholder.label = QStringLiteral("image");

        // The placeholder's empty text does not hold the page back.
        QCOMPARE(PageStatus::recognitionModeAfterBlockText(PageRecognitionMode::MarkupOnly, singlePage({text, placeholder}), everythingButImages), PageRecognitionMode::MarkupOcr);

        // One text-less block does.
        BoundingBox pending = box(QStringLiteral("title"));
        pending.text.clear();
        QCOMPARE(PageStatus::recognitionModeAfterBlockText(PageRecognitionMode::MarkupOnly, singlePage({text, pending}), everythingButImages), PageRecognitionMode::MarkupOnly);

        // The other modes never flip here.
        QCOMPARE(PageStatus::recognitionModeAfterBlockText(PageRecognitionMode::RawOcr, singlePage({pending}), everythingButImages), PageRecognitionMode::RawOcr);
        QCOMPARE(PageStatus::recognitionModeAfterBlockText(PageRecognitionMode::MarkupOcr, singlePage({pending}), everythingButImages), PageRecognitionMode::MarkupOcr);
        QCOMPARE(PageStatus::recognitionModeAfterBlockText(PageRecognitionMode::None, singlePage({pending}), everythingButImages), PageRecognitionMode::None);
    }

    void pendingFixCountReadsTheEffectiveLayer()
    {
        BoundingBox answered = box(QStringLiteral("text"));
        answered.checkStatus = BoxCheckStatus::Ok;
        BoundingBox rejected = box(QStringLiteral("title"));
        rejected.checkStatus = BoxCheckStatus::Mismatch;
        BoundingBox unreadable = box(QStringLiteral("table"));
        unreadable.checkStatus = BoxCheckStatus::Review;
        BoundingBox pending = box(QStringLiteral("text"));  // NotChecked
        QCOMPARE(PageStatus::pendingFixCount(singlePage({answered, rejected, unreadable, pending})), 2);
        QCOMPARE(PageStatus::pendingFixCount(singlePage({answered, pending})), 0);
        QCOMPARE(PageStatus::pendingFixCount(singlePage({})), 0);

        // A fix verdict on the correction layer counts: the block still asks
        // for attention (a refused REVIEW) or is settled by it.
        BoundingBox corrected = box(QStringLiteral("text"));
        corrected.checkStatus = BoxCheckStatus::Mismatch;
        corrected.correctedText = QStringLiteral("fixed");
        corrected.correctedCheckStatus = BoxCheckStatus::Ok;
        QCOMPARE(PageStatus::pendingFixCount(singlePage({corrected})), 0);
        corrected.correctedCheckStatus = BoxCheckStatus::Review;
        QCOMPARE(PageStatus::pendingFixCount(singlePage({corrected})), 1);

        // The fixer has answered this candidate (ADR 188): the block waits for
        // the operator, not for another fix run.
        BoundingBox confirmed = box(QStringLiteral("text"));
        confirmed.checkStatus = BoxCheckStatus::Mismatch;
        confirmed.fixStatus = BoxFixStatus::NotNeeded;
        QCOMPARE(PageStatus::pendingFixCount(singlePage({confirmed})), 0);
        BoundingBox damaged = box(QStringLiteral("text"));
        damaged.correctedCheckStatus = BoxCheckStatus::Review;
        damaged.useCorrectedText = true;
        damaged.fixStatus = BoxFixStatus::Damaged;
        QCOMPARE(PageStatus::pendingFixCount(singlePage({damaged})), 0);
        // A stored correction is a new candidate the fixer has not seen: a
        // correction the decision model rejected goes back to it.
        BoundingBox rejectedCorrection = box(QStringLiteral("text"));
        rejectedCorrection.correctedText = QStringLiteral("fixed");
        rejectedCorrection.correctedCheckStatus = BoxCheckStatus::Mismatch;
        rejectedCorrection.useCorrectedText = true;
        rejectedCorrection.fixStatus = BoxFixStatus::Fixed;
        QCOMPARE(PageStatus::pendingFixCount(singlePage({rejectedCorrection})), 1);
    }

    void pageListModelCarriesTheStatusRoles()
    {
        PageListModel model;
        model.appendPages(3);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);

        QSignalSpy modeSpy(&model, &QAbstractItemModel::dataChanged);
        model.setRecognitionMode(1, int(PageRecognitionMode::MarkupOnly));
        model.setVerification(1, int(PageVerification::HasErrors));
        QCOMPARE(modeSpy.count(), 2);

        const QModelIndex mi = model.index(1);
        QCOMPARE(model.data(mi, PageListModel::RecognitionModeRole).toInt(), int(PageRecognitionMode::MarkupOnly));
        QCOMPARE(model.data(mi, PageListModel::VerificationRole).toInt(), int(PageVerification::HasErrors));
        // Untouched rows keep the defaults.
        QCOMPARE(model.data(model.index(0), PageListModel::RecognitionModeRole).toInt(), 0);
        QCOMPARE(model.data(model.index(0), PageListModel::VerificationRole).toInt(), 0);

        // A no-op write stays silent.
        const int writes = modeSpy.count();
        model.setVerification(1, int(PageVerification::HasErrors));
        QCOMPARE(modeSpy.count(), writes);

        // The values follow their rows through a move.
        model.movePage(1, 0);
        QCOMPARE(model.data(model.index(0), PageListModel::RecognitionModeRole).toInt(), int(PageRecognitionMode::MarkupOnly));
        QCOMPARE(model.data(model.index(0), PageListModel::VerificationRole).toInt(), int(PageVerification::HasErrors));
    }
};

QTEST_MAIN(TestPageStatus)
#include "test_page_status.moc"
