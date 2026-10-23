#include <QtTest>

#include "core/BoxTransitions.h"

using namespace llocr;

namespace {

BoundingBox recognizedBox()
{
    BoundingBox box;
    box.text = QStringLiteral("recognized");
    box.label = QStringLiteral("text");
    return box;
}

BoundingBox correctedBox()
{
    // A block the universal model has fixed: text layer + correction layer.
    BoundingBox box = recognizedBox();
    box.correctedText = QStringLiteral("corrected");
    box.checkStatus = BoxCheckStatus::Mismatch;
    box.confidence = 0.2;
    return box;
}

CheckResult verdict(CheckStatus status, double confidence = -1.0)
{
    CheckResult result;
    result.status = status;
    result.confidence = confidence;
    return result;
}

}  // namespace

// The block state machine (ADR 164): two text layers with independent
// verdicts, transitions that erase exactly what the plan (§4–5) says.
class TestBoxTransitions : public QObject
{
    Q_OBJECT

private slots:
    // Any OCR result overwrites the text layer and erases the correction
    // layer entirely: a re-recognition never stacks a second variant.
    void ocrResultOverwritesTheTextAndErasesTheCorrection()
    {
        BoundingBox box = correctedBox();
        box.correctedCheckStatus = BoxCheckStatus::Ok;
        box.correctedConfidence = 0.9;

        BoxTransitions::ocrResultApplied(box, QStringLiteral("fresh"));

        QCOMPARE(box.text, QStringLiteral("fresh"));
        QVERIFY(box.correctedText.isEmpty());
        QCOMPARE(box.checkStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(box.confidence, -1.0);
        QCOMPARE(box.correctedCheckStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(box.correctedConfidence, -1.0);
        QVERIFY(!BoxTransitions::hasCorrection(box));
    }

    // The transcription model answered "unreadable": the text survives, the
    // text layer carries the Review verdict, the correction is gone.
    void blockUnreadableKeepsTheTextAndMarksReview()
    {
        BoundingBox box = correctedBox();

        BoxTransitions::blockUnreadable(box);

        QCOMPARE(box.text, QStringLiteral("recognized"));
        QVERIFY(box.correctedText.isEmpty());
        QCOMPARE(box.checkStatus, BoxCheckStatus::Review);
        QCOMPARE(box.confidence, -1.0);
        QCOMPARE(box.correctedCheckStatus, BoxCheckStatus::NotChecked);
    }

    // A decision run starts by erasing the verdict of the layer it judges.
    void verificationErasesTheVerdictOfTheEffectiveLayer()
    {
        BoundingBox textJudged = recognizedBox();
        textJudged.checkStatus = BoxCheckStatus::Mismatch;
        textJudged.confidence = 0.1;
        BoxTransitions::verificationStarted(textJudged);
        QCOMPARE(textJudged.checkStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(textJudged.confidence, -1.0);
        QVERIFY(!BoxTransitions::hasCorrection(textJudged));

        BoundingBox correctionJudged = correctedBox();
        correctionJudged.correctedCheckStatus = BoxCheckStatus::Ok;
        correctionJudged.correctedConfidence = 0.8;
        BoxTransitions::verificationStarted(correctionJudged);
        // The text layer's verdict is about the OCR text — it survives.
        QCOMPARE(correctionJudged.checkStatus, BoxCheckStatus::Mismatch);
        QCOMPARE(correctionJudged.confidence, 0.2);
        QCOMPARE(correctionJudged.correctedCheckStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(correctionJudged.correctedConfidence, -1.0);
    }

    // The verdict lands on the effective layer: the text for a plain block,
    // the correction for a corrected one.
    void verdictLandsOnTheEffectiveLayer()
    {
        BoundingBox plain = recognizedBox();
        BoxTransitions::verdictApplied(plain, verdict(CheckStatus::Ok, 0.97));
        QCOMPARE(plain.checkStatus, BoxCheckStatus::Ok);
        QCOMPARE(plain.confidence, 0.97);
        QVERIFY(plain.correctedText.isEmpty());

        BoundingBox corrected = correctedBox();
        BoxTransitions::verdictApplied(corrected, verdict(CheckStatus::Mismatch, 0.1));
        QCOMPARE(corrected.correctedCheckStatus, BoxCheckStatus::Mismatch);
        QCOMPARE(corrected.correctedConfidence, 0.1);
        QCOMPARE(corrected.checkStatus, BoxCheckStatus::Mismatch);
        QCOMPARE(corrected.confidence, 0.2);
    }

    // The fix outcomes (ADR 163, ADR 185): the correction layer is rewritten,
    // the text layer's verdict survives everything, and the outcome itself is
    // recorded — a confirmed text is not a stored correction.
    void fixOutcomesTouchOnlyTheCorrectionLayer()
    {
        BoundingBox fixed = recognizedBox();
        fixed.checkStatus = BoxCheckStatus::Mismatch;
        fixed.confidence = 0.1;
        BoxTransitions::fixApplied(fixed, QStringLiteral("better text"));
        QCOMPARE(fixed.correctedText, QStringLiteral("better text"));
        QCOMPARE(fixed.correctedCheckStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(fixed.correctedConfidence, -1.0);
        QCOMPARE(fixed.checkStatus, BoxCheckStatus::Mismatch);
        QCOMPARE(fixed.fixStatus, BoxFixStatus::Fixed);
        // A fresh correction is the text the user reads.
        QVERIFY(fixed.useCorrectedText);

        BoundingBox confirmed = recognizedBox();
        confirmed.checkStatus = BoxCheckStatus::Mismatch;
        BoxTransitions::fixConfirmedOriginal(confirmed);
        // The fixer confirmed the text: nothing is stored, and the verdict the
        // recognition earned survives.
        QVERIFY(confirmed.correctedText.isEmpty());
        QCOMPARE(confirmed.checkStatus, BoxCheckStatus::Mismatch);
        QCOMPARE(confirmed.fixStatus, BoxFixStatus::NotNeeded);
        QVERIFY(!BoxTransitions::hasCorrection(confirmed));

        BoundingBox damaged = correctedBox();
        BoxTransitions::fixMarkedUnreadable(damaged);
        QVERIFY(damaged.correctedText.isEmpty());
        QCOMPARE(damaged.correctedCheckStatus, BoxCheckStatus::Review);
        QCOMPARE(damaged.fixStatus, BoxFixStatus::Damaged);
        // ...and the damaged verdict is what the dot shows.
        QCOMPARE(BoxTransitions::effectiveCheckStatus(damaged), BoxCheckStatus::Review);
    }

    // A fix that answers «no correction needed» must not throw the
    // recognition's verdict away (ADR 185): the text layer keeps it, and the
    // corrected layer is not created at all.
    void fixConfirmationKeepsTheRecognitionVerdict()
    {
        BoundingBox box = recognizedBox();
        box.checkStatus = BoxCheckStatus::Mismatch;
        box.confidence = 0.2;

        BoxTransitions::fixConfirmedOriginal(box);

        QCOMPARE(box.checkStatus, BoxCheckStatus::Mismatch);
        QCOMPARE(box.confidence, 0.2);
        QCOMPARE(BoxTransitions::effectiveCheckStatus(box), BoxCheckStatus::Mismatch);
        QCOMPARE(BoxTransitions::effectiveText(box), QStringLiteral("recognized"));
        QCOMPARE(box.fixStatus, BoxFixStatus::NotNeeded);
    }

    // The panel's toggle (ADR 185): the user reads the other layer, and the
    // verdict follows the layer in view.
    void theTogglePicksTheLayerInView()
    {
        BoundingBox box = correctedBox();
        box.correctedCheckStatus = BoxCheckStatus::Ok;
        box.correctedConfidence = 0.9;

        // The correction is in view by default.
        QVERIFY(BoxTransitions::correctionSelected(box));
        QCOMPARE(BoxTransitions::effectiveText(box), QStringLiteral("corrected"));
        QCOMPARE(BoxTransitions::effectiveCheckStatus(box), BoxCheckStatus::Ok);

        BoxTransitions::layerSwitched(box, false);
        QVERIFY(!BoxTransitions::correctionSelected(box));
        QCOMPARE(BoxTransitions::effectiveText(box), QStringLiteral("recognized"));
        // The text layer's own verdict answers for the text in view.
        QCOMPARE(BoxTransitions::effectiveCheckStatus(box), BoxCheckStatus::Mismatch);
        QCOMPARE(BoxTransitions::effectiveConfidence(box), 0.2);

        BoxTransitions::layerSwitched(box, true);
        QCOMPARE(BoxTransitions::effectiveText(box), QStringLiteral("corrected"));
        QCOMPARE(BoxTransitions::effectiveCheckStatus(box), BoxCheckStatus::Ok);
    }

    // A re-fix that changes the text switches the view to the new text
    // (ADR 185) — the user was reading the old one.
    void aNewCorrectionTakesOverTheView()
    {
        BoundingBox box = correctedBox();
        BoxTransitions::layerSwitched(box, false);

        BoxTransitions::fixApplied(box, QStringLiteral("newer text"));

        QVERIFY(box.useCorrectedText);
        QVERIFY(BoxTransitions::correctionSelected(box));
        QCOMPARE(BoxTransitions::effectiveText(box), QStringLiteral("newer text"));
    }

    // The user's own verdict (ADR 185): the layer in view is marked correct
    // even when the decision model rejected it.
    void approvalMarksTheLayerInView()
    {
        BoundingBox plain = recognizedBox();
        plain.checkStatus = BoxCheckStatus::Mismatch;
        plain.confidence = 0.1;

        BoxTransitions::blockApproved(plain);

        QCOMPARE(plain.checkStatus, BoxCheckStatus::Approved);
        QCOMPARE(BoxTransitions::effectiveCheckStatus(plain), BoxCheckStatus::Approved);

        BoundingBox corrected = correctedBox();
        corrected.correctedCheckStatus = BoxCheckStatus::Mismatch;
        BoxTransitions::blockApproved(corrected);

        // The correction is in view: the correction layer is approved, the
        // text layer keeps the model's verdict.
        QCOMPARE(corrected.correctedCheckStatus, BoxCheckStatus::Approved);
        QCOMPARE(corrected.checkStatus, BoxCheckStatus::Mismatch);
    }

    // «Отменить исправление»: the block rolls back to the text layer's state —
    // including the verdict the text earned before the correction.
    void correctionRevertedRollsBackToTheTextLayer()
    {
        BoundingBox box = correctedBox();
        box.correctedCheckStatus = BoxCheckStatus::Ok;
        box.correctedConfidence = 0.9;

        BoxTransitions::correctionReverted(box);

        QVERIFY(box.correctedText.isEmpty());
        QCOMPARE(box.correctedCheckStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(box.correctedConfidence, -1.0);
        QCOMPARE(BoxTransitions::effectiveText(box), QStringLiteral("recognized"));
        QCOMPARE(BoxTransitions::effectiveCheckStatus(box), BoxCheckStatus::Mismatch);
        QCOMPARE(BoxTransitions::effectiveConfidence(box), 0.2);
    }

    // The effective helpers: the correction wins when the layer carries text
    // or a verdict — a fix REVIEW stores a verdict without text and the dot
    // must still show damaged.
    void effectiveHelpersFollowTheCorrectionLayer()
    {
        BoundingBox plain = recognizedBox();
        QCOMPARE(BoxTransitions::effectiveText(plain), QStringLiteral("recognized"));
        QCOMPARE(BoxTransitions::effectiveCheckStatus(plain), BoxCheckStatus::NotChecked);
        QCOMPARE(BoxTransitions::effectiveConfidence(plain), -1.0);

        BoundingBox corrected = correctedBox();
        QVERIFY(BoxTransitions::hasCorrection(corrected));
        QCOMPARE(BoxTransitions::effectiveText(corrected), QStringLiteral("corrected"));
        QCOMPARE(BoxTransitions::effectiveCheckStatus(corrected), BoxCheckStatus::NotChecked);

        BoundingBox damaged = recognizedBox();
        QVERIFY(!BoxTransitions::hasCorrection(damaged));
        BoxTransitions::fixMarkedUnreadable(damaged);
        QVERIFY(BoxTransitions::hasCorrection(damaged));
        QCOMPARE(BoxTransitions::effectiveText(damaged), QStringLiteral("recognized"));
        QCOMPARE(BoxTransitions::effectiveCheckStatus(damaged), BoxCheckStatus::Review);
    }

    // The removed ReRecognized value leaves a numeric hole: old projects read
    // it as NotChecked, the real statuses keep their values.
    void reservedValueTwoReadsAsNotChecked()
    {
        QCOMPARE(boxCheckStatusFromInt(2), BoxCheckStatus::NotChecked);
        QCOMPARE(boxCheckStatusFromInt(0), BoxCheckStatus::NotChecked);
        QCOMPARE(boxCheckStatusFromInt(1), BoxCheckStatus::Ok);
        QCOMPARE(boxCheckStatusFromInt(3), BoxCheckStatus::Review);
        QCOMPARE(boxCheckStatusFromInt(4), BoxCheckStatus::Mismatch);
        QCOMPARE(boxCheckStatusFromInt(5), BoxCheckStatus::Approved);
        QCOMPARE(boxCheckStatusFromInt(-1), BoxCheckStatus::NotChecked);
        QCOMPARE(boxCheckStatusFromInt(6), BoxCheckStatus::NotChecked);
    }
};

QTEST_MAIN(TestBoxTransitions)
#include "test_box_transitions.moc"
