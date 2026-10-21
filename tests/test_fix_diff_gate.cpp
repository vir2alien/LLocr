#include <QtTest>

#include "models/FixDiffGate.h"

using namespace llocr;
using namespace llocr::FixDiffGate;

namespace {

Outcome runSpan(const QString &candidate, const QString &body, const QString &type = QStringLiteral("text"))
{
    return fixSpan(candidate, body, type);
}

Outcome runFull(const QString &candidate, const QString &body, const QString &type)
{
    return fixFullBlock(candidate, body, type);
}

}  // namespace

class TestFixDiffGate : public QObject
{
    Q_OBJECT

private slots:
    // The normalization mirrors the prompts' equivalence lists; strictly
    // different pairs (е/ё, homoglyphs) must survive it untouched.
    void normalizationCoversTheEquivalencePolicy();
    void codeKeepsItsWhitespace();
    void spanFixAppliesEditsToTheCandidate();
    void spanAnchorProblemsDowngradeToReview();
    void spanEquivalentNoopBecomesOk();
    void fullBlockWithDeclaredEditsIsAccepted();
    void fullBlockUndeclaredDriftBecomesReview();
    void fullBlockWithoutEditsSectionBecomesReview();
    void fullBlockDeclaredFragmentsAreChecked();
    void fullBlockEquivalentReplyBecomesOk();
    void tableStructuralEditBypassesTheTokenDiff();
    void fullBlockTypesAreTheStructuralOnes();
    // The model normalizes what it copies: carrier markup disappears, spaces
    // collapse — the anchor still has to land.
    void spanAnchorMatchesThroughEquivalence();
    void fixNoneIsReviewed();
    void editsNoneDefersToTheEquivalenceCheck();
    void latexSpacingIsEquivalentInMathChunks();
    void rowOnlyCellPrefixIsALocation();
    void descriptiveNewSideSkipsTheContainmentCheck();
    void spanAnchorMatchesHyphenation();
    void spanMathSpacingChurnIsSkipped();
    void spanFullBlockFallbackForFormulas();
};

void TestFixDiffGate::normalizationCoversTheEquivalencePolicy()
{
    QVERIFY(normalizeForEquivalence(QStringLiteral("e\u0301"), false) == normalizeForEquivalence(QStringLiteral("\u00E9"), false));
    QVERIFY(normalizeForEquivalence(QStringLiteral("при\u00ADмер"), false) == normalizeForEquivalence(QStringLiteral("пример"), false));
    QVERIFY(normalizeForEquivalence(QStringLiteral("конец\u2026"), false) == normalizeForEquivalence(QStringLiteral("конец..."), false));
    QVERIFY(normalizeForEquivalence(QStringLiteral("\uFB01nal"), false) == normalizeForEquivalence(QStringLiteral("final"), false));
    QVERIFY(normalizeForEquivalence(QStringLiteral("a\u00A0\u2003b"), false) == normalizeForEquivalence(QStringLiteral("a b"), false));
    QVERIFY(normalizeForEquivalence(QStringLiteral("a   b"), false) == normalizeForEquivalence(QStringLiteral("a b"), false));
    QVERIFY(normalizeForEquivalence(QStringLiteral("«Альфа»"), false) == normalizeForEquivalence(QStringLiteral("\"Альфа\""), false));
    QVERIFY(normalizeForEquivalence(QStringLiteral("конец "), false) == normalizeForEquivalence(QStringLiteral(" конец"), false));
    // Strictly different — never normalized.
    QVERIFY(normalizeForEquivalence(QStringLiteral("лес"), false) != normalizeForEquivalence(QStringLiteral("лёс"), false));
    QVERIFY(normalizeForEquivalence(QStringLiteral("мама"), false) != normalizeForEquivalence(QStringLiteral("mama"), false));
    QVERIFY(normalizeForEquivalence(QStringLiteral("0,5"), false) != normalizeForEquivalence(QStringLiteral("0.5"), false));
}

void TestFixDiffGate::codeKeepsItsWhitespace()
{
    QVERIFY(normalizeForEquivalence(QStringLiteral("a  b"), true) != normalizeForEquivalence(QStringLiteral("a b"), true));
    QVERIFY(normalizeForEquivalence(QStringLiteral("«x»"), true) != normalizeForEquivalence(QStringLiteral("\"x\""), true));
    QVERIFY(normalizeForEquivalence(QStringLiteral("\uFB01nal"), true) == normalizeForEquivalence(QStringLiteral("final"), true));
}

