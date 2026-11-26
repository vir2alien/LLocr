#include <QtTest>

#include "core/ModelProfiles.h"

using namespace llocr;

namespace {

const ModelProfiles::PromptProfile *fixRequests()
{
    return ModelProfiles::promptProfile(QStringLiteral("fix-requests"));
}

const ModelProfiles::PromptProfile *readRequests()
{
    return ModelProfiles::promptProfile(QString::fromLatin1(ModelProfiles::kReadPromptProfileId));
}

struct TypeMarker {
    const char *type;
    const char *marker;
};

const QList<TypeMarker> &fixTypeMarkers()
{
    static const QList<TypeMarker> markers = {
        {"text", "the dash type in a numeric range"},
        {"title", "add a Markdown heading prefix the candidate does not have"},
        {"abstract", "leading zeros"},
        {"list", "the indentation level"},
        {"code", "O/0, l/I/1, |/!"},
        {"caption", "the sub-figure letters"},
        {"footnote", "p < 0.05 vs p < 0.01"},
        {"ref_text", "a letter drawn identically from another alphabet is not the same letter"},
        {"header", "the section and page numbers"},
        {"footer", "the organization or copyright text"},
        {"page_number", "infer a digit you cannot see"},
        {"seal", "the legal-form abbreviation"},
        {"key_value", "the checkbox state (checked vs empty)"},
        {"table", "cell position, rowspan, colspan and empty cells are content"},
        {"equation", "\\tag{n}"},
    };
    return markers;
}

const QList<TypeMarker> &readTypeMarkers()
{
    static const QList<TypeMarker> markers = {
        {"text", "Keep Latin, Greek and Cyrillic letters as they appear"},
        {"title", "A Markdown # prefix is markup, not content"},
        {"abstract", "never infer a value you cannot see"},
        {"list", "do not reorder, re-level, renumber, merge or split items"},
        {"code", "O and 0, l and 1, | and !"},
        {"caption", "Keep the label word (Fig., Table, …)"},
        {"footnote", "the significance thresholds and their operators (<, ≤, >, ≥)"},
        {"ref_text", "a letter that looks like a letter of another alphabet is not the same letter"},
        {"header", "Keep the header as one line"},
        {"footer", "Keep the footer as one line"},
        {"page_number", "never infer a digit you cannot see"},
        {"seal", "the emblem, the border and the ornament"},
        {"key_value", "never fill an empty field"},
        {"table", "Use rowspan and colspan exactly where cells visibly span rows or columns"},
        {"equation", "Do not wrap the formula in $ or $$ delimiters"},
    };
    return markers;
}

QStringList canonicalTypes()
{
    return {QStringLiteral("text"),
            QStringLiteral("title"),
            QStringLiteral("abstract"),
            QStringLiteral("list"),
            QStringLiteral("code"),
            QStringLiteral("caption"),
            QStringLiteral("footnote"),
            QStringLiteral("ref_text"),
            QStringLiteral("header"),
            QStringLiteral("footer"),
            QStringLiteral("page_number"),
            QStringLiteral("seal"),
            QStringLiteral("key_value"),
            QStringLiteral("table"),
            QStringLiteral("equation")};
}

}  // namespace

class TestRequestContracts : public QObject
{
    Q_OBJECT

private slots:
    void fixSystemPromptCarriesTheContract();
    void fixSystemPromptListsTheReviewTriggers();
    void fixTypePromptsCarryTheFourParts();
    void fixTypePromptsCarryTheirOwnRule();
    void fixTablePromptKeepsTheCandidateSerialization();
    void readSystemPromptCarriesTheContract();
    void readSystemPromptListsTheReviewTriggers();
    void readTypePromptsCarryTheRules();
    void readTypePromptsCarryTheirOwnRule();
};

void TestRequestContracts::fixSystemPromptCarriesTheContract()
{
    const ModelProfiles::PromptProfile *profile = fixRequests();
    QVERIFY(profile);
    const QString system = profile->prompts.systemPrompt;
    QVERIFY(system.contains(QStringLiteral("exactly one of these")));
    QVERIFY(system.contains(QStringLiteral("OK")));
    QVERIFY(system.contains(QStringLiteral("FIX")));
    QVERIFY(system.contains(QStringLiteral("REVIEW")));
    QVERIFY(!system.contains(QStringLiteral("EMPTY")));
    QVERIFY(system.contains(QStringLiteral("Never truncate the output")));
    QVERIFY(system.contains(QStringLiteral("<the complete corrected block>")));
    QVERIFY(system.contains(QStringLiteral("one character, or a run of characters, or a whole word")));
    QVERIFY(system.contains(QStringLiteral("Never insert content the image does not show")));
    QVERIFY(system.contains(QStringLiteral("drawn identically in two alphabets")));
    QVERIFY(system.contains(QStringLiteral("Never add markup the candidate does not have")));
}

