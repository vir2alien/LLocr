#include <QtTest>

#include "core/ModelProfiles.h"
#include "core/OcrResult.h"
#include "parsers/BlockStyle.h"
#include "parsers/MinerULayoutParser.h"
#include "parsers/ParserFactory.h"
#include "parsers/ParserOptions.h"
#include "parsers/RawParser.h"

using namespace llocr;

class TestDetParser : public QObject
{
    Q_OBJECT

private slots:
    // --- MinerU layout pass (the manual markup feature) -----------------------

    // One "<|box_start|>…<|box_end|><|ref_start|>…<|ref_end|>" line per block,
    // coordinates on the 0–1000 scale; the rotation token and the continuation
    // flag in the tail are dropped — the markup pass only positions blocks, the
    // text arrives later through blockRecognition.
    void parsesMinerULayoutLines()
    {
        const QString raw = QStringLiteral("Sure, here is the layout of the page:\n"
                                           "<|box_start|>34 56 789 123<|box_end|><|ref_start|>title<|ref_end|><|rotate_up|>\n"
                                           "<|box_start|>100 200 900 800<|box_end|><|ref_start|>text<|ref_end|><|rotate_up|>txt_contd_tgt\n"
                                           "<|box_start|>481 923 511 935<|box_end|><|ref_start|>header<|ref_end|><|rotate_up|><|im_end|>\n");

        MinerULayoutParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        QCOMPARE(r.pages.size(), 1);
        const OcrPage &page = r.pages.first();
        QCOMPARE(page.boxes.size(), 3);

        const BoundingBox &title = page.boxes.at(0);
        QCOMPARE(title.label, QStringLiteral("title"));
        QVERIFY(title.text.isEmpty());
        QVERIFY(title.positioned);
        QVERIFY(qFuzzyCompare(title.rect.x(), 0.034));
        QVERIFY(qFuzzyCompare(title.rect.y(), 0.056));
        QVERIFY(qFuzzyCompare(title.rect.width(), (789.0 - 34.0) / 1000.0));
        QVERIFY(qFuzzyCompare(title.rect.height(), (123.0 - 56.0) / 1000.0));

        QCOMPARE(page.boxes.at(1).label, QStringLiteral("text"));
        QCOMPARE(page.boxes.at(2).label, QStringLiteral("header"));

        // Nothing is rendered yet — no blocks with text.
        QVERIFY(page.text.isEmpty());
        QVERIFY(r.notes.isEmpty());
    }

    // The upstream client maps unknown regions to images and skips inline
    // formulas: an unknown block renders as an image crop placeholder, an
    // inline-formula line must not become a block at all.
    void minerULayoutUnknownReadsAsImageAndInlineFormulaIsSkipped()
    {
        const QString raw = QStringLiteral("<|box_start|>10 10 500 200<|box_end|><|ref_start|>unknown<|ref_end|>\n"
                                           "<|box_start|>20 220 500 240<|box_end|><|ref_start|>inline_formula<|ref_end|>\n"
                                           "<|box_start|>30 300 500 400<|box_end|><|ref_start|>image<|ref_end|>\n");

        MinerULayoutParser parser(ParserOptions{});
        parser.m_options.modelId = QStringLiteral("mineru");
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        QCOMPARE(r.pages.first().boxes.size(), 2);
        QCOMPARE(r.pages.first().boxes.at(0).label, QStringLiteral("image"));
        const QString md = r.pages.first().text;
        QVERIFY(md.contains(QStringLiteral("![Image](image://ocr/crop/0)")));
        QVERIFY(md.contains(QStringLiteral("![Image](image://ocr/crop/1)")));
    }

    // Boxes off the 0–1000 scale and zero-size boxes are dropped the way the
    // upstream client drops them.
    void minerULayoutDropsInvalidBoxes()
    {
        const QString raw = QStringLiteral("<|box_start|>10 10 1200 200<|box_end|><|ref_start|>text<|ref_end|>\n"
                                           "<|box_start|>50 50 50 300<|box_end|><|ref_start|>text<|ref_end|>\n"
                                           "<|box_start|>100 100 400 200<|box_end|><|ref_start|>text<|ref_end|>\n");

        MinerULayoutParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        QCOMPARE(r.pages.first().boxes.size(), 1);
        QVERIFY(qFuzzyCompare(r.pages.first().boxes.at(0).rect.width(), 0.3));
    }