void TestFixDiffGate::spanFixAppliesEditsToTheCandidate()
{
    const QString candidate = QStringLiteral("В 2026 году коэффициент k = 0,85 был подтверждён.");
    const Outcome outcome = runSpan(candidate, QStringLiteral("k = 0,85 был => k = 0,35 был"));
    QCOMPARE(outcome.status, CheckStatus::Fixed);
    QVERIFY(outcome.downgradeReason.isEmpty());
    QCOMPARE(outcome.text, QStringLiteral("В 2026 году коэффициент k = 0,35 был подтверждён."));

    // Edits apply in order; a line break inside a span travels escaped.
    const QString lines = QStringLiteral("alpha\nbeta");
    const Outcome escaped = runSpan(lines, QStringLiteral("alpha\\nbeta => alpha\\ngamma"));
    QCOMPARE(escaped.status, CheckStatus::Fixed);
    QCOMPARE(escaped.text, QStringLiteral("alpha\ngamma"));
}

void TestFixDiffGate::spanAnchorProblemsDowngradeToReview()
{
    const QString candidate = QStringLiteral("Точное число равно 42.");

    const Outcome missing = runSpan(candidate, QStringLiteral("число равно 43 => число равно 43,5"));
    QCOMPARE(missing.status, CheckStatus::Review);
    QVERIFY(!missing.downgradeReason.isEmpty());

    const Outcome ambiguous = runSpan(QStringLiteral("Равно 42 или всё же 42?"), QStringLiteral("42 => 43"));
    QCOMPARE(ambiguous.status, CheckStatus::Review);

    const Outcome anchored = runSpan(candidate, QStringLiteral("равно 42 => ровно 42"));
    QCOMPARE(anchored.status, CheckStatus::Fixed);
    QCOMPARE(anchored.text, QStringLiteral("Точное число ровно 42."));

    const Outcome noAnchor = runSpan(candidate, QStringLiteral("=> вставка"));
    QCOMPARE(noAnchor.status, CheckStatus::Review);

    const Outcome noEdits = runSpan(candidate, QStringLiteral("всё остальное верно"));
    QCOMPARE(noEdits.status, CheckStatus::Review);
}

void TestFixDiffGate::spanEquivalentNoopBecomesOk()
{
    const Outcome quotes = runSpan(QStringLiteral("Проект \"Альфа\" завершён."), QStringLiteral("\"Альфа\" => «Альфа»"));
    QCOMPARE(quotes.status, CheckStatus::Ok);
    QVERIFY(!quotes.downgradeReason.isEmpty());

    // A real correction after an equivalent one still lands, and the
    // equivalent edit does not rewrite the candidate's bytes.
    const Outcome mixed = runSpan(QStringLiteral("Проект \"Альфа\" равен 0,85."), QStringLiteral("\"Альфа\" => «Альфа»\n0,85 => 0,35"));
    QCOMPARE(mixed.status, CheckStatus::Fixed);
    QCOMPARE(mixed.text, QStringLiteral("Проект \"Альфа\" равен 0,35."));
}

void TestFixDiffGate::fullBlockWithDeclaredEditsIsAccepted()
{
    const QString candidate = QStringLiteral("В 2026 году коэффициент k = 0,85 был подтверждён в Таблице 3.");
    const QString body = QStringLiteral("В 2026 году коэффициент k = 0,35 был подтверждён в Таблице 3.\n"
                                        "EDITS:\n"
                                        "0,85 => 0,35");
    const Outcome outcome = runFull(candidate, body, QStringLiteral("text"));
    QCOMPARE(outcome.status, CheckStatus::Fixed);
    QCOMPARE(outcome.text, QStringLiteral("В 2026 году коэффициент k = 0,35 был подтверждён в Таблице 3."));
}

void TestFixDiffGate::fullBlockUndeclaredDriftBecomesReview()
{
    const QString candidate = QStringLiteral("В 2026 году коэффициент k = 0,85 был подтверждён в Таблице 3.");
    const QString body = QStringLiteral("В 2026 году коэффициент k = 0,35 был доказан в Таблице 3.\n"
                                        "EDITS:\n"
                                        "0,85 => 0,35");
    const Outcome outcome = runFull(candidate, body, QStringLiteral("text"));
    QCOMPARE(outcome.status, CheckStatus::Review);
    QVERIFY2(outcome.downgradeReason.text().contains(QStringLiteral("EDITS")), qPrintable(outcome.downgradeReason.text()));
}

void TestFixDiffGate::fullBlockWithoutEditsSectionBecomesReview()
{
    const Outcome missing = runFull(QStringLiteral("Старый текст."), QStringLiteral("Новый текст целиком."), QStringLiteral("text"));
    QCOMPARE(missing.status, CheckStatus::Review);

    const Outcome emptyBlock = runFull(QStringLiteral("Старый текст."), QStringLiteral("EDITS:\nСтарый => Новый"), QStringLiteral("text"));
    QCOMPARE(emptyBlock.status, CheckStatus::Review);

    const Outcome noEdits = runFull(QStringLiteral("Старый текст."), QStringLiteral("Новый текст.\nEDITS:\nбез стрелки"), QStringLiteral("text"));
    QCOMPARE(noEdits.status, CheckStatus::Review);
}

