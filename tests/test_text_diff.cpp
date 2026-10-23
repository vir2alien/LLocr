#include <QtTest>

#include "core/TextDiff.h"

using namespace llocr;

namespace {

QString joined(const QVector<DiffRange> &ranges, const QString &text)
{
    QStringList parts;
    for (const DiffRange &range : ranges)
        parts << text.mid(range.start, range.length);
    return parts.join(QLatin1Char('|'));
}

}  // namespace

// What a correction changed, word by word (ADR 186): the fragments the fix
// dropped from the original and the fragments it put in.
class TestTextDiff : public QObject
{
    Q_OBJECT

private slots:
    void identicalTextsHaveNoChanges()
    {
        const TextDiff diff = diffText(QStringLiteral("same text"), QStringLiteral("same text"));
        QVERIFY(diff.removed.isEmpty());
        QVERIFY(diff.added.isEmpty());
    }

    void aChangedWordIsOneRangeOnEachSide()
    {
        const QString before = QStringLiteral("The quick brown fox");
        const QString after = QStringLiteral("The slow brown fox");
        const TextDiff diff = diffText(before, after);

        QCOMPARE(joined(diff.removed, before), QStringLiteral("quick"));
        QCOMPARE(joined(diff.added, after), QStringLiteral("slow"));
    }

    // The case the panel exists for: a formula rewritten in one place.
    void aFormulaFragmentIsOneRange()
    {
        const QString before = QStringLiteral("\\frac{\\mathrm{T}}{\\mathrm{T}_{\\mathrm{H.BPp}}}");
        const QString after = QStringLiteral("\\frac{\\mathrm{T}}{\\mathrm{T}_{\\mathrm{н.впр}}}");
        const TextDiff diff = diffText(before, after);

        QCOMPARE(joined(diff.removed, before), QStringLiteral("H.BPp"));
        QCOMPARE(joined(diff.added, after), QStringLiteral("н.впр"));
    }

    // Two words that go away read as one hole, not as two.
    void neighbouringRemovedWordsJoin()
    {
        const QString before = QStringLiteral("alpha beta gamma");
        const QString after = QStringLiteral("gamma");
        const TextDiff diff = diffText(before, after);

        QCOMPARE(joined(diff.removed, before), QStringLiteral("alpha beta"));
        QVERIFY(diff.added.isEmpty());
    }

    void anEmptySideRemovesEverything()
    {
        const QString before = QStringLiteral("only text");
        const TextDiff diff = diffText(before, QString());

        QCOMPARE(joined(diff.removed, before), QStringLiteral("only text"));
        QVERIFY(diff.added.isEmpty());
    }

    // The ranges must address the original text: the highlighted fragment has
    // to be the one the user sees in the panel.
    void rangesPointAtTheOriginalText()
    {
        const QString before = QStringLiteral("one two three");
        const QString after = QStringLiteral("one 2 three");
        const TextDiff diff = diffText(before, after);

        QCOMPARE(diff.removed.size(), 1);
        QCOMPARE(before.mid(diff.removed.first().start, diff.removed.first().length), QStringLiteral("two"));
        QCOMPARE(diff.added.size(), 1);
        QCOMPARE(after.mid(diff.added.first().start, diff.added.first().length), QStringLiteral("2"));
    }
};

QTEST_MAIN(TestTextDiff)
#include "test_text_diff.moc"
