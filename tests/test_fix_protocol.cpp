#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "core/CheckRequest.h"
#include "models/FixModel.h"

using namespace llocr;

namespace {

// The parse/build pair is the whole fix protocol surface; expose them.
class ExposedFixModel : public FixModel
{
public:
    QByteArray build(const CheckRequest &request, const QByteArray &imageDataUrl) { return buildRequestBody(request, imageDataUrl); }
    CheckResult parse(const QByteArray &responseData, const QString &candidate = {}, const QString &blockType = {}, CheckRequest::FixProtocol protocol = CheckRequest::FixProtocol::FullBlock)
    {
        return parseResponse(responseData, candidate, blockType, protocol);
    }
};

QByteArray chatReply(const QString &content)
{
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", QJsonObject{{"content", content}}}}}}}).toJson(QJsonDocument::Compact);
}

// The server stopped at the token budget — the reply carries a fragment.
QByteArray chatReplyCut(const QString &content)
{
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", QStringLiteral("length")}, {"message", QJsonObject{{"content", content}}}}}}}).toJson(QJsonDocument::Compact);
}

CheckRequest fixRequest()
{
    CheckRequest request;
    request.image = QImage(4, 4, QImage::Format_RGB32);
    request.systemPrompt = QStringLiteral("Answer OK, FIX or REVIEW.");
    request.typePrompt = QStringLiteral("Block type: text.");
    request.recognizedText = QStringLiteral("The candidate text");
    request.blockType = QStringLiteral("text");
    request.modelId = QStringLiteral("qwen3.5-9b");
    request.task = CheckRequest::Task::Fix;
    return request;
}

// The read task: the same request with the protocol the action chose.
CheckRequest readRequest()
{
    CheckRequest request = fixRequest();
    request.fixProtocol = CheckRequest::FixProtocol::ReadBlock;
    return request;
}

}  // namespace

class TestFixProtocol : public QObject
{
    Q_OBJECT

private slots:
    // A FIX carries the complete corrected block; both documented spellings
    // of the keyword are accepted and nothing else is stripped.
    void fixCarriesTheCompleteBlock();
    // An explicit FIX without a payload is a refusal to judge, not an error.
    void emptyFixMeansReview();
    void okConfirmsWithoutText();
    void reviewMarksWithoutText();
    // EMPTY is no longer a verdict: a reply outside OK / FIX / REVIEW is a
    // deviation and goes to human eyes.
    void offSchemeReplyIsReviewed();
    void garbageIsReviewed();
    // A whole-reply Markdown fence is packaging: the protocol answer inside
    // is still parsed.
    void codeFenceAroundFix();
    void thinkingIsStripped();
    void emptyContentIsAnError();
    // The whole-block reply IS the contract now: it replaces the candidate.
    void fullBlockReplyIsApplied();
    // "FIX none" is not a verdict either: the model declared no change
    // instead of a correction, so the block needs a human look.
    void fixNoneIsReviewed();
    // A formula reply carries the complete formula.
    void formulaReplyIsAppliedAsTheCorrection();
    // A reply cut by the token budget is a fragment, not a correction.
    void truncatedReplyIsReviewed();
    // The fix request wraps the candidate in <CANDIDATE> and orders the
    // parts after the model's fix role (image-first for the shipped profiles).
    void requestBodyCarriesTheCandidate();
    void emptyCandidateStillTravelsFramed();
    // The reasoning budget rides the request and switches the thinking on.
    void reasoningBudgetTurnsTheThinkingOn();
    // The read protocol (ADR 259): the recognized text travels as a sample of
    // the document's notation, not as the candidate.
    void readRequestBodyCarriesTheRecognizedSample();
    // A block nobody recognized has no sample of the document's notation: the
    // reader gets the image alone.
    void readWithoutATextSendsTheImageAlone();
    // The reader has no verdict on the candidate: OK is outside its scheme.
    void readSchemeHasNoOk();
    void readFixCarriesTheBlock();
    void readReviewIsReviewed();
    // A block the reader reproduces as it is confirms the candidate.
    void readEquivalentBlockConfirmsWithoutText();
    void readReviewSpeaksInTheReadersWords();
    void readTruncatedReplyIsReviewed();
};

