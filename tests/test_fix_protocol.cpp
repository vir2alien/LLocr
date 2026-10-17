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
    CheckResult parse(const QByteArray &responseData) { return parseResponse(responseData); }
};

QByteArray chatReply(const QString &content)
{
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", QJsonObject{{"content", content}}}}}}}).toJson(QJsonDocument::Compact);
}

CheckRequest fixRequest()
{
    CheckRequest request;
    request.image = QImage(4, 4, QImage::Format_RGB32);
    request.systemPrompt = QStringLiteral("Answer OK, FIX or REVIEW.");
    request.typePrompt = QStringLiteral("Block type: text.");
    request.recognizedText = QStringLiteral("The candidate text");
    request.modelId = QStringLiteral("qwen3.5-9b");
    request.task = CheckRequest::Task::Fix;
    return request;
}

}  // namespace

class TestFixProtocol : public QObject
{
    Q_OBJECT

private slots:
    // The parser accepts both documented FIX spellings and strips nothing
    // from the payload but the keyword and its separator.
    void fixWithColonAndNewline();
    // An explicit FIX without a payload is a refusal to judge, not an error.
    void emptyFixMeansReview();
    void okConfirmsWithoutText();
    void reviewMarksWithoutText();
    void garbageIsAnError();
    // A whole-reply Markdown fence is packaging: the protocol answer inside
    // is still parsed.
    void codeFenceAroundFix();
    void thinkingIsStripped();
    void emptyContentIsAnError();
    // The fix request wraps the candidate in <ocr_candidate> and orders the
    // parts after the model's fix role (image-first for the shipped profiles).
    void requestBodyCarriesTheCandidate();
    void emptyCandidateStillTravelsFramed();
};

void TestFixProtocol::fixWithColonAndNewline()
{
    ExposedFixModel model;

    const CheckResult colon = model.parse(chatReply(QStringLiteral("FIX: The corrected text")));
    QCOMPARE(colon.status, CheckStatus::Fixed);
    QCOMPARE(colon.text, QStringLiteral("The corrected text"));

    const CheckResult newline = model.parse(chatReply(QStringLiteral("FIX\nThe corrected\ntext")));
    QCOMPARE(newline.status, CheckStatus::Fixed);
    QCOMPARE(newline.text, QStringLiteral("The corrected\ntext"));
}

void TestFixProtocol::emptyFixMeansReview()
{
    ExposedFixModel model;
    for (const QString &answer : {QStringLiteral("FIX"), QStringLiteral("FIX:"), QStringLiteral("fix \n")}) {
        const CheckResult result = model.parse(chatReply(answer));
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

void TestFixProtocol::garbageIsAnError()
{
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("The block is fine, I think")));
    QCOMPARE(result.status, CheckStatus::Failed);
    QVERIFY2(result.errorMessage.text().contains(QStringLiteral("OK, FIX or REVIEW")), qPrintable(result.errorMessage.text()));
}

void TestFixProtocol::codeFenceAroundFix()
{
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("```\nFIX: The corrected text\n```")));
    QCOMPARE(result.status, CheckStatus::Fixed);
    QCOMPARE(result.text, QStringLiteral("The corrected text"));
}

void TestFixProtocol::thinkingIsStripped()
{
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QStringLiteral("FIX: The corrected text\n thinking what to change response")));
    QCOMPARE(result.status, CheckStatus::Fixed);
    QCOMPARE(result.text, QStringLiteral("The corrected text"));
}

void TestFixProtocol::emptyContentIsAnError()
{
    ExposedFixModel model;
    const CheckResult result = model.parse(chatReply(QString()));
    QCOMPARE(result.status, CheckStatus::Failed);
    QVERIFY(!result.errorMessage.text().isEmpty());
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
    QVERIFY2(candidate.contains(QStringLiteral("<ocr_candidate>")), qPrintable(candidate));
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
    QVERIFY(body.contains("ocr_candidate"));
    const QJsonArray content = QJsonDocument::fromJson(body).object().value("messages").toArray().at(1).toObject().value("content").toArray();
    QCOMPARE(content.size(), 3);
}

QTEST_MAIN(TestFixProtocol)
#include "test_fix_protocol.moc"
