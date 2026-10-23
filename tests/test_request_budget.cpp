#include <QtTest>

#include "core/RequestBudget.h"

using namespace llocr;

// The generation budget of one request (ADR 193): the answer and the thinking
// inside the server's context window.
class TestRequestBudget : public QObject
{
    Q_OBJECT

private slots:
    void theRoomIsWhatTheContextLeaves();
    void theThinkingYieldsBeforeTheAnswer();
    void anUnknownContextClipsNothing();
    void thePromptEstimateCountsTheTextAndTheImage();
};

void TestRequestBudget::theRoomIsWhatTheContextLeaves()
{
    RequestBudget budget;
    budget.contextSize = 16384;
    budget.promptTokens = 2000;
    budget.answerTokens = 2048;
    budget.reasoningTokens = 1536;
    QCOMPARE(budget.room(), 14384);
    QCOMPARE(budget.effectiveAnswer(), 2048);
    QCOMPARE(budget.effectiveReasoning(), 1536);
    QCOMPARE(budget.maxTokens(), 3584);
}

void TestRequestBudget::theThinkingYieldsBeforeTheAnswer()
{
    // A tight context: the answer keeps its room, the thinking shrinks.
    RequestBudget budget;
    budget.contextSize = 5000;
    budget.promptTokens = 2000;
    budget.answerTokens = 2048;
    budget.reasoningTokens = 1536;
    QCOMPARE(budget.room(), 3000);
    QCOMPARE(budget.effectiveAnswer(), 2048);
    QCOMPARE(budget.effectiveReasoning(), 952);
    QCOMPARE(budget.maxTokens(), 3000);

    // A context that cannot hold the answer: the answer takes what is left,
    // the thinking gets nothing.
    budget.promptTokens = 4000;
    QCOMPARE(budget.effectiveAnswer(), 1000);
    QCOMPARE(budget.effectiveReasoning(), 0);
    QCOMPARE(budget.maxTokens(), 1000);

    // A prompt that fills the context: nothing is generated.
    budget.promptTokens = 6000;
    QCOMPARE(budget.room(), 0);
    QCOMPARE(budget.effectiveAnswer(), 0);
    QCOMPARE(budget.effectiveReasoning(), 0);
    QCOMPARE(budget.maxTokens(), 0);
}

void TestRequestBudget::anUnknownContextClipsNothing()
{
    // An external server: the app does not know its window and clips nothing.
    RequestBudget budget;
    budget.contextSize = 0;
    budget.promptTokens = 5000;
    budget.answerTokens = 2048;
    budget.reasoningTokens = 1536;
    QCOMPARE(budget.effectiveAnswer(), 2048);
    QCOMPARE(budget.effectiveReasoning(), 1536);
    QCOMPARE(budget.maxTokens(), 3584);
}

void TestRequestBudget::thePromptEstimateCountsTheTextAndTheImage()
{
    // The image sits at the model's token floor.
    QCOMPARE(estimatePromptTokens(QStringLiteral("текст"), QString(), QString(), 1024), 1024 + 2);

    // Latin text is counted at four characters per token, Cyrillic at two.
    const int latin = estimatePromptTokens(QStringLiteral("abcdefgh"), QString(), QString(), 0);
    QCOMPARE(latin, 2);
    const int cyrillic = estimatePromptTokens(QStringLiteral("абвгде"), QString(), QString(), 0);
    QCOMPARE(cyrillic, 3);

    // The estimate grows with the prompt.
    const QString candidate = QString(400, QChar(0x0410));  // 400 Cyrillic letters
    QCOMPARE(estimatePromptTokens(QString(), QString(), candidate, 0), 200);
}

QTEST_MAIN(TestRequestBudget)
#include "test_request_budget.moc"