void TestFixProtocol::fixCarriesTheCompleteBlock()
{
    ExposedFixModel model;
    const QString candidate = QStringLiteral("Коэффициент k = 0,85 подтверждён.");

    const CheckResult colon = model.parse(chatReply(QStringLiteral("FIX: Коэффициент k = 0,35 подтверждён.")), candidate, QStringLiteral("text"));
    QCOMPARE(colon.status, CheckStatus::Fixed);
    QCOMPARE(colon.text, QStringLiteral("Коэффициент k = 0,35 подтверждён."));

    const CheckResult newline = model.parse(chatReply(QStringLiteral("FIX\nКоэффициент k = 0,35 подтверждён.")), candidate, QStringLiteral("text"));
    QCOMPARE(newline.status, CheckStatus::Fixed);
    QCOMPARE(newline.text, QStringLiteral("Коэффициент k = 0,35 подтверждён."));
}

void TestFixProtocol::emptyFixMeansReview()
{
    ExposedFixModel model;
    for (const QString &answer : {QStringLiteral("FIX"), QStringLiteral("FIX:"), QStringLiteral("fix \n")}) {
        const CheckResult result = model.parse(chatReply(answer), QStringLiteral("some text"), QStringLiteral("text"));
        QCOMPARE(result.status, CheckStatus::Review);
        QVERIFY(result.text.isEmpty());
    }
}

void TestFixProtocol::okConfirmsWithoutText()
{
    ExposedFixModel model;
    for (const QString &answer : {QStringLiteral("OK"), QStringLiteral("Ok"), QStringLiteral("OK — the block matches")}) {
        const CheckResult result = model.parse(chatReply(answer));
        QCOMPARE(result.status, CheckStatus::Ok);
        QVERIFY(result.text.isEmpty());
    }
}

void TestFixProtocol::reviewMarksWithoutText()
{
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("REVIEW")));
    QCOMPARE(result.status, CheckStatus::Review);
    QVERIFY(result.text.isEmpty());
}

void TestFixProtocol::offSchemeReplyIsReviewed()
{
    // EMPTY is gone from the scheme: it is now an ordinary deviation.
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("EMPTY")), QStringLiteral("Распознанный текст"), QStringLiteral("text"));
    QCOMPARE(result.status, CheckStatus::Review);
    QVERIFY2(!result.notes.isEmpty(), qPrintable(result.notes.text()));
}

void TestFixProtocol::garbageIsReviewed()
{
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("The block is fine, I think")));
    QCOMPARE(result.status, CheckStatus::Review);
    QVERIFY2(!result.notes.isEmpty(), qPrintable(result.notes.text()));
}

void TestFixProtocol::codeFenceAroundFix()
{
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("```\nFIX: k = 0,35 подтверждён.\n```")), QStringLiteral("k = 0,85 подтверждён."), QStringLiteral("text"));
    QCOMPARE(result.status, CheckStatus::Fixed);
    QCOMPARE(result.text, QStringLiteral("k = 0,35 подтверждён."));
}

void TestFixProtocol::thinkingIsStripped()
{
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("FIX: k = 0,35 подтверждён.\n thinking what to change response")), QStringLiteral("k = 0,85 подтверждён."), QStringLiteral("text"));
    QCOMPARE(result.status, CheckStatus::Fixed);
    QCOMPARE(result.text, QStringLiteral("k = 0,35 подтверждён."));
}

void TestFixProtocol::emptyContentIsAnError()
{
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QString()));
    QCOMPARE(result.status, CheckStatus::Failed);
    QVERIFY(!result.errorMessage.text().isEmpty());
}