void TestRequestContracts::fixSystemPromptListsTheReviewTriggers()
{
    const ModelProfiles::PromptProfile *profile = fixRequests();
    QVERIFY(profile);
    const QString system = profile->prompts.systemPrompt;
    for (const QString &trigger : {QStringLiteral("cut off"), QStringLiteral("covered"), QStringLiteral("overlapped"), QStringLiteral("faded"), QStringLiteral("too small to resolve")})
        QVERIFY2(system.contains(trigger), qPrintable(trigger));
}

void TestRequestContracts::fixTypePromptsCarryTheFourParts()
{
    const ModelProfiles::PromptProfile *profile = fixRequests();
    QVERIFY(profile);
    for (const QString &type : canonicalTypes()) {
        const QString prompt = profile->prompts.blockPrompts.value(type);
        QVERIFY2(!prompt.isEmpty(), qPrintable(type));
        QVERIFY2(prompt.contains(QStringLiteral("After FIX, output")), qPrintable(type));
        QVERIFY2(prompt.contains(QStringLiteral("Correct:")), qPrintable(type));
        QVERIFY2(prompt.contains(QStringLiteral("Leave:")), qPrintable(type));
        QVERIFY2(prompt.contains(QStringLiteral("Never:")), qPrintable(type));
    }
}

void TestRequestContracts::fixTypePromptsCarryTheirOwnRule()
{
    const ModelProfiles::PromptProfile *profile = fixRequests();
    QVERIFY(profile);
    for (const TypeMarker &marker : fixTypeMarkers()) {
        const QString type = QString::fromUtf8(marker.type);
        const QString prompt = profile->prompts.blockPrompts.value(type);
        QVERIFY2(!prompt.isEmpty(), qPrintable(type));
        QVERIFY2(prompt.contains(QString::fromUtf8(marker.marker)), qPrintable(type));
    }
}

void TestRequestContracts::fixTablePromptKeepsTheCandidateSerialization()
{
    const ModelProfiles::PromptProfile *profile = fixRequests();
    QVERIFY(profile);
    const QString prompt = profile->prompts.blockPrompts.value(QStringLiteral("table"));
    QVERIFY(prompt.contains(QStringLiteral("the candidate's serialization")));
    QVERIFY(prompt.contains(QStringLiteral("HTML if the candidate is HTML")));
    QVERIFY(prompt.contains(QStringLiteral("a pipe table if it is Markdown")));
    QVERIFY(!prompt.contains(QStringLiteral("HTML table")));
}

void TestRequestContracts::readSystemPromptCarriesTheContract()
{
    const ModelProfiles::PromptProfile *profile = readRequests();
    QVERIFY(profile);
    const QString system = profile->prompts.systemPrompt;
    QVERIFY(system.contains(QStringLiteral("You transcribe one block of a printed document")));
    QVERIFY(system.contains(QStringLiteral("The image is the only source of truth")));
    QVERIFY(system.contains(QStringLiteral("Preserve by default")));
    QVERIFY(system.contains(QStringLiteral("drawn identically in two alphabets")));
    QVERIFY(system.contains(QStringLiteral("Do not add $ or $$ delimiters")));
    QVERIFY(system.contains(QStringLiteral("answer REVIEW")));
    QVERIFY(system.contains(QStringLiteral("Return only the transcription")));
}

void TestRequestContracts::readSystemPromptListsTheReviewTriggers()
{
    const ModelProfiles::PromptProfile *profile = readRequests();
    QVERIFY(profile);
    const QString system = profile->prompts.systemPrompt;
    for (const QString &trigger : {QStringLiteral("cut off"), QStringLiteral("covered"), QStringLiteral("overlapped"), QStringLiteral("faded"), QStringLiteral("too small to resolve")})
        QVERIFY2(system.contains(trigger), qPrintable(trigger));
}

void TestRequestContracts::readTypePromptsCarryTheRules()
{
    const ModelProfiles::PromptProfile *profile = readRequests();
    QVERIFY(profile);
    for (const QString &type : canonicalTypes()) {
        const QString prompt = profile->prompts.blockPrompts.value(type);
        QVERIFY2(!prompt.isEmpty(), qPrintable(type));
        QVERIFY2(prompt.contains(QStringLiteral("Rules:")), qPrintable(type));
        QVERIFY2(prompt.contains(QStringLiteral("output exactly: REVIEW")), qPrintable(type));
        QVERIFY2(!prompt.contains(QStringLiteral("output FIX")), qPrintable(type));
        // The "$ signs" rule contradicted the app's canonical form (ADR 194).
        QVERIFY2(!prompt.contains(QStringLiteral("between $ signs")), qPrintable(type));
    }
}

void TestRequestContracts::readTypePromptsCarryTheirOwnRule()
{
    const ModelProfiles::PromptProfile *profile = readRequests();
    QVERIFY(profile);
    for (const TypeMarker &marker : readTypeMarkers()) {
        const QString type = QString::fromUtf8(marker.type);
        const QString prompt = profile->prompts.blockPrompts.value(type);
        QVERIFY2(!prompt.isEmpty(), qPrintable(type));
        QVERIFY2(prompt.contains(QString::fromUtf8(marker.marker)), qPrintable(type));
    }
}

QTEST_MAIN(TestRequestContracts)
#include "test_request_contracts.moc"
