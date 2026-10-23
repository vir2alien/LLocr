#include <QtTest>

#include "models/FixDiffGate.h"

using namespace llocr;
using namespace llocr::FixDiffGate;

class TestFixDiffGate : public QObject
{
    Q_OBJECT

private slots:
    // The normalization mirrors the prompts' "Never fix" lists; strictly
    // different pairs (е/ё, homoglyphs) must survive it untouched.
    void normalizationCoversTheEquivalencePolicy();
    void codeKeepsItsWhitespace();
    // A re-wrapped line or a joined hyphenation is not a correction: the
    // candidate's own bytes stay.
    void reflowAndHyphenationAreEquivalent();
    void correctedBlockReplacesTheCandidate();
    void equivalentBlockKeepsTheCandidate();
    void emptyReplyIsReviewed();
    void noChangesWordIsReviewed();
    void latexSpacingIsEquivalent();
    void codeWhitespaceIsACorrection();
    void mathBlockTypesAreTheMathOnes();
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

void TestFixDiffGate::reflowAndHyphenationAreEquivalent()
{
    // The candidate wraps the word; the model joined it. The hyphen of a
    // line-end break is not content, and a re-wrapped paragraph is not a
    // correction either.
    const Outcome hyphen = fixBlock(QStringLiteral("звукопоглощающих перего-\nродок в канале."), QStringLiteral("звукопоглощающих перего-родок в канале."), QStringLiteral("text"));
    QCOMPARE(hyphen.status, CheckStatus::Ok);

    const Outcome reflow = fixBlock(QStringLiteral("Первая строка.\nВторая строка."), QStringLiteral("Первая строка. Вторая строка."), QStringLiteral("text"));
    QCOMPARE(reflow.status, CheckStatus::Ok);
}

void TestFixDiffGate::correctedBlockReplacesTheCandidate()
{
    const QString candidate = QStringLiteral("В 2026 году коэффициент k = 0,85 был подтверждён.");
    const Outcome outcome = fixBlock(candidate, QStringLiteral("В 2026 году коэффициент k = 0,35 был подтверждён."), QStringLiteral("text"));
    QCOMPARE(outcome.status, CheckStatus::Fixed);
    QVERIFY(outcome.downgradeReason.isEmpty());
    QCOMPARE(outcome.text, QStringLiteral("В 2026 году коэффициент k = 0,35 был подтверждён."));

    // A formula is corrected the same way — the reply is the whole block.
    const Outcome formula = fixBlock(QStringLiteral("\\[x = y\\]"), QStringLiteral("\\[x = z\\]"), QStringLiteral("equation"));
    QCOMPARE(formula.status, CheckStatus::Fixed);
    QCOMPARE(formula.text, QStringLiteral("\\[x = z\\]"));
}

void TestFixDiffGate::equivalentBlockKeepsTheCandidate()
{
    // The model re-spelled the block without changing anything: the
    // candidate's own bytes stay.
    const Outcome quotes = fixBlock(QStringLiteral("Проект «Альфа» завершён."), QStringLiteral("Проект \"Альфа\" завершён."), QStringLiteral("text"));
    QCOMPARE(quotes.status, CheckStatus::Ok);
    QVERIFY(!quotes.downgradeReason.isEmpty());
    QVERIFY(quotes.text.isEmpty());
}

void TestFixDiffGate::emptyReplyIsReviewed()
{
    const Outcome outcome = fixBlock(QStringLiteral("Старый текст."), QString(), QStringLiteral("text"));
    QCOMPARE(outcome.status, CheckStatus::Review);
    QVERIFY(!outcome.downgradeReason.isEmpty());
}

void TestFixDiffGate::noChangesWordIsReviewed()
{
    // "FIX none" is not a verdict: the model declared no change instead of a
    // correction, so the block goes to human eyes.
    for (const QString &answer : {QStringLiteral("none"), QStringLiteral("Nothing to change"), QStringLiteral("ничего")}) {
        const Outcome outcome = fixBlock(QStringLiteral("какой-то текст"), answer, QStringLiteral("text"));
        QCOMPARE(outcome.status, CheckStatus::Review);
        QVERIFY(!outcome.downgradeReason.isEmpty());
    }
}

void TestFixDiffGate::latexSpacingIsEquivalent()
{
    // "\tag {3}" vs "\tag{3}" renders the same: the whole reply is math
    // spacing churn, the candidate is kept.
    const Outcome outcome = fixBlock(QStringLiteral("\\tag {3} x = y"), QStringLiteral("\\tag{3} x = y"), QStringLiteral("equation"));
    QCOMPARE(outcome.status, CheckStatus::Ok);
}

void TestFixDiffGate::codeWhitespaceIsACorrection()
{
    // In code nothing is equivalent: the indentation is content.
    const Outcome outcome = fixBlock(QStringLiteral("if (a) {\n    b();\n}"), QStringLiteral("if (a) {\n  b();\n}"), QStringLiteral("code"));
    QCOMPARE(outcome.status, CheckStatus::Fixed);
    QCOMPARE(outcome.text, QStringLiteral("if (a) {\n  b();\n}"));
}

void TestFixDiffGate::mathBlockTypesAreTheMathOnes()
{
    QVERIFY(isMathBlockType(QStringLiteral("equation")));
    QVERIFY(isMathBlockType(QStringLiteral("formula")));
    QVERIFY(!isMathBlockType(QStringLiteral("text")));
    QVERIFY(!isMathBlockType(QStringLiteral("table")));
}

QTEST_MAIN(TestFixDiffGate)
#include "test_fix_diff_gate.moc"