void TestFixProtocol::fullBlockReplyIsApplied()
{
    // The reply carries the complete corrected block — that is the contract
    // now, for every block type.
    ExposedFixModel model;
    const CheckResult result =
        model.parse(chatReply(QStringLiteral("FIX\nВ 2026 году коэффициент k = 0,35 был подтверждён.")), QStringLiteral("В 2026 году коэффициент k = 0,85 был подтверждён."), QStringLiteral("text"));
    QCOMPARE(result.status, CheckStatus::Fixed);
    QCOMPARE(result.text, QStringLiteral("В 2026 году коэффициент k = 0,35 был подтверждён."));
}

void TestFixProtocol::fixNoneIsReviewed()
{
    ExposedFixModel model;
    for (const QString &answer : {QStringLiteral("FIX\nnone"), QStringLiteral("FIX: Nothing to change."), QStringLiteral("FIX\nничего")}) {
        const CheckResult result = model.parse(chatReply(answer), QStringLiteral("какой-то текст"), QStringLiteral("text"));
        QCOMPARE(result.status, CheckStatus::Review);
        QVERIFY(!result.notes.isEmpty());
    }
}

void TestFixProtocol::formulaReplyIsAppliedAsTheCorrection()
{
    // The formula prompt asks for the complete formula, so a formula reply is
    // a full block.
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("FIX\n\\[\\mathrm{Bi}_{1} = 2\\]")), QStringLiteral("$$\n\\mathrm{Bi}_{1} = 1\n$$"), QStringLiteral("equation"));
    QCOMPARE(result.status, CheckStatus::Fixed);
    QCOMPARE(result.text, QStringLiteral("\\[\\mathrm{Bi}_{1} = 2\\]"));
}

void TestFixProtocol::truncatedReplyIsReviewed()
{
    // The server stopped at the token budget: the reply is a fragment of the
    // block, and a fragment is not a correction.
    ExposedFixModel model;
    const CheckResult result =
        model.parse(chatReplyCut(QStringLiteral("FIX\nВ 2026 году коэффициент k = 0,35 был подтв")), QStringLiteral("В 2026 году коэффициент k = 0,85 был подтверждён."), QStringLiteral("text"));
    QCOMPARE(result.status, CheckStatus::Review);
    QVERIFY(result.text.isEmpty());
    QVERIFY2(!result.notes.isEmpty(), qPrintable(result.notes.text()));
}

void TestFixProtocol::requestBodyCarriesTheCandidate()
{
    ExposedFixModel model;
    const QJsonObject body = QJsonDocument::fromJson(model.build(fixRequest(), QByteArrayLiteral("data:image/png;base64,AAAA"))).object();

    QCOMPARE(body.value("model").toString(), QStringLiteral("qwen3.5-9b"));
    const QJsonArray messages = body.value("messages").toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).toObject().value("role").toString(), QStringLiteral("system"));
    QCOMPARE(messages.at(0).toObject().value("content").toString(), QStringLiteral("Answer OK, FIX or REVIEW."));

    const QJsonArray content = messages.at(1).toObject().value("content").toArray();
    QCOMPARE(content.size(), 3);
    // The shipped fix roles train image-first: prompt, image, candidate.
    QCOMPARE(content.at(0).toObject().value("text").toString(), QStringLiteral("Block type: text."));
    QCOMPARE(content.at(1).toObject().value("image_url").toObject().value("url").toString(), QStringLiteral("data:image/png;base64,AAAA"));
    const QString candidate = content.at(2).toObject().value("text").toString();
    QVERIFY2(candidate.contains(QStringLiteral("<CANDIDATE>")), qPrintable(candidate));
    QVERIFY2(candidate.contains(QStringLiteral("The candidate text")), qPrintable(candidate));
}

void TestFixProtocol::emptyCandidateStillTravelsFramed()
{
    // The candidate-free transcription contract lives in
    // BlockRecognitionModel; the fix wire always carries the framing, even
    // when the candidate text is empty — the empty-payload handling is the
    // parser's verdict, not a wire shape.
    ExposedFixModel model;
    CheckRequest request = fixRequest();
    request.recognizedText.clear();
    const QByteArray body = model.build(request, QByteArrayLiteral("data:image/png;base64,AAAA"));
    QVERIFY(body.contains("CANDIDATE"));
    const QJsonArray content = QJsonDocument::fromJson(body).object().value("messages").toArray().at(1).toObject().value("content").toArray();
    QCOMPARE(content.size(), 3);
}

