#include <QtTest>

#include "core/LatexText.h"

using namespace llocr;
using namespace llocr::LatexText;

// The formula text: the canonical form the block carries from the moment it
// is recognized, and the syntax problems that reject it (ADR 194).
class TestLatexText : public QObject
{
    Q_OBJECT

private slots:
    void mathLabelsAreTheFormulaOnes();
    void canonicalFormRemovesTheModelSpacing();
    void soundFormulasAreAccepted();
    void unclosedBraceIsRejected();
    void strayBraceIsRejected();
    void environmentsArePaired();
    void leftAndRightArePaired();
    void delimitersAreClosed();
    void truncatedCommandIsRejected();
    void missingArgumentIsRejected();
    void aLoneDollarIsNotADelimiter();
    void aMarkdownFenceIsCarrierNotFormula();
    void theInvisibleDelimiterPairsWithAnyBracket();
    void aFragmentedFormulaIsNotASyntaxProblem();
};

// The labels the OCR models use for a formula.
void TestLatexText::mathLabelsAreTheFormulaOnes()
{
    QVERIFY(isMathLabel(QStringLiteral("equation")));
    QVERIFY(isMathLabel(QStringLiteral("formula")));
    QVERIFY(isMathLabel(QStringLiteral("equation_block")));
    QVERIFY(!isMathLabel(QStringLiteral("text")));
    QVERIFY(!isMathLabel(QStringLiteral("table")));
    QVERIFY(!isMathLabel(QStringLiteral("equation_number")));
}

// The OCR models write spaced LaTeX; the canonical form is what the block
// carries from now on.
void TestLatexText::canonicalFormRemovesTheModelSpacing()
{
    QCOMPARE(canonical(QStringLiteral("Bi _ {1}")), QStringLiteral("Bi _{1}"));
    QCOMPARE(canonical(QStringLiteral("\\frac {1} {2}")), QStringLiteral("\\frac{1}{2}"));
    QCOMPARE(canonical(QStringLiteral("\\tag {3}")), QStringLiteral("\\tag{3}"));
    QCOMPARE(canonical(QStringLiteral("\\left( x \\right)")), QStringLiteral("\\left(x \\right)"));
}

// A formula the app cannot fault must not be rejected.
void TestLatexText::soundFormulasAreAccepted()
{
    QCOMPARE(check(QStringLiteral("x = 1")), Problem::None);
    QCOMPARE(check(QStringLiteral("$$x = 1$$")), Problem::None);
    QCOMPARE(check(QStringLiteral("$x$")), Problem::None);
    QCOMPARE(check(QStringLiteral("\\frac{1}{2}")), Problem::None);
    QCOMPARE(check(QStringLiteral("\\frac12")), Problem::None);
    QCOMPARE(check(QStringLiteral("\\sqrt[3]{x}")), Problem::None);
    QCOMPARE(check(QStringLiteral("\\left( x \\right)")), Problem::None);
    QCOMPARE(check(QStringLiteral("\\mathrm{Bi} _ {1}")), Problem::None);
    QCOMPARE(check(QStringLiteral("\\begin{aligned}a &= b \\\\ c &= d\\end{aligned}")), Problem::None);
    QCOMPARE(check(QStringLiteral("\\{x\\}")), Problem::None);
    QCOMPARE(check(QString()), Problem::None);
}

void TestLatexText::unclosedBraceIsRejected()
{
    QCOMPARE(check(QStringLiteral("x = {1")), Problem::UnclosedBrace);
    QCOMPARE(check(QStringLiteral("\\frac{1{2}")), Problem::UnclosedBrace);
}

void TestLatexText::strayBraceIsRejected()
{
    QCOMPARE(check(QStringLiteral("x = 1}")), Problem::ExtraBrace);
}

void TestLatexText::environmentsArePaired()
{
    QCOMPARE(check(QStringLiteral("\\begin{aligned}x &= y")), Problem::UnclosedEnvironment);
    QCOMPARE(check(QStringLiteral("\\begin{aligned}x &= y\\end{array}")), Problem::MismatchedEnvironment);
    QCOMPARE(check(QStringLiteral("\\end{aligned}")), Problem::MismatchedEnvironment);
}