void TestFixDiffGate::fullBlockDeclaredFragmentsAreChecked()
{
    const QString candidate = QStringLiteral("Строка с числом 42.");
    const Outcome foreignOld = runFull(candidate, QStringLiteral("Строка с числом 43.\nEDITS:\nчисло 41 => число 43"), QStringLiteral("text"));
    QCOMPARE(foreignOld.status, CheckStatus::Review);

    const Outcome foreignNew = runFull(candidate, QStringLiteral("Строка с числом 43.\nEDITS:\nчисло 42 => число 44"), QStringLiteral("text"));
    QCOMPARE(foreignNew.status, CheckStatus::Review);
}

void TestFixDiffGate::fullBlockEquivalentReplyBecomesOk()
{
    const Outcome outcome = runFull(QStringLiteral("Проект «Альфа» завершён."), QStringLiteral("Проект \"Альфа\" завершён.\nEDITS:\n«Альфа\" => \"Альфа»"), QStringLiteral("text"));
    QCOMPARE(outcome.status, CheckStatus::Ok);
    QVERIFY(!outcome.downgradeReason.isEmpty());
}

void TestFixDiffGate::tableStructuralEditBypassesTheTokenDiff()
{
    // The row reshuffle changes almost every token; the structure declaration
    // plus the cell-level EDITS carry it.
    const QString candidate = QStringLiteral("<table><tr><td>1</td><td>2</td></tr><tr><td>3</td><td>4</td></tr></table>");
    const QString body = QStringLiteral("<table><tr><td>3</td><td>4</td></tr><tr><td>1</td><td>2</td></tr></table>\n"
                                        "EDITS:\n"
                                        "structure: rows 1 and 2 swapped");
    const Outcome outcome = runFull(candidate, body, QStringLiteral("table"));
    QCOMPARE(outcome.status, CheckStatus::Fixed);
}

void TestFixDiffGate::fullBlockTypesAreTheStructuralOnes()
{
    // Only tables answer the full-block contract: formulas drown it in
    // spacing churn, and the model prefers span style for them anyway.
    QVERIFY(isFullBlockType(QStringLiteral("table")));
    QVERIFY(!isFullBlockType(QStringLiteral("equation")));
    QVERIFY(!isFullBlockType(QStringLiteral("formula")));
    QVERIFY(!isFullBlockType(QStringLiteral("text")));
    QVERIFY(!isFullBlockType(QStringLiteral("code")));
    QVERIFY(!isFullBlockType(QStringLiteral("reference")));
}

void TestFixDiffGate::spanMathSpacingChurnIsSkipped()
{
    // The measured qwen3.5-4b behavior on MinerU-style candidates: a list of
    // pure LaTeX-spacing rewrites. None of them may touch the candidate.
    const QString candidate = QStringLiteral("\\[\\mathrm{Bi} _ {1} + \\alpha_ {2}\\]");
    const Outcome churn = runSpan(candidate,
                                  QStringLiteral("\\mathrm{Bi} _ {1} => \\mathrm{Bi}_1\n"
                                                 "\\alpha_ {2} => \\alpha_2"),
                                  QStringLiteral("equation"));
    QCOMPARE(churn.status, CheckStatus::Ok);

    // A real correction among the churn still lands, and only it.
    const Outcome real = runSpan(QStringLiteral("\\[\\iint_ {S} d S\\]"), QStringLiteral("\\iint_ {S} => \\iiint_ {S}"), QStringLiteral("equation"));
    QCOMPARE(real.status, CheckStatus::Fixed);
    QCOMPARE(real.text, QStringLiteral("\\[\\iiint_ {S} d S\\]"));
}

void TestFixDiffGate::spanFullBlockFallbackForFormulas()
{
    // The model answers a formula in full-block style on its own: an
    // equivalent rewrite reads as Ok, a differing block is the correction.
    const Outcome same = runSpan(QStringLiteral("\\[x = y\\]"), QStringLiteral("\\[ x = y \\]"), QStringLiteral("equation"));
    QCOMPARE(same.status, CheckStatus::Ok);

    const Outcome changed = runSpan(QStringLiteral("\\[x = y\\]"), QStringLiteral("\\[x = z\\]"), QStringLiteral("equation"));
    QCOMPARE(changed.status, CheckStatus::Fixed);
    QCOMPARE(changed.text, QStringLiteral("\\[x = z\\]"));
}

