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

CheckRequest fixRequest()
{
    CheckRequest request;
    request.image = QImage(4, 4, QImage::Format_RGB32);
    request.systemPrompt = QStringLiteral("Answer OK, FIX, REVIEW or EMPTY.");
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
    // A span FIX edits the candidate; both documented spellings of the
    // keyword are accepted and nothing else is stripped.
    void fixWithColonAndNewline();
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
    // A legacy full-block reply on a span type lists no `old => new` lines:
    // the gate sends it to review instead of trusting a re-transcription.
    void legacyFullBlockReplyIsDemotedToReview();
    // "FIX none" is not a verdict either: the model declared no change
    // instead of a correction, so the block needs a human look.
    void fixNoneIsReviewed();
    // The structural types read the full-block + EDITS: contract instead.
    void fullBlockTypeParsesTheEditsSection();
    // A formula reply carries the complete formula (no EDITS: section).
    void formulaReplyIsAppliedAsTheCorrection();
    // The fix request wraps the candidate in <CANDIDATE> and orders the
    // parts after the model's fix role (image-first for the shipped profiles).
    void requestBodyCarriesTheCandidate();
    void emptyCandidateStillTravelsFramed();
};

void TestFixProtocol::fixWithColonAndNewline()
{
    ExposedFixModel model;
    const QString candidate = QStringLiteral("Коэффициент k = 0,85 подтверждён.");

    const CheckResult colon = model.parse(chatReply(QStringLiteral("FIX: k = 0,85 => k = 0,35")), candidate, QStringLiteral("text"));
    QCOMPARE(colon.status, CheckStatus::Fixed);
    QCOMPARE(colon.text, QStringLiteral("Коэффициент k = 0,35 подтверждён."));

    const CheckResult newline = model.parse(chatReply(QStringLiteral("FIX\nk = 0,85 => k = 0,35")), candidate, QStringLiteral("text"));
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
    const CheckResult result = model.parse(chatReply(QStringLiteral("```\nFIX: k = 0,85 => k = 0,35\n```")), QStringLiteral("k = 0,85 подтверждён."), QStringLiteral("text"));
    QCOMPARE(result.status, CheckStatus::Fixed);
    QCOMPARE(result.text, QStringLiteral("k = 0,35 подтверждён."));
}

void TestFixProtocol::thinkingIsStripped()
{
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("FIX: k = 0,85 => k = 0,35\n thinking what to change response")), QStringLiteral("k = 0,85 подтверждён."), QStringLiteral("text"));
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

void TestFixProtocol::legacyFullBlockReplyIsDemotedToReview()
{
    // A model still on the old contract re-transcribes the block; without an
    // `old => new` line the gate refuses the reply — the candidate survives.
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("FIX\nThe complete corrected text")), QStringLiteral("The candidate text"), QStringLiteral("text"));
    QCOMPARE(result.status, CheckStatus::Review);
    QVERIFY(result.text.isEmpty());
    QVERIFY2(!result.notes.isEmpty(), qPrintable(result.notes.text()));
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
    // a full block without an EDITS: section.
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("FIX\n\\[\\mathrm{Bi}_{1} = 2\\]")), QStringLiteral("$$\n\\mathrm{Bi}_{1} = 1\n$$"), QStringLiteral("equation"));
    QCOMPARE(result.status, CheckStatus::Fixed);
    QCOMPARE(result.text, QStringLiteral("\\[\\mathrm{Bi}_{1} = 2\\]"));
}

void TestFixProtocol::fullBlockTypeParsesTheEditsSection()
{
    ExposedFixModel model;
    const QString reply = QStringLiteral("FIX\nВ 2026 году коэффициент k = 0,35 был подтверждён.\n"
                                         "EDITS:\n"
                                         "0,85 => 0,35");
    // Tables keep the full-block contract; formulas answer in span style.
    const CheckResult result = model.parse(chatReply(reply), QStringLiteral("В 2026 году коэффициент k = 0,85 был подтверждён."), QStringLiteral("table"));
    QCOMPARE(result.status, CheckStatus::Fixed);
    QCOMPARE(result.text, QStringLiteral("В 2026 году коэффициент k = 0,35 был подтверждён."));
}

void TestFixProtocol::requestBodyCarriesTheCandidate()
{
    ExposedFixModel model;
    const QJsonObject body = QJsonDocument::fromJson(model.build(fixRequest(), QByteArrayLiteral("data:image/png;base64,AAAA"))).object();

    QCOMPARE(body.value("model").toString(), QStringLiteral("qwen3.5-9b"));
    const QJsonArray messages = body.value("messages").toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).toObject().value("role").toString(), QStringLiteral("system"));
    QCOMPARE(messages.at(0).toObject().value("content").toString(), QStringLiteral("Answer OK, FIX, REVIEW or EMPTY."));

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