void TestLatexText::leftAndRightArePaired()
{
    QCOMPARE(check(QStringLiteral("\\left( x")), Problem::UnbalancedLeftRight);
    QCOMPARE(check(QStringLiteral("\\left( x \\right]")), Problem::UnbalancedLeftRight);
    QCOMPARE(check(QStringLiteral("x \\right)")), Problem::UnbalancedLeftRight);
    // The pairs LaTeX accepts: "(" with ")", "{" with "}".
    QCOMPARE(check(QStringLiteral("\\left( x \\right)")), Problem::None);
    QCOMPARE(check(QStringLiteral("\\left\\{ x \\right\\}")), Problem::None);
}

void TestLatexText::delimitersAreClosed()
{
    QCOMPARE(check(QStringLiteral("$$x = 1")), Problem::UnbalancedDelimiter);
    QCOMPARE(check(QStringLiteral("\\[x = 1")), Problem::UnbalancedDelimiter);
    QCOMPARE(check(QStringLiteral("\\(x = 1")), Problem::UnbalancedDelimiter);
}

void TestLatexText::truncatedCommandIsRejected()
{
    QCOMPARE(check(QStringLiteral("x = \\alpha\\")), Problem::TruncatedCommand);
}

void TestLatexText::missingArgumentIsRejected()
{
    QCOMPARE(check(QStringLiteral("\\frac{1}")), Problem::MissingArgument);
    QCOMPARE(check(QStringLiteral("\\frac 1")), Problem::MissingArgument);
}

// A dollar sign that closes nothing and starts nothing is a currency symbol,
// not a delimiter: a formula must not be rejected over it.
void TestLatexText::aLoneDollarIsNotADelimiter()
{
    QCOMPARE(check(QStringLiteral("100$")), Problem::None);
    QCOMPARE(check(QStringLiteral("x = 5$")), Problem::None);
}

// The model answers a formula in a markdown code fence — sometimes in several
// pieces and sometimes inside the math wrapper. The fence is carrier markup:
// it is unwrapped, the LaTeX inside is kept.
void TestLatexText::aMarkdownFenceIsCarrierNotFormula()
{
    const QString fenced = QStringLiteral(R"LL($$
```latex
K_{\ell} = \frac{1}{R^*} =
```

```latex
= \frac{1}{\frac{1}{\alpha_1 R_1} + \frac{1}{\lambda_w} \ln \frac{R_2}{R_1}}
```

(8.36)
$$)LL");

    const QString canonicalText = canonical(fenced);
    QVERIFY(!canonicalText.contains(QStringLiteral("```")));
    QVERIFY(canonicalText.contains(QStringLiteral("K_{\\ell}")));
    QVERIFY(canonicalText.contains(QStringLiteral("\\frac{1}{R^*}")));
    QVERIFY(canonicalText.contains(QStringLiteral("(8.36)")));
    QCOMPARE(check(canonicalText), Problem::None);

    // A fence that could not be unwrapped stays a defect: a backtick is
    // markdown, not LaTeX.
    QCOMPARE(check(QStringLiteral("$$x = 1`")), Problem::CodeMarkup);
    QCOMPARE(check(QStringLiteral("`x`")), Problem::CodeMarkup);
}

// A bracket split across the lines of a formula: the invisible delimiter
// closes any opening one and is closed by any of them. This is how a
// multi-line formula carries a bracket that spans two lines.
void TestLatexText::theInvisibleDelimiterPairsWithAnyBracket()
{
    QCOMPARE(check(QStringLiteral("\\left[ a \\right. \\\\ \\left. b \\right]")), Problem::None);
    QCOMPARE(check(QStringLiteral("\\left( a \\right.")), Problem::None);
    // A closing delimiter without an opening one is still a defect.
    QCOMPARE(check(QStringLiteral("a \\right)")), Problem::UnbalancedLeftRight);
}

// A formula the model split into display-math fragments is not broken: the
// "$$" are carriers, and they are balanced.
void TestLatexText::aFragmentedFormulaIsNotASyntaxProblem()
{
    QCOMPARE(check(QStringLiteral("a =\n$$\n= b\n$$\n\n$$\n- c\n$$\n\n(63a)")), Problem::None);
}

QTEST_MAIN(TestLatexText)
#include "test_latex_text.moc"