void TestFixDiffGate::spanAnchorMatchesThroughEquivalence()
{
    // Real qwen3.5-4b replies drop the carrier math delimiters and collapse
    // the doubled spaces of the candidate: the anchor must still land.
    const QString candidate = QStringLiteral("Модель \\( [10, 30] \\)  не видит текст.");
    const Outcome noop = runSpan(candidate, QStringLiteral("Модель [10, 30] не видит текст. => Модель [10, 30] не видит текст."));
    QCOMPARE(noop.status, CheckStatus::Ok);

    const Outcome fixed = runSpan(candidate, QStringLiteral("не видит текст => не читает текст"));
    QCOMPARE(fixed.status, CheckStatus::Fixed);
    QCOMPARE(fixed.text, QStringLiteral("Модель \\( [10, 30] \\)  не читает текст."));

    // A genuinely absent anchor still demotes.
    QCOMPARE(runSpan(candidate, QStringLiteral("Модель [10] => Модель [11]")).status, CheckStatus::Review);
}

void TestFixDiffGate::spanAnchorMatchesHyphenation()
{
    // The candidate wraps "перего-\nродок"; the model copies the joined form.
    const QString candidate = QStringLiteral("звукопоглощающих перего-\nродок в канале.");
    const Outcome outcome = runSpan(candidate, QStringLiteral("перего-родок => перегородки"));
    QCOMPARE(outcome.status, CheckStatus::Fixed);
    // The mapped span swallows the wrapped hyphen and line break: the joined
    // replacement lands whole.
    QCOMPARE(outcome.text, QStringLiteral("звукопоглощающих перегородки в канале."));
}

void TestFixDiffGate::fixNoneIsReviewed()
{
    // "FIX none" is not a verdict: the model declared no change instead of a
    // correction, so the block goes to human eyes.
    const Outcome outcome = runSpan(QStringLiteral("какой-то текст"), QStringLiteral("none"));
    QCOMPARE(outcome.status, CheckStatus::Review);
    QVERIFY(!outcome.downgradeReason.isEmpty());
}

void TestFixDiffGate::editsNoneDefersToTheEquivalenceCheck()
{
    const QString candidate = QStringLiteral("Проект «Альфа» завершён.");
    const Outcome same = runFull(candidate, QStringLiteral("Проект \"Альфа\" завершён.\nEDITS:\nnone"), QStringLiteral("equation"));
    QCOMPARE(same.status, CheckStatus::Ok);

    const Outcome changed = runFull(candidate, QStringLiteral("Проект «Бета» завершён.\nEDITS:\nnone"), QStringLiteral("equation"));
    QCOMPARE(changed.status, CheckStatus::Review);
}

void TestFixDiffGate::latexSpacingIsEquivalentInMathChunks()
{
    // "\tag {3}" vs "\tag{3}" renders the same: the whole reply is math
    // spacing churn, the candidate is kept.
    const QString candidate = QStringLiteral("\\tag {3} x = y");
    const QString body = QStringLiteral("\\tag{3} x = y\n"
                                        "EDITS:\n"
                                        "\\tag {3} => \\tag{3}");
    const Outcome outcome = runFull(candidate, body, QStringLiteral("table"));
    QCOMPARE(outcome.status, CheckStatus::Ok);
}

void TestFixDiffGate::rowOnlyCellPrefixIsALocation()
{
    // The wild writes "2: old => new", "1c5: - => -" and "1r1c1: old => new"
    // next to "r2c3:".
    const QString candidate = QStringLiteral("<fcel>Значение<fcel>5<nl>");
    const QString body = QStringLiteral("<fcel>Значение<fcel>6<nl>\n"
                                        "EDITS:\n"
                                        "1r1c2: 5 => 6");
    const Outcome outcome = runFull(candidate, body, QStringLiteral("table"));
    QVERIFY2(outcome.status == CheckStatus::Fixed, qPrintable(outcome.downgradeReason.text()));
}

void TestFixDiffGate::descriptiveNewSideSkipsTheContainmentCheck()
{
    const QString candidate = QStringLiteral("<fcel>a<fcel>5<fcel>лишнее<nl>");
    const QString body = QStringLiteral("<fcel>a<fcel>5<nl>\n"
                                        "EDITS:\n"
                                        "r1c3: лишнее => (removed extra cell)");
    const Outcome outcome = runFull(candidate, body, QStringLiteral("table"));
    QVERIFY2(outcome.status == CheckStatus::Fixed, qPrintable(outcome.downgradeReason.text()));
}

QTEST_MAIN(TestFixDiffGate)
#include "test_fix_diff_gate.moc"