void TestFixProtocol::reasoningBudgetTurnsTheThinkingOn()
{
    // The budget rides the request: llama.cpp reads reasoning_budget_tokens
    // per request, and the budget is also the switch — a budget above zero
    // asks the model to think first (ADR 193).
    ExposedFixModel model;
    CheckRequest request = fixRequest();

    QJsonObject body = QJsonDocument::fromJson(model.build(request, QByteArrayLiteral("data:image/png;base64,AAAA"))).object();
    QVERIFY(!body.contains("reasoning_budget_tokens"));
    QCOMPARE(body.value("chat_template_kwargs").toObject().value("enable_thinking").toBool(), false);

    RequestParameter budget;
    budget.name = QLatin1String(kReasoningBudgetParameter);
    budget.kind = RequestValueKind::Number;
    budget.value = QVariant(1024.0);
    request.parameters.append(budget);
    RequestParameter maxTokens;
    maxTokens.name = QStringLiteral("max_tokens");
    maxTokens.kind = RequestValueKind::Number;
    maxTokens.value = QVariant(2048.0);
    request.parameters.append(maxTokens);

    body = QJsonDocument::fromJson(model.build(request, QByteArrayLiteral("data:image/png;base64,AAAA"))).object();
    QCOMPARE(body.value("reasoning_budget_tokens").toDouble(), 1024.0);
    QCOMPARE(body.value("chat_template_kwargs").toObject().value("enable_thinking").toBool(), true);
    // The transport follows the budget the task computed: the generation
    // budget is the task's own (the answer plus the thinking inside the
    // context window), not the transport's business.
    QCOMPARE(body.value("max_tokens").toDouble(), 2048.0);

    // A budget of zero is the default: the reply is the answer.
    request.parameters[0].value = QVariant(0.0);
    body = QJsonDocument::fromJson(model.build(request, QByteArrayLiteral("data:image/png;base64,AAAA"))).object();
    QCOMPARE(body.value("reasoning_budget_tokens").toDouble(), 0.0);
    QCOMPARE(body.value("chat_template_kwargs").toObject().value("enable_thinking").toBool(), false);
    QCOMPARE(body.value("max_tokens").toDouble(), 2048.0);
}

void TestFixProtocol::readRequestBodyCarriesTheRecognizedSample()
{
    // The reader takes the recognized text as a sample of how this document
    // writes, not as the answer: the framing and the wording say so.
    ExposedFixModel model;
    const QJsonObject body = QJsonDocument::fromJson(model.build(readRequest(), QByteArrayLiteral("data:image/png;base64,AAAA"))).object();
    const QJsonArray content = body.value("messages").toArray().at(1).toObject().value("content").toArray();
    QCOMPARE(content.size(), 3);
    const QString sample = content.at(2).toObject().value("text").toString();
    QVERIFY2(sample.contains(QStringLiteral("<RECOGNIZED>")), qPrintable(sample));
    QVERIFY2(sample.contains(QStringLiteral("</RECOGNIZED>")), qPrintable(sample));
    QVERIFY2(sample.contains(QStringLiteral("The candidate text")), qPrintable(sample));
    QVERIFY2(sample.contains(QStringLiteral("a sample of the document's notation")), qPrintable(sample));
    QVERIFY2(!sample.contains(QStringLiteral("<CANDIDATE>")), qPrintable(sample));
}

void TestFixProtocol::readWithoutATextSendsTheImageAlone()
{
    // A block nobody recognized yet has no sample of the document's notation
    // to show: the reader gets the image alone (ADR 260).
    ExposedFixModel model;
    CheckRequest request = readRequest();
    request.recognizedText.clear();
    const QJsonObject body = QJsonDocument::fromJson(model.build(request, QByteArrayLiteral("data:image/png;base64,AAAA"))).object();
    const QJsonArray content = body.value("messages").toArray().at(1).toObject().value("content").toArray();
    // The type prompt and the image — no sample.
    QCOMPARE(content.size(), 2);
    const QByteArray wire = QJsonDocument(content).toJson();
    QVERIFY2(!wire.contains("<RECOGNIZED>"), wire.constData());
}

