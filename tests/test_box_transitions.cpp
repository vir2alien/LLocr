#include <QtTest>

#include "core/BoxTransitions.h"
#include "core/LatexText.h"

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

    // The fix outcomes (ADR 163, ADR 240): the correction layer is rewritten,
    // the outcome itself is recorded — a confirmed text is not a stored
    // correction — and the fixer is no verifier: its OK leaves the block
    // without a verdict.
    void theFixOutcomesAndTheirVerdicts()
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
        // The fixer confirmed the text: nothing is stored, and the verdict it
        // looked at is gone — the block is "recognized" again (ADR 240).
        QVERIFY(confirmed.correctedText.isEmpty());
        QCOMPARE(confirmed.checkStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(confirmed.confidence, -1.0);
        QCOMPARE(confirmed.fixStatus, BoxFixStatus::NotNeeded);
        QVERIFY(!BoxTransitions::hasCorrection(confirmed));

        BoundingBox damaged = correctedBox();
        BoxTransitions::fixMarkedUnreadable(damaged);
        QVERIFY(damaged.correctedText.isEmpty());
        QCOMPARE(damaged.correctedCheckStatus, BoxCheckStatus::Review);
        QCOMPARE(damaged.fixStatus, BoxFixStatus::Damaged);
        // ...and the damaged verdict is what the frame shows.
        QCOMPARE(BoxTransitions::effectiveCheckStatus(damaged), BoxCheckStatus::Review);
    }

    // A fix that answers «no correction needed» drops the stale verdict: the
    // fixer is not a verifier (ADR 240), so the block goes back to the gray
    // «recognized» state and the corrected layer is not created at all.
    void fixConfirmationDropsTheRecognitionVerdict()
    {
        BoundingBox box = recognizedBox();
        box.checkStatus = BoxCheckStatus::Mismatch;
        box.confidence = 0.2;

        BoxTransitions::fixConfirmedOriginal(box);

        // The fixer is not a verifier (ADR 240): its OK says «nothing to
        // correct», not «verified». The stale verdict is dropped and the block
        // goes back to the gray «recognized» state.
        QCOMPARE(box.checkStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(box.confidence, -1.0);
        QCOMPARE(BoxTransitions::effectiveCheckStatus(box), BoxCheckStatus::NotChecked);
        QCOMPARE(BoxTransitions::effectiveText(box), QStringLiteral("recognized"));
        QCOMPARE(box.fixStatus, BoxFixStatus::NotNeeded);
    }

    // The same rule on a block the user reads the correction of: the fixer's
    // OK confirms the correction as it is, but the block loses its verdict
    // (ADR 240).
    void fixConfirmationDropsTheVerdictOfTheCorrectionInView()
    {
        BoundingBox box = correctedBox();
        box.correctedCheckStatus = BoxCheckStatus::Ok;
        box.correctedConfidence = 0.9;

        BoxTransitions::fixConfirmedOriginal(box);

        QCOMPARE(box.correctedText, QStringLiteral("corrected"));
        QCOMPARE(box.correctedCheckStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(box.correctedConfidence, -1.0);
        QCOMPARE(BoxTransitions::effectiveCheckStatus(box), BoxCheckStatus::NotChecked);
        QCOMPARE(box.fixStatus, BoxFixStatus::NotNeeded);
    }

    // The user moved or resized the block (ADR 241): the text was recognized
    // for the region the block used to cover and says nothing about this piece
    // of the page any more. The block goes back to markup — no text, no
    // verdict, no correction.
    void aMovedBlockGoesBackToMarkup()
    {
        BoundingBox box = correctedBox();
        box.correctedCheckStatus = BoxCheckStatus::Mismatch;
        box.correctedConfidence = 0.1;
        box.fixStatus = BoxFixStatus::Fixed;

        QVERIFY(BoxTransitions::blockRectChanged(box));

        QVERIFY(box.text.isEmpty());
        QVERIFY(box.correctedText.isEmpty());
        QCOMPARE(box.checkStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(box.confidence, -1.0);
        QCOMPARE(box.correctedCheckStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(box.correctedConfidence, -1.0);
        QCOMPARE(box.fixStatus, BoxFixStatus::None);
        QCOMPARE(box.syntaxProblem, LatexText::Problem::None);
        QVERIFY(!BoxTransitions::hasCorrection(box));

        // An empty block carries nothing: the caller learns that the page
        // text does not have to follow a second time.
        QVERIFY(!BoxTransitions::blockRectChanged(box));
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

    // The hand-written text (ADR 187): it lands in the version the user wrote
    // in, it is marked as the user's own text — not as a confirmation of the
    // model's verdict — and the version in view follows the hand.
    void theHandWrittenTextIsStoredAndMarkedAsEdited()
    {
        BoundingBox plain = recognizedBox();
        plain.checkStatus = BoxCheckStatus::Mismatch;
        plain.confidence = 0.3;

        BoxTransitions::blockTextEdited(plain, QStringLiteral("by hand"), false);
        QCOMPARE(plain.text, QStringLiteral("by hand"));
        QCOMPARE(plain.checkStatus, BoxCheckStatus::Edited);
        QCOMPARE(plain.confidence, -1.0);
        QVERIFY(!plain.useCorrectedText);

        BoundingBox corrected = correctedBox();
        corrected.checkStatus = BoxCheckStatus::Mismatch;
        BoxTransitions::blockTextEdited(corrected, QStringLiteral("fixed by hand"), true);
        QCOMPARE(corrected.correctedText, QStringLiteral("fixed by hand"));
        QCOMPARE(corrected.correctedCheckStatus, BoxCheckStatus::Edited);
        // The other version keeps its own text and verdict.
        QCOMPARE(corrected.text, QStringLiteral("recognized"));
        QCOMPARE(corrected.checkStatus, BoxCheckStatus::Mismatch);
        QVERIFY(corrected.useCorrectedText);
    }

    // A version the user opened and left alone is not a hand-written one: the
    // model's verdict stands.
    void anUnchangedEditKeepsTheVerdict()
    {
        BoundingBox box = recognizedBox();
        box.checkStatus = BoxCheckStatus::Mismatch;
        box.confidence = 0.2;

        BoxTransitions::blockTextEdited(box, box.text, false);

        QCOMPARE(box.checkStatus, BoxCheckStatus::Mismatch);
        QCOMPARE(box.confidence, 0.2);
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
        QCOMPARE(boxCheckStatusFromInt(6), BoxCheckStatus::Edited);
        QCOMPARE(boxCheckStatusFromInt(7), BoxCheckStatus::InvalidSyntax);
        QCOMPARE(boxCheckStatusFromInt(8), BoxCheckStatus::NotChecked);
    }

    // «Отозвать утверждение» (ADR 187): the approval is undone and the
    // verdict the decision model gave stands again.
    void approvalCanBeReverted()
    {
        BoundingBox plain = recognizedBox();
        plain.checkStatus = BoxCheckStatus::Mismatch;
        plain.confidence = 0.1;

        BoxTransitions::blockApproved(plain);
        QCOMPARE(plain.checkStatus, BoxCheckStatus::Approved);
        QCOMPARE(plain.checkStatusBeforeApproval, BoxCheckStatus::Mismatch);

        BoxTransitions::blockApprovalReverted(plain);
        QCOMPARE(plain.checkStatus, BoxCheckStatus::Mismatch);
        QCOMPARE(plain.checkStatusBeforeApproval, BoxCheckStatus::NotChecked);
        // The approval is gone: a second revert changes nothing.
        BoxTransitions::blockApprovalReverted(plain);
        QCOMPARE(plain.checkStatus, BoxCheckStatus::Mismatch);
    }

    // The correction layer keeps its own approval: the text layer's verdict
    // is not touched by the confirmation of the correction.
    void theApprovalOfTheCorrectionCanBeReverted()
    {
        BoundingBox box = correctedBox();
        box.correctedCheckStatus = BoxCheckStatus::Mismatch;

        BoxTransitions::blockApproved(box);
        QCOMPARE(box.correctedCheckStatus, BoxCheckStatus::Approved);
        QCOMPARE(box.checkStatus, BoxCheckStatus::Mismatch);

        BoxTransitions::blockApprovalReverted(box);
        QCOMPARE(box.correctedCheckStatus, BoxCheckStatus::Mismatch);
        QCOMPARE(box.checkStatus, BoxCheckStatus::Mismatch);
    }

    // A new verdict replaces the one the approval kept: the revert must not
    // bring back a verdict the model has already answered again.
    void aNewVerdictForgetsTheApproval()
    {
        BoundingBox box = recognizedBox();
        box.checkStatus = BoxCheckStatus::Mismatch;
        BoxTransitions::blockApproved(box);
        QCOMPARE(box.checkStatusBeforeApproval, BoxCheckStatus::Mismatch);

        BoxTransitions::verificationStarted(box);
        BoxTransitions::verdictApplied(box, verdict(CheckStatus::Ok, 0.9));

        QCOMPARE(box.checkStatus, BoxCheckStatus::Ok);
        QCOMPARE(box.checkStatusBeforeApproval, BoxCheckStatus::NotChecked);
    }

    // A block nobody judged keeps its «recognized» state when the approval is
    // undone: there is no verdict to bring back.
    void revertingAnApprovalWithoutAVerdictLeavesTheBlockUnchecked()
    {
        BoundingBox box = recognizedBox();
        BoxTransitions::blockApproved(box);
        QCOMPARE(box.checkStatus, BoxCheckStatus::Approved);

        BoxTransitions::blockApprovalReverted(box);
        QCOMPARE(box.checkStatus, BoxCheckStatus::NotChecked);
    }

    // A formula lands in its canonical LaTeX form, and a formula whose
    // LaTeX is broken is rejected right away (ADR 194).
    void aFormulaLandsCanonicalAndABrokenOneIsRejected()
    {
        BoundingBox box;
        box.label = QStringLiteral("equation");

        BoxTransitions::ocrResultApplied(box, QStringLiteral("Bi _ {1}"));
        QCOMPARE(box.text, QStringLiteral("Bi _{1}"));
        QCOMPARE(box.checkStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(box.syntaxProblem, LatexText::Problem::None);

        BoxTransitions::ocrResultApplied(box, QStringLiteral("\\frac{1}"));
        QCOMPARE(box.checkStatus, BoxCheckStatus::InvalidSyntax);
        QCOMPARE(box.syntaxProblem, LatexText::Problem::MissingArgument);
        // A rejected block waits for a new recognition, not for a verdict.
        QVERIFY(!BoxTransitions::needsFix(box));
    }

    // A text block is not a formula: its text is left as it was written.
    void aTextBlockIsNotTouchedByTheFormulaRules()
    {
        BoundingBox box = recognizedBox();
        BoxTransitions::ocrResultApplied(box, QStringLiteral("a { b"));
        QCOMPARE(box.text, QStringLiteral("a { b"));
        QCOMPARE(box.checkStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(box.syntaxProblem, LatexText::Problem::None);
    }

    // The syntax verdict is derived from the text: a project written before
    // the rules changed carries the old verdict, and it is recomputed when
    // the project opens (ADR 194).
    void theSyntaxVerdictIsRecomputedFromTheText()
    {
        BoundingBox box;
        box.label = QStringLiteral("equation");
        box.text = QStringLiteral("\\frac{1}{2}");

        // A stale rejection of a formula that is sound: the rejection is void.
        box.checkStatus = BoxCheckStatus::InvalidSyntax;
        box.syntaxProblem = LatexText::Problem::MissingArgument;
        BoxTransitions::syntaxVerdictRefreshed(box);
        QCOMPARE(box.checkStatus, BoxCheckStatus::NotChecked);
        QCOMPARE(box.syntaxProblem, LatexText::Problem::None);

        // A formula that is broken is rejected whatever the file says.
        box.text = QStringLiteral("\\frac{1}");
        box.checkStatus = BoxCheckStatus::Ok;
        BoxTransitions::syntaxVerdictRefreshed(box);
        QCOMPARE(box.checkStatus, BoxCheckStatus::InvalidSyntax);
        QCOMPARE(box.syntaxProblem, LatexText::Problem::MissingArgument);

        // The hand-written text is the operator's own, and so is its state.
        box.checkStatus = BoxCheckStatus::Edited;
        BoxTransitions::syntaxVerdictRefreshed(box);
        QCOMPARE(box.checkStatus, BoxCheckStatus::Edited);
    }
};

QTEST_MAIN(TestBoxTransitions)
#include "test_box_transitions.moc"