    // A reply without a single block line leaves no positioned boxes — the
    // markup pass reports it instead of wiping the page.
    void minerULayoutReplyWithoutBlockLinesYieldsNoBoxes()
    {
        const QString raw = QStringLiteral("This page contains a paragraph of body text.");

        MinerULayoutParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        QVERIFY(r.pages.first().boxes.isEmpty());
        QCOMPARE(r.notes.size(), 1);
        QVERIFY(r.notes.first().contains(QStringLiteral("No layout tokens")));
    }

    // Composite regions are recognition units: a container absorbs the children
    // it covers (>= 0.9 of the child) and reads as the canonical equation/image
    // kind, so markup never queues the same pixels twice. A child that only
    // pokes out of a container, and a caption below it, survive.
    void minerULayoutAbsorbsChildrenIntoComposites()
    {
        const QString raw = QStringLiteral("<|box_start|>100 100 900 500<|box_end|><|ref_start|>equation_block<|ref_end|>\n"
                                           "<|box_start|>110 120 500 200<|box_end|><|ref_start|>equation<|ref_end|>\n"
                                           "<|box_start|>110 220 890 320<|box_end|><|ref_start|>text<|ref_end|>\n"
                                           "<|box_start|>100 550 900 700<|box_end|><|ref_start|>image_block<|ref_end|>\n"
                                           "<|box_start|>120 560 700 690<|box_end|><|ref_start|>image<|ref_end|>\n"
                                           "<|box_start|>120 700 700 780<|box_end|><|ref_start|>image_caption<|ref_end|>\n"
                                           "<|box_start|>850 450 950 520<|box_end|><|ref_start|>text<|ref_end|>\n");

        MinerULayoutParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const OcrPage &page = r.pages.first();
        QCOMPARE(page.boxes.size(), 4);
        QCOMPARE(page.boxes.at(0).label, QStringLiteral("equation"));
        QCOMPARE(page.boxes.at(0).rect.width(), 0.8);
        QCOMPARE(page.boxes.at(1).label, QStringLiteral("image"));
        QCOMPARE(page.boxes.at(2).label, QStringLiteral("image_caption"));
        QCOMPARE(page.boxes.at(3).label, QStringLiteral("text"));

        // The absorbed children are gone from the text; the surviving container
        // renders as the crop placeholder of its box index.
        QVERIFY(page.text.contains(QStringLiteral("![Image](image://ocr/crop/1)")));
        QCOMPARE(page.boxes.at(0).duplicateSuspect, false);
    }

    // Two heavily overlapping composite boxes do not erase each other: only
    // the strictly larger one absorbs, so the pair collapses onto the bigger
    // copy (the coordinates stay outside the base parser's exact-match
    // tolerance, so this really exercises the absorption pass).
    void minerULayoutNearIdenticalCompositesSurvive()
    {
        const QString raw = QStringLiteral("<|box_start|>100 100 900 500<|box_end|><|ref_start|>equation_block<|ref_end|>\n"
                                           "<|box_start|>115 115 885 485<|box_end|><|ref_start|>equation_block<|ref_end|>\n");

        MinerULayoutParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        QCOMPARE(r.pages.first().boxes.size(), 1);
        QCOMPARE(r.pages.first().boxes.at(0).label, QStringLiteral("equation"));
        QCOMPARE(r.pages.first().boxes.at(0).rect.width(), 0.8);
    }

