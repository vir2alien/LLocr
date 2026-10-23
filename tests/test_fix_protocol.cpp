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
    CheckResult parse(const QByteArray &responseData, const QString &candidate = {}, const QString &blockType = {}) { return parseResponse(responseData, candidate, blockType); }
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

QTEST_MAIN(TestFixProtocol)
#include "test_fix_protocol.moc"