void TestFixProtocol::readSchemeHasNoOk()
{
    // The reader writes the block from the image; it has no verdict on the
    // candidate, so OK is outside its scheme (ADR 259).
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("OK")), QStringLiteral("The candidate text"), QStringLiteral("text"), CheckRequest::FixProtocol::ReadBlock);
    QCOMPARE(result.status, CheckStatus::Review);
    QVERIFY2(!result.notes.isEmpty(), qPrintable(result.notes.text()));
    QVERIFY2(result.notes.text().contains(QStringLiteral("FIX/REVIEW")), qPrintable(result.notes.text()));
}

void TestFixProtocol::readFixCarriesTheBlock()
{
    ExposedFixModel model;
    const CheckResult result = model.parse(
        chatReply(QStringLiteral("FIX\nКоэффициент k = 0,35 подтверждён.")), QStringLiteral("Коэффициент k = 0,85 подтверждён."), QStringLiteral("text"), CheckRequest::FixProtocol::ReadBlock);
    QCOMPARE(result.status, CheckStatus::Fixed);
    QCOMPARE(result.text, QStringLiteral("Коэффициент k = 0,35 подтверждён."));
}

void TestFixProtocol::readReviewIsReviewed()
{
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("REVIEW")), QStringLiteral("The candidate text"), QStringLiteral("text"), CheckRequest::FixProtocol::ReadBlock);
    QCOMPARE(result.status, CheckStatus::Review);
    QVERIFY(result.text.isEmpty());
}

void TestFixProtocol::readEquivalentBlockConfirmsWithoutText()
{
    // The gate judges the read block against the current text: a block that
    // says the same thing confirms the candidate and keeps its bytes.
    ExposedFixModel model;
    const CheckResult result = model.parse(
        chatReply(QStringLiteral("FIX\nКоэффициент k = 0,85 подтверждён.")), QStringLiteral("Коэффициент k = 0,85 подтверждён."), QStringLiteral("text"), CheckRequest::FixProtocol::ReadBlock);
    QCOMPARE(result.status, CheckStatus::Ok);
    QVERIFY(result.text.isEmpty());
    // The reader explains the outcome in its own words: the fixer's note
    // speaks about a candidate and a correction (ADR 260).
    QVERIFY(!result.notes.text().isEmpty());
    QVERIFY(!result.notes.text().contains(QStringLiteral("candidate"), Qt::CaseInsensitive));
    QVERIFY(result.notes.text().contains(QStringLiteral("read"), Qt::CaseInsensitive));
}

void TestFixProtocol::readReviewSpeaksInTheReadersWords()
{
    // A block the reader could not read is the reader's own outcome: the
    // panel and the status line must not borrow the fixer's words.
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("REVIEW")), QStringLiteral("Коэффициент k = 0,85 подтверждён."), QStringLiteral("text"), CheckRequest::FixProtocol::ReadBlock);
    QCOMPARE(result.status, CheckStatus::Review);
    QVERIFY(!result.notes.text().isEmpty());
    QVERIFY(result.notes.text().contains(QStringLiteral("read"), Qt::CaseInsensitive));
}

void TestFixProtocol::readTruncatedReplyIsReviewed()
{
    // The reader's answer is the whole block: a cut one is a fragment.
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReplyCut(QStringLiteral("FIX\nВ 2026 году коэффициент k = 0,35 был подтв")),
                                           QStringLiteral("В 2026 году коэффициент k = 0,85 был подтверждён."),
                                           QStringLiteral("text"),
                                           CheckRequest::FixProtocol::ReadBlock);
    QCOMPARE(result.status, CheckStatus::Review);
    QVERIFY(result.text.isEmpty());
    QVERIFY2(!result.notes.isEmpty(), qPrintable(result.notes.text()));
}

QTEST_MAIN(TestFixProtocol)
#include "test_fix_protocol.moc"