    // The bullet-list container absorbs its items (they arrive as sibling text
    // blocks under it) and is recognized as one list; a heading near it is not
    // a list item and stays.
    void minerULayoutAbsorbsListItems()
    {
        const QString raw = QStringLiteral("<|box_start|>100 100 900 180<|box_end|><|ref_start|>title<|ref_end|>\n"
                                           "<|box_start|>120 220 880 700<|box_end|><|ref_start|>list<|ref_end|>\n"
                                           "<|box_start|>130 230 870 380<|box_end|><|ref_start|>text<|ref_end|>\n"
                                           "<|box_start|>130 390 870 540<|box_end|><|ref_start|>text<|ref_end|>\n"
                                           "<|box_start|>130 550 870 690<|box_end|><|ref_start|>text<|ref_end|>\n"
                                           "<|box_start|>100 750 900 830<|box_end|><|ref_start|>text<|ref_end|>\n");

        MinerULayoutParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const OcrPage &page = r.pages.first();
        QCOMPARE(page.boxes.size(), 3);
        QCOMPARE(page.boxes.at(0).label, QStringLiteral("title"));
        QCOMPARE(page.boxes.at(1).label, QStringLiteral("list"));
        QCOMPARE(page.boxes.at(2).label, QStringLiteral("text"));
    }

    // A reply with no layout header at all is kept as text — by whichever
    // parser was pointed at it. The raw parser is the one that answers a
    // transcription task; the layout parser reports the empty page instead.
    void fallsBackWhenNoTokens()
    {
        const OcrResult r = RawParser().parse(QStringLiteral("just plain text"));

        QVERIFY(r.success);
        QCOMPARE(r.pages.size(), 1);
        QCOMPARE(r.pages.first().text, QStringLiteral("just plain text"));
        QVERIFY(r.pages.first().boxes.isEmpty());
    }

    // A short reply without tokens is chatter, not a misplaced page: no
    // diagnostic for it.
    void shortReplyWithoutTokensIsSilent()
    {
        const OcrResult r = MinerULayoutParser().parse(QStringLiteral("OK"));
        QVERIFY(r.success);
        QVERIFY(r.notes.isEmpty());
    }

    // --- RawParser ------------------------------------------------------------

    // rebuildText is the parser's own contract: the raw parser has no
    // fragments, so it must return the page text rather than an empty string.
    void rawParserRebuildsToPageText()
    {
        const RawParser parser;
        OcrPage page;
        page.text = QStringLiteral("just text");
        QCOMPARE(parser.rebuildText(page), QStringLiteral("just text"));
        QCOMPARE(parser.displayName(), QStringLiteral("Raw text"));
    }

    // Layout-marked pages carry boxes even under a raw-parser model
    // (GLM-OCR): the rebuild joins the blocks' effective texts, so a
    // block filled or re-recognized by «Исправить» reaches the preview instead
    // of the stale markup text.
    void rawParserRebuildsMarkupBoxes()
    {
        const RawParser parser;
        OcrPage page;
        page.text = QStringLiteral("stale markup text");

        BoundingBox plain;
        plain.label = QStringLiteral("text");
        plain.text = QStringLiteral("first block");
        page.boxes.append(plain);

        BoundingBox reRecognized = plain;
        reRecognized.text = QStringLiteral("original text");
        reRecognized.correctedText = QStringLiteral("corrected text");
        page.boxes.append(reRecognized);

        BoundingBox unfilled;
        unfilled.label = QStringLiteral("text");
        page.boxes.append(unfilled);

        QCOMPARE(parser.rebuildText(page), QStringLiteral("first block\n\ncorrected text"));
    }

    // The raw parser keeps no fragments: the page text is the whole truth and
    // the sync has nothing to map.
    void rawParserKeepsNoRanges()
    {
        OcrPage page;
        page.text = QStringLiteral("whatever the model said");
        const auto rebuilt = RawParser().rebuildTextWithRanges(page);
        QCOMPARE(rebuilt.text, page.text);
        QVERIFY(rebuilt.ranges.isEmpty());
    }

    // --- ParserFactory ---------------------------------------------------------

    // Every registered id must resolve to a parser that exists.
    void registeredParsersAreCreatable()
    {
        const QStringList ids = ParserFactory::registeredIds();
        QVERIFY(ids.contains(QStringLiteral("raw")));
        for (const QString &id : ids) {
            const auto parser = ParserFactory::create(id);
            QVERIFY(parser);
            QCOMPARE(parser->id(), id);
            QVERIFY(!parser->displayName().isEmpty());
        }
    }

    // "auto" is offered first in Settings but is not a parser: it is resolved
    // against the model adapter before create() (ADR 88). Reaching the factory
    // with it means a caller skipped that resolution, and the factory refuses
    // instead of silently answering with 'raw'.
    void autoIdIsSelectableButNeverCreatesAParser()
    {
        QCOMPARE(ParserFactory::selectableIds().first(), ParserFactory::kAutoId);
        QCOMPARE(ParserFactory::selectableIds().size(), ParserFactory::selectableDisplayNames().size());
        QVERIFY(!ParserFactory::create(ParserFactory::kAutoId));

        // An unregistered id still degrades to 'raw' rather than crashing.
        const auto fallback = ParserFactory::create(QStringLiteral("no-such-parser"));
        QVERIFY(fallback);
        QCOMPARE(fallback->id(), QStringLiteral("raw"));
    }

    // A parser is named after the model whose reply it reads, and the id is
    // what the profile names. The retired shared id must not linger: it resolves
    // to raw like any other unknown one, which is why the startup migration
    // rewrites a stored one instead of letting it sit.
    void parsersAreNamedAfterTheirModel()
    {
        QCOMPARE(MinerULayoutParser().id(), QStringLiteral("mineru-layout"));
        QCOMPARE(MinerULayoutParser().displayName(), QStringLiteral("MinerU layout"));

        const QStringList ids = ParserFactory::registeredIds();
        QVERIFY(ids.contains(QStringLiteral("raw")));
        QVERIFY(ids.contains(QStringLiteral("mineru-layout")));
        QVERIFY(!ids.contains(QStringLiteral("det_tokens")));
        QVERIFY(dynamic_cast<MinerULayoutParser *>(ParserFactory::create(QStringLiteral("mineru-layout")).get()));
        QCOMPARE(ParserFactory::create(QStringLiteral("det_tokens"))->id(), QStringLiteral("raw"));
    }

    // --- Label map (resources/profiles/labels.json, ADR 88) ------------------

    // The unit-test binary carries no resource bundle, so the compiled-in
    // fallback table must behave like the shipped one for the shared labels.
    void builtInLabelMapStylesSharedLabels()
    {
        QCOMPARE(blockStyleForLabel(QStringLiteral("title")).style, BlockStyle::Heading);
        QCOMPARE(blockStyleForLabel(QStringLiteral("table")).style, BlockStyle::Table);
        QCOMPARE(blockStyleForLabel(QStringLiteral("equation")).style, BlockStyle::Equation);
        QCOMPARE(blockStyleForLabel(QStringLiteral("image")).style, BlockStyle::ImagePlaceholder);
        QCOMPARE(blockStyleForLabel(QStringLiteral("page_caption")).style, BlockStyle::PlainText);
    }

    // Model styling now lives in resources/profiles/models/<id>.json, not in
    // labels.json, so the base document carries no per-model section at all.
    void modelStylesDoNotBleedIntoOtherModels()
    {
        const ModelProfiles::Role *mineru = ModelProfiles::roleFor(QStringLiteral("mineru"), QStringLiteral("layout"));
        QVERIFY(mineru);
        QCOMPARE(mineru->blockStyles.value(QStringLiteral("doc_title")), QStringLiteral("heading"));
        QCOMPARE(mineru->blockStyles.value(QStringLiteral("caption")), QStringLiteral("italic"));

        const BlockStyleMap &map = BlockStyleMap::instance();
        QCOMPARE(map.styleForLabel(QStringLiteral("doc_title"), QStringLiteral("mineru")).style, BlockStyle::Heading);
        QCOMPARE(map.styleForLabel(QStringLiteral("caption"), QStringLiteral("mineru")).style, BlockStyle::Italic);
        // A model that does not name the label must not inherit it.
        QCOMPARE(map.styleForLabel(QStringLiteral("doc_title"), QStringLiteral("glm-ocr")).style, BlockStyle::PlainText);
        QVERIFY(!map.knowsLabel(QStringLiteral("doc_title"), QStringLiteral("glm-ocr")));
        // A label the model does not name still falls through to the base map.
        QCOMPARE(map.styleForLabel(QStringLiteral("image"), QStringLiteral("mineru")).style, BlockStyle::ImagePlaceholder);
    }
};

QTEST_MAIN(TestDetParser)
#include "test_det_parser.moc"
