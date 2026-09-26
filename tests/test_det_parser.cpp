#include <QtTest>

#include "core/OcrResult.h"
#include "parsers/BlockStyle.h"
#include "parsers/DetTokensParser.h"
#include "parsers/ParserFactory.h"
#include "parsers/ParserOptions.h"
#include "parsers/RawParser.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

using namespace llocr;

class TestDetParser : public QObject {
    Q_OBJECT

private slots:
    // The current model wraps every token in <|det|>…<|/det|> and streams
    // newlines as the two characters `\n`.
    void parsesWrappedDetStream() {
        const QString raw = QStringLiteral(
            R"(<|det|>title [115, 101, 273, 117]<|/det|>1. Introduction\n)"
            R"(<|det|>text [112, 132, 884, 309]<|/det|>Humans are remarkably adept at long-horizon tasks\n)"
            R"(<|det|>text [141, 484, 884, 581]<|/det|>- We introduce Reference Sliding Window Attention (R-SWA)\n)"
            R"(<|det|>page_number [493, 924, 506, 935]<|/det|>3)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        QCOMPARE(r.pages.size(), 1);
        const OcrPage& page = r.pages.first();
        QCOMPARE(page.boxes.size(), 4);

        const BoundingBox& title = page.boxes.at(0);
        QCOMPARE(title.label, QStringLiteral("title"));
        QCOMPARE(title.text, QStringLiteral("1. Introduction"));
        // x = 115/1000, width = (273-115)/1000
        QVERIFY(qFuzzyCompare(title.rect.x(), 0.115));
        QVERIFY(qFuzzyCompare(title.rect.width(), 0.158));

        const BoundingBox& text = page.boxes.at(1);
        QCOMPARE(text.label, QStringLiteral("text"));
        // the trailing \n escape is trimmed away
        QCOMPARE(text.text, QStringLiteral("Humans are remarkably adept at long-horizon tasks"));

        const BoundingBox& pageNumber = page.boxes.at(3);
        QCOMPARE(pageNumber.label, QStringLiteral("page_number"));
        QCOMPARE(pageNumber.text, QStringLiteral("3"));

        const QString md = page.text;
        QVERIFY(md.contains(QStringLiteral("## 1. Introduction")));   // title -> heading
        QVERIFY(md.contains(QStringLiteral("- We introduce Reference Sliding Window Attention (R-SWA)")));
        QVERIFY(md.contains(QStringLiteral("*3*")));                  // page number in italics
    }

    // Settings → Output → "Keep page numbers" off: page_number tokens are
    // ignored entirely — no text block and no overlay box.
    void dropsPageNumbersWhenDisabled() {
        const QString raw = QStringLiteral(
            R"(<|det|>title [115, 101, 273, 117]<|/det|>1. Introduction\n)"
            R"(<|det|>page_number [493, 924, 506, 935]<|/det|>3\n)"
            R"(<|det|>text [112, 132, 884, 309]<|/det|>Body paragraph\n)");

        DetTokensParser parser(ParserOptions{false, false});
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const OcrPage& page = r.pages.first();
        QCOMPARE(page.boxes.size(), 2);
        const QString md = page.text;
        QVERIFY(!md.contains(QStringLiteral("*3*")));
        QVERIFY(md.contains(QStringLiteral("## 1. Introduction")));
        QVERIFY(md.contains(QStringLiteral("Body paragraph")));

        // rebuildText honours the flag for pages that still carry a
        // page_number box (e.g. recognized before the setting was changed).
        OcrPage withNumber = page;
        BoundingBox numberBox;
        numberBox.label = QStringLiteral("page_number");
        numberBox.text = QStringLiteral("3");
        withNumber.boxes.append(numberBox);
        QVERIFY(DetTokensParser().rebuildText(withNumber).contains(QStringLiteral("*3*")));
        QVERIFY(!parser.rebuildText(withNumber).contains(QStringLiteral("*3*")));
    }

    // Wrapped content decodes ONLY the stream's own \n line separator. LaTeX
    // math is emitted with real single backslashes (\( ... \)), so the command
    // survives verbatim and inline math converts to $...$.
    void keepsSingleBackslashLatexInWrappedContent() {
        const QString raw = QStringLiteral(
            R"(<|det|>text [112, 132, 884, 309]<|/det|>line one\nline two  \( m + n \)\n)"
            R"(<|det|>text [113, 780, 884, 860]<|/det|>see  \( [10, 30, 33, 34] \)\n)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const OcrPage& page = r.pages.first();
        QCOMPARE(page.boxes.size(), 2);
        // \n -> real newline; LaTeX backslashes are preserved, not decoded.
        QCOMPARE(page.boxes.at(0).text, QStringLiteral("line one\nline two  \\( m + n \\)"));

        const QString md = page.text;
        QVERIFY(md.contains(QStringLiteral("$m + n$")));
        QVERIFY(md.contains(QStringLiteral("$[10, 30, 33, 34]$")));
    }

    // Regression: the formula stream must not be re-unescaped after JSON
    // decoding. A wrapped equation with single-backslash LaTeX (\frac, \top,
    // \right, \tag) previously degraded to form feed / tab / carriage return.
    void keepsFormulaLatexIntact() {
        const QString raw = QStringLiteral(
            R"(<|det|>equation [295, 564, 884, 579]<|/det|>\alpha_ {t j} = \frac {\exp \left(\frac {\mathbf {q} _ {t} ^ {\top} \mathbf {k}}{\sqrt {d _ {k}}}\right)}{\sum_ {i \in \mathcal {N} (t)} \exp \left(\frac {\mathbf {q} _ {t} ^ {\top} \mathbf {k} _ {i}}{\sqrt {d _ {k}}}\right)}, \quad j \in \mathcal {N} (t), \tag {3}\n)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const OcrPage& page = r.pages.first();
        QCOMPARE(page.boxes.size(), 1);
        const QString text = page.boxes.at(0).text;
        QVERIFY(text.contains(QStringLiteral("\\frac")));
        QVERIFY(text.contains(QStringLiteral("\\top")));
        QVERIFY(text.contains(QStringLiteral("\\right")));
        QVERIFY(text.contains(QStringLiteral("\\tag {3}")));
        QVERIFY(!text.contains(QLatin1Char('\t')));   // \tag must not become TAB+"ag"
        QVERIFY(!text.contains(QLatin1Char('\f')));   // \frac must not become FF+"rac"
        QVERIFY(!text.contains(QLatin1Char('\r')));   // \right must not become CR+"ight"
    }

    // The model appends a trailing <|end_of_sentence|> marker after the last
    // token. It must not leak into the recognized text.
    void stripsTrailingEndOfSentenceToken() {
        const QString raw = QStringLiteral(
            R"(<|det|>page_number [493, 924, 506, 935]<|/det|>3<|end_of_sentence|>\n)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const OcrPage& page = r.pages.first();
        QCOMPARE(page.boxes.size(), 1);
        QCOMPARE(page.boxes.at(0).label, QStringLiteral("page_number"));
        QCOMPARE(page.boxes.at(0).text, QStringLiteral("3"));

        const QString md = page.text;
        QVERIFY(md.contains(QStringLiteral("*3*")));
        QVERIFY(!md.contains(QStringLiteral("<|end_of_sentence|>")));
        QVERIFY(!md.contains(QStringLiteral("<|")));
    }

    // The live stream emits the EOS marker with full-width pipes (｜ U+FF5C)
    // and ▁ (U+2581) instead of spaces; those must be stripped too.
    void stripsFullWidthServiceToken() {
        const QString raw = QStringLiteral(
            "<|det|>page_number [493, 924, 506, 935]<|/det|>3"
            "<\uFF5C" "end\u2581of\u2581sentence\uFF5C" ">\n");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const OcrPage& page = r.pages.first();
        QCOMPARE(page.boxes.size(), 1);
        QCOMPARE(page.boxes.at(0).text, QStringLiteral("3"));
        QVERIFY(!page.text.contains(QStringLiteral("\uFF5C")));
        QVERIFY(!page.text.contains(QStringLiteral("end")));
        QVERIFY(page.text.contains(QStringLiteral("*3*")));
    }

    // A stray control token inside token content (e.g. an unparsed
    // <|grounding|> tag) is removed as well.
    void stripsStrayServiceTokensInContent() {
        const QString raw = QStringLiteral(
            R"(<|det|>text [112, 132, 884, 309]<|/det|>Body text <|grounding|> with more.\n)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const OcrPage& page = r.pages.first();
        QCOMPARE(page.boxes.size(), 1);
        QCOMPARE(page.boxes.at(0).text, QStringLiteral("Body text  with more."));
        QVERIFY(!page.text.contains(QStringLiteral("<|grounding|>")));
    }

    // Real-world sample captured from the live model in step D.
    void parsesRealResponse() {
        const QString raw = QStringLiteral(
            "title [92, 109, 890, 165]КАК ЗАКАЗАТЬ ПЕЧАТЬ?\n"
            "text [81, 304, 745, 400]√ Выбрать может оттиска и оснастку;\n"
            "footer [402, 904, 602, 941]ПЕЧАТИ\nИ ШТАМПЫ");

        DetTokensParser parser;

        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        QCOMPARE(r.pages.size(), 1);

        const OcrPage& page = r.pages.first();
        QCOMPARE(page.boxes.size(), 3);

                // First box: label, text and normalized geometry.
        const BoundingBox& first = page.boxes.at(0);
        QCOMPARE(first.label, QStringLiteral("title"));
        QCOMPARE(first.text, QStringLiteral("КАК ЗАКАЗАТЬ ПЕЧАТЬ?"));
        // x = 92/1000, width = (890-92)/1000
        QVERIFY(qFuzzyCompare(first.rect.x(), 0.092));
        QVERIFY(qFuzzyCompare(first.rect.width(), 0.798));

                // Last box must capture the multi-line text after the ']'.
        const BoundingBox& last = page.boxes.at(2);
        QCOMPARE(last.label, QStringLiteral("footer"));
        QCOMPARE(last.text, QStringLiteral("ПЕЧАТИ\nИ ШТАМПЫ"));
    }

    // LFM2.5-VL layout annotation: bare tokens prefixed with "image_index=<n>".
    // The prefix must be consumed (not leak into the text), and the next
    // header's prefix must not stick to the previous block's content.
    void parsesLfm25LayoutAnnotation() {
        const QString raw = QStringLiteral(
            "image_index=0 title [115, 101, 273, 117]\n"
            "Заголовок\n"
            "\n"
            "image_index=0 text [112, 132, 884, 309]\n"
            "Обычный текст\n"
            "\n"
            "image_index=0 page_number [493, 924, 506, 935]\n"
            "3");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        QCOMPARE(r.pages.size(), 1);
        const OcrPage& page = r.pages.first();
        QCOMPARE(page.boxes.size(), 3);
        QCOMPARE(page.boxes.at(0).label, QStringLiteral("title"));
        QCOMPARE(page.boxes.at(0).text, QStringLiteral("Заголовок"));
        QCOMPARE(page.boxes.at(1).label, QStringLiteral("text"));
        QCOMPARE(page.boxes.at(1).text, QStringLiteral("Обычный текст"));
        QCOMPARE(page.boxes.at(2).label, QStringLiteral("page_number"));
        QCOMPARE(page.boxes.at(2).text, QStringLiteral("3"));

        const QString md = page.text;
        QVERIFY(md.contains("## Заголовок"));
        QVERIFY(md.contains("Обычный текст"));
        QVERIFY(!md.contains("image_index"));
    }

    // LFM2.5-VL serializes tables in OTSL (TableFormer vocabulary): <fcel>
    // opens a cell, <lcel>/<ucel>/<xcel> are cells covered by a span (rendered
    // empty — the value is written once, ADR 19), <nl> ends a row.
    void parsesOtslTableIntoMarkdown() {
        const QString raw = QStringLiteral(
            "<|det|>table [0, 0, 500, 200]<|/det|>"
            "<fcel>Вид энергоресурсов<fcel>Годы<fcel>1990<nl>"
            "<fcel>Нефть, млн. т<fcel>в мире<fcel>3179,7<nl>"
            "<fcel>Россия<lcel><fcel>518<nl>");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const QString md = r.pages.first().text;
        QVERIFY(md.contains("| Вид энергоресурсов | Годы | 1990 |"));
        QVERIFY(md.contains("| --- | --- | --- |"));
        QVERIFY(md.contains("| Нефть, млн. т | в мире | 3179,7 |"));
        QVERIFY(md.contains("| Россия |  | 518 |"));
        QVERIFY(!md.contains("<fcel>"));
        QVERIFY(!md.contains("<lcel>"));
        QVERIFY(!md.contains("<nl>"));
    }

    // With «Tables as HTML» on (ADR 64) the OTSL spans become real
    // rowspan/colspan attributes — the parity with the Unlimited-OCR model's
    // verbatim <table> output.
    void parsesOtslTableIntoHtmlWhenEnabled() {
        const QString raw = QStringLiteral(
            "<|det|>table [0, 0, 500, 200]<|/det|>"
            "<fcel>Регион<fcel>1990<fcel>1995<nl>"
            "<fcel>Мир<fcel>100<lcel><nl>"
            "<fcel>Россия<ucel><fcel>50<nl>");

        DetTokensParser parser(ParserOptions{true, true});
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const QString md = r.pages.first().text;
        QVERIFY(md.startsWith(QStringLiteral("<table>")));
        QVERIFY(md.contains(QStringLiteral("<th>Регион</th>")));
        // «100» covers the 1990+1995 columns (lcel) and both «Мир» rows
        // (the ucel in the Россия row).
        QVERIFY(md.contains(QStringLiteral("<td colspan=\"2\" rowspan=\"2\">100</td>")));
        QVERIFY(md.contains(QStringLiteral("<td>Россия</td>")));
        QVERIFY(md.contains(QStringLiteral("</table>")));
        QVERIFY(!md.contains("<fcel>"));
        QVERIFY(!md.contains("<nl>"));
    }

    // LFM2.5-VL formula artifacts: a formula truncated without the closing
    // "\)" still becomes inline math, and the "~" spacing artifact inside
    // \mathrm{...} is dropped (only within math spans).
    void cleansFormulaArtifactsInOtslCells() {
        const QString raw = QStringLiteral(
            "<|det|>table [0, 0, 500, 200]<|/det|>"
            "<fcel>\\( \\mathrm{~r}_{O_{2}} = 0,211 ;<nl>"
            "<fcel>\\( \\mathrm{~r}_{N_{2}} = 0,789 \\) .<nl>");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const QString md = r.pages.first().text;
        QVERIFY(md.contains(QStringLiteral("$ \\mathrm{ r}_{O_{2}} = 0,211 ;$")));
        QVERIFY(md.contains(QStringLiteral("$\\mathrm{ r}_{N_{2}} = 0,789$ .")));
        QVERIFY(!md.contains(QStringLiteral("\\(")));
    }

    // The model can emit OTSL rows under a non-table token (a formula under
    // text/equation): the tags must be stripped there as well — and a
    // single-column fragment becomes plain lines, not a 1-col pipe table.
    void cleansOtslTagsInNonTableBlocks() {
        const QString raw = QStringLiteral(
            "text [10, 10, 400, 100]\\( \\mathrm{~r}_{O_{2}} = 0,211 ;<nl>"
            "\\( \\mathrm{~r}_{N_{2}} = 0,789 \\) .<nl>");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const QString md = r.pages.first().text;
        QVERIFY(!md.contains("<fcel>"));
        QVERIFY(!md.contains("<nl>"));
        QVERIFY(md.contains(QStringLiteral("$ \\mathrm{ r}_{O_{2}} = 0,211 ;$")));
        QVERIFY(md.contains(QStringLiteral("$\\mathrm{ r}_{N_{2}} = 0,789$ .")));
        QVERIFY(!md.contains(QStringLiteral("|")));
    }

    // The layout annotation is experimental (model card): the model sometimes
    // drifts into the XML-ish "image_index=<n> <label>name</label>" shape —
    // with or without a bbox, wrapped in <content>/<figure>/<image> tags and
    // elision lines. Those regions must still tokenize and no service text
    // may leak into the output.
    void parsesXmlDriftAnnotation() {
        const QString raw = QStringLiteral(
            "image_index=0 <label>image</label>\n"
            "<content>\n<figure>\n<image>\n<\n...\n</image>\n</figure>\n</content>\n\n"
            "image_index=0 <label>image_caption</label> [117, 289, 885, 380]\n"
            "<content>Figure 2 | Inspired by humans copying books.</content>\n\n"
            "image_index=0 <label>title</label> [114, 402, 282, 420]\n"
            "<content>3. Methodology</content>");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const auto &page = r.pages.first();
        QCOMPARE(page.boxes.size(), 3);
        QCOMPARE(page.boxes.at(0).label, QStringLiteral("image"));
        QVERIFY(page.boxes.at(0).text.isEmpty());
        QCOMPARE(page.boxes.at(1).label, QStringLiteral("image_caption"));
        QCOMPARE(page.boxes.at(2).label, QStringLiteral("title"));

        const QString md = page.text;
        QVERIFY(md.contains(QStringLiteral("![Image](image://ocr/crop/0)")));
        QVERIFY(md.contains(QStringLiteral("*Figure 2 | Inspired by humans copying books.*")));
        QVERIFY(md.contains(QStringLiteral("## 3. Methodology")));
        QVERIFY(!md.contains("<label>"));
        QVERIFY(!md.contains("<content>"));
        QVERIFY(!md.contains("<figure>"));
        QVERIFY(!md.contains("<image>"));
        QVERIFY(!md.contains("image_index="));
    }

    // Bare <label> headers without a bbox must not be deduped against each
    // other (they all share the zero rect) — every region stays in the output.
    void xmlDriftTokensWithoutBboxAreNotDeduped() {
        const QString raw = QStringLiteral(
            "image_index=0 <label>title</label>\n<content>First</content>\n\n"
            "image_index=0 <label>title</label>\n<content>Second</content>");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const auto &page = r.pages.first();
        QCOMPARE(page.boxes.size(), 2);
        QVERIFY(page.text.contains(QStringLiteral("First")));
        QVERIFY(page.text.contains(QStringLiteral("Second")));
    }

    // The model often nests inline \(…\) inside the display \[…\] equation
    // wrapper — the redundant delimiters must be stripped (bare parens and
    // \left( kept), both in equation blocks and in text with $$…$$ math.
    void stripsNestedInlineDelimsInDisplayMath() {
        DetTokensParser parser;
        const QString raw = QStringLiteral(
            "equation [10, 10, 400, 100]\\[\n\\(N(t) = \\mathcal{P} \\cup D_{n}(t),\\)\n\\]");
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const QString md = r.pages.first().text;
        QVERIFY(md.contains(QStringLiteral("$$\nN(t) = \\mathcal{P} \\cup D_{n}(t),\n$$")));
        QVERIFY(!md.contains(QStringLiteral("\\(")));

        // Same nesting inside a text block.
        const OcrResult r2 = parser.parse(QStringLiteral(
            "text [10, 10, 400, 200]Intro:\\[\n\\(x \\in S\\),\n\\]"));
        QVERIFY(r2.success);
        const QString md2 = r2.pages.first().text;
        QVERIFY(md2.contains(QStringLiteral("$$\nx \\in S,\n$$")));
        QVERIFY(!md2.contains(QStringLiteral("\\(")));
    }

    // Table-caption drift (real-world LFM2.5-VL output): the caption sits in
    // <label>…</label> and the OTSL rows in <content>…</content>, with cells
    // separated by CLOSING </fcel> tags and rows by plain newlines.
    void parsesTableCaptionDrift() {
        const QString raw = QStringLiteral(
            "image_index=0 <label>Table 3 | Performance of long-horizon OCR. Distinct-n is the higher the better.</label>\n"
            "<content>\n"
            "<fcel>Metric</fcel>Pages</fcel>2</fcel>5</fcel>\n"
            "<fcel>Distinct-20</fcel>99.76%</fcel>99.78%</fcel>97.49%</fcel>\n"
            "</content>\n\n"
            "image_index=0 title [114, 383, 338, 401]\n"
            "6. Efficiency Analysis");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const auto &page = r.pages.first();
        QCOMPARE(page.boxes.size(), 3);
        QCOMPARE(page.boxes.at(0).label, QStringLiteral("table_caption"));
        QCOMPARE(page.boxes.at(1).label, QStringLiteral("table"));
        QCOMPARE(page.boxes.at(2).label, QStringLiteral("title"));

        const QString md = page.text;
        QVERIFY(md.contains(QStringLiteral("*Table 3 | Performance of long-horizon OCR. Distinct-n is the higher the better.*")));
        QVERIFY(md.contains(QStringLiteral("| Metric | Pages | 2 | 5 |")));
        QVERIFY(md.contains(QStringLiteral("| Distinct-20 | 99.76% | 99.78% | 97.49% |")));
        QVERIFY(!md.contains("</fcel>"));
        QVERIFY(!md.contains("<content>"));
        QVERIFY(!md.contains("image_index="));
    }

    // A duplicate region must replace the *positioned* block it duplicates, not
    // an unpositioned fragment that happens to sit earlier in the list. Both
    // drift tokens and the untagged preamble are unpositioned (rect 0,0,0,0);
    // the old raw-coordinate index space counted only bbox tokens, so it wrote
    // the replacement into the wrong box and lost the fragment.
    void duplicateRegionReplacesThePositionedBlockNotAnUnpositionedOne() {
        const QString raw = QStringLiteral(
            "image_index=0 <label>title</label>\n<content>Drift header</content>\n\n"
            "text [10, 10, 200, 200]\nFirst\n\n"
            "text [10, 10, 200, 200]\nSecond");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const auto &page = r.pages.first();
        QVERIFY(page.hasDuplicates);
        QCOMPARE(page.boxes.size(), 2);
        QCOMPARE(page.boxes.at(0).text, QStringLiteral("Drift header"));
        QVERIFY(!page.boxes.at(0).positioned);
        QCOMPARE(page.boxes.at(1).text, QStringLiteral("Second"));
        // The text follows the boxes.
        QCOMPARE(page.text, parser.rebuildText(page));
        QVERIFY(page.text.contains(QStringLiteral("Drift header")));
        QVERIFY(!page.text.contains(QStringLiteral("First")));
        QVERIFY(page.text.contains(QStringLiteral("Second")));
    }

    // The same, with an untagged preamble in front of the duplicated region.
    void duplicateRegionAfterPreambleKeepsThePreamble() {
        const QString raw = QStringLiteral(
            "Preamble line\n"
            "text [10, 10, 200, 200]\nFirst\n"
            "text [10, 10, 200, 200]\nSecond");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        const auto &page = r.pages.first();
        QVERIFY(page.hasDuplicates);
        QCOMPARE(page.boxes.size(), 2);
        QCOMPARE(page.boxes.at(0).text, QStringLiteral("Preamble line"));
        QVERIFY(!page.boxes.at(0).positioned);
        QCOMPARE(page.boxes.at(1).text, QStringLiteral("Second"));
        QCOMPARE(page.text, parser.rebuildText(page));
        QVERIFY(page.text.startsWith(QStringLiteral("Preamble line")));
    }

    // The page text is a pure function of the boxes: parsing produces exactly
    // what rebuildText() renders, whatever the reply shape. This is the property
    // the parallel block list used to break.
    void pageTextAlwaysEqualsRebuiltText() {
        const QList<QString> samples = {
            // wrapped stream with a page number
            QStringLiteral(
                R"(<|det|>title [115, 101, 273, 117]<|/det|>1. Introduction\n)"
                R"(<|det|>text [112, 132, 884, 309]<|/det|>Body\n)"
                R"(<|det|>page_number [493, 924, 506, 935]<|/det|>3)"),
            // bare tokens with an untagged preamble and an image block
            QStringLiteral(
                "Lead in\n"
                "title [92, 109, 890, 165]Heading\n"
                "image [132, 118, 862, 269]\n! caption\n"
                "text [10, 10, 200, 200]Tail"),
            // XML drift: unpositioned headers, no coordinates at all
            QStringLiteral(
                "image_index=0 <label>title</label>\n<content>First</content>\n\n"
                "image_index=0 <label>title</label>\n<content>Second</content>"),
            // a block with empty content between two real ones
            QStringLiteral(
                "text [10, 10, 200, 200]First\n"
                "text [210, 10, 400, 200]\n"
                "text [410, 10, 600, 200]Third"),
            // a duplicated region
            QStringLiteral(
                "text [10, 10, 200, 200]First\n"
                "text [10, 10, 200, 200]Second"),
        };

        DetTokensParser parser;
        for (const QString &raw : samples) {
            const OcrResult r = parser.parse(raw);
            QVERIFY(r.success);
            QCOMPARE(r.pages.size(), 1);
            const OcrPage &page = r.pages.first();
            QVERIFY2(page.text == parser.rebuildText(page),
                     qPrintable(QStringLiteral("text/rebuild mismatch for:\n%1\ntext: %2")
                                    .arg(raw, page.text)));
            QCOMPARE(r.text, page.text);
        }
    }

    // The image placeholder must not inherit the model's multi-line figure
    // text as its alt — only the first meaningful line survives.
    void imageAltUsesSingleLine() {
        const QString raw = QStringLiteral(
            "image [10, 10, 400, 200]! Vanilla Attention\n! R-SWA\n! Reference");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        QVERIFY(r.pages.first().text.contains(
            QStringLiteral("![Vanilla Attention](image://ocr/crop/0)")));
        QVERIFY(!r.pages.first().text.contains(QStringLiteral("R-SWA")));
    }

            // Text with no structured tokens falls back to raw text, still succeeds.
    void fallsBackWhenNoTokens() {
        DetTokensParser parser;
        const OcrResult r = parser.parse(QStringLiteral("just plain text"));

        QVERIFY(r.success);
        QCOMPARE(r.pages.size(), 1);
        QCOMPARE(r.pages.first().text, QStringLiteral("just plain text"));
        QVERIFY(r.pages.first().boxes.isEmpty());
    }

            // Every tag is captured into an ordered box list, the image gets a
            // text placeholder, and titles/captions/page numbers get styled.
    void parsesAllTagsAndFormatsMarkdown() {
        const QString raw = QStringLiteral(
            "image [132, 118, 862, 269]\n"
            "image_caption [113, 276, 885, 374]Figure 2 | A caption\n"
            "title [114, 397, 283, 416]3. Methodology\n"
            "title [114, 430, 340, 447]3.1. Long-horizon Parsing\n"
            "text [113, 456, 885, 603]Body text here.\n"
            "page_number [493, 923, 506, 935]5");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        QCOMPARE(r.pages.size(), 1);
        const OcrPage& page = r.pages.first();

        // All six tokens are recognized as boxes, in order.
        QCOMPARE(page.boxes.size(), 6);
        QCOMPARE(page.boxes.at(0).label, QStringLiteral("image"));
        QCOMPARE(page.boxes.at(1).label, QStringLiteral("image_caption"));
        QCOMPARE(page.boxes.at(2).label, QStringLiteral("title"));
        QCOMPARE(page.boxes.at(3).label, QStringLiteral("title"));
        QCOMPARE(page.boxes.at(4).label, QStringLiteral("text"));
        QCOMPARE(page.boxes.at(5).label, QStringLiteral("page_number"));

        const QString md = page.text;
        QVERIFY(md.contains("![Image](image://ocr/crop/0)"));  // image placeholder
        QVERIFY(md.contains("*Figure 2 | A caption*"));        // figure caption
        QVERIFY(md.contains("## 3. Methodology"));             // title -> ##
        QVERIFY(md.contains("### 3.1. Long-horizon Parsing")); // subsection -> ###
        QVERIFY(md.contains("Body text here."));               // plain paragraph
        QVERIFY(md.contains("*5*"));                           // page number footer
    }

            // Inline LaTeX math is converted to Markdown $...$, preserving
            // parentheses inside the formula and formulas without them.
    void convertsInlineMathWithAndWithoutParentheses() {
        const QString raw = QStringLiteral(
            "text [1, 1, 2, 2]capacity of  \\( m + n \\) and  \\( (m + 1) \\)-th token");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const QString md = r.pages.first().text;
        QVERIFY(md.contains(QStringLiteral("$m + n$")));
        QVERIFY(md.contains(QStringLiteral("$(m + 1)$")));
    }

            // The dedicated equation token is parsed as a box and rendered as a
            // clean display-math block $$ … $$ (no stray blank lines).
    void handlesEquationToken() {
        const QString raw = QStringLiteral(
            "text [1, 1, 2, 2]where P denotes the prefix segment of length  \\( L_{m} \\)\n"
            "equation [295, 564, 884, 579]\\[\n"
            "\\mathcal {N} (t) = \\mathcal {P} \\cup \\mathcal {D} _ {n} (t); \\quad \\mathcal {P} = \\{1, \\dots , L _ {m} \\}, \\tag {1}\n\\]\n"
            "text [112, 610, 884, 658]then the following text.");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const OcrPage& page = r.pages.first();
        QCOMPARE(page.boxes.size(), 3);
        QCOMPARE(page.boxes.at(1).label, QStringLiteral("equation"));

        const QString md = page.text;
        QVERIFY(md.contains(QStringLiteral("$$\n\\mathcal {N} (t) = \\mathcal {P} \\cup \\mathcal {D} _ {n} (t); \\quad \\mathcal {P} = \\{1, \\dots , L _ {m} \\}, \\tag {1}\n$$"), Qt::CaseSensitive));
        // The equation must not leak the LaTeX display delimiters.
        QVERIFY(!md.contains(QStringLiteral("\\[")));
        QVERIFY(!md.contains(QStringLiteral("\\]")));
    }

    // Preamble text before the first token is captured as a box (untagged → "text").
    void capturesPreambleAsText() {
        const QString raw = QStringLiteral(
            "Some intro text.\n"
            "title [100, 100, 200, 200]Heading");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);
        QCOMPARE(r.pages.first().boxes.size(), 2);

        const BoundingBox &first = r.pages.first().boxes.at(0);
        QCOMPARE(first.label, QStringLiteral("text"));
        QVERIFY(first.text.contains(QStringLiteral("intro")));

        const BoundingBox &second = r.pages.first().boxes.at(1);
        QCOMPARE(second.label, QStringLiteral("title"));
    }

    // Swapped coordinates (x2 < x1) are normalized correctly with
    // positive width/height regardless of order.
    void normalizesSwappedCoordinates() {
        const QString raw = QStringLiteral(
            "text [200, 300, 100, 100]some text");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);
        QCOMPARE(r.pages.first().boxes.size(), 1);

        const QRectF rect = r.pages.first().boxes.at(0).rect;
        QVERIFY(rect.x() >= 0.0);
        QVERIFY(rect.y() >= 0.0);
        QVERIFY(rect.width() >= 0.0);
        QVERIFY(rect.height() >= 0.0);
        // x = min(200,100)/1000 = 0.1, width = (200-100)/1000 = 0.1
        QVERIFY(qFuzzyCompare(rect.x(), 0.1));
        QVERIFY(qFuzzyCompare(rect.width(), 0.1));
    }
    // The model can emit an HTML <table> inside a table token; it must be
    // rendered as a GFM pipe table. This sample mirrors the real stream in
    // Table_example.txt (rowspan cells, arrows, math in surrounding text).
    void parsesTableBlockIntoMarkdown() {
        const QString raw = QStringLiteral(
            R"(<|det|>title [115, 190, 317, 208]<|/det|>5.3. Subcategory Study
)"
            R"(<|det|>text [113, 389, 885, 456]<|/det|>As shown in Table 2. All metrics are edit distances.
)"
            R"(<|det|>table [137, 467, 865, 611]<|/det|><table><tr><td>Model</td><td>Edit ↓</td><td>PPT</td></tr><tr><td rowspan="2">DS-OCR</td><td>Text</td><td>0.052</td></tr><tr><td>R-order</td><td>0.052</td></tr></table>
)"
            R"(<|det|>page_number [489, 923, 511, 936]<|/det|>10
)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);
        QCOMPARE(r.pages.size(), 1);

        const OcrPage &page = r.pages.first();
        QCOMPARE(page.boxes.size(), 4);
        QCOMPARE(page.boxes.at(2).label, QStringLiteral("table"));

        const QString md = page.text;
        // Header + separator row + data rows as a pipe table.
        QVERIFY(md.contains(QStringLiteral("| Model | Edit ↓ | PPT |")));
        QVERIFY(md.contains(QStringLiteral("| --- | --- | --- |")));
        // A rowspan value is written once (top-left cell); the continuation
        // row keeps the covered column empty so columns still line up.
        QVERIFY(md.contains(QStringLiteral("| DS-OCR | Text | 0.052 |")));
        QVERIFY(md.contains(QStringLiteral("|  | R-order | 0.052 |")));
        // The raw HTML must not leak into the result.
        QVERIFY(!md.contains(QStringLiteral("<table")));
        QVERIFY(!md.contains(QStringLiteral("<tr")));
        QVERIFY(!md.contains(QStringLiteral("<td")));
        QVERIFY(md.contains(QStringLiteral("## 5.3. Subcategory Study")));
        QVERIFY(md.contains(QStringLiteral("*10*")));
    }

    // A table token with colspan keeps the columns aligned in the grid.
    void parsesTableWithColspan() {
        const QString raw = QStringLiteral(
            R"(<|det|>table [0, 0, 100, 100]<|/det|><table><tr><td colspan="2">A</td><td>B</td></tr><tr><td>1</td><td>2</td><td>3</td></tr></table>
)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const QString md = r.pages.first().text;
        // A colspan value is written once (leftmost cell); the covered columns
        // stay empty, keeping the grid aligned with the 3-column data row.
        QVERIFY(md.contains(QStringLiteral("| A |  | B |")));
        QVERIFY(md.contains(QStringLiteral("| 1 | 2 | 3 |")));
        QVERIFY(!md.contains(QStringLiteral("<td")));
    }

    // Real-world table shape: a full-width section-header row (colspan), a
    // rowspan model column with a delta sub-row, and a second section header
    // whose colspan only covers the columns left free by the active rowspan.
    void parsesTableWithSectionHeaders() {
        const QString raw = QStringLiteral(
            R"(<|det|>table [0, 0, 100, 100]<|/det|><table><tr><td>Model</td><td>Size</td><td>Overall ↑</td><td>Read-order ↓</td></tr><tr><td colspan="4">End-to-end Model (v1.5)</td></tr><tr><td>OCRFlux [3]</td><td>3B</td><td>74.82</td><td>0.202</td></tr><tr><td rowspan="3">Unlimited-OCR</td><td rowspan="3">3B-A0.5B</td><td>93.23</td><td>0.045</td></tr><tr><td>↑ 6.22</td><td>↓ 0.041</td></tr><tr><td colspan="2">End-to-end Model (v1.6)</td></tr><tr><td>HunyuanOCR [29]</td><td>1B</td><td>89.95</td><td>0.171</td></tr></table>
)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const QString md = r.pages.first().text;
        QVERIFY(md.contains(QStringLiteral("| Model | Size | Overall ↑ | Read-order ↓ |")));
        // Full-width section header: text in the first column, rest empty
        // (not repeated across every column).
        QVERIFY(md.contains(QStringLiteral("| End-to-end Model (v1.5) |  |  |  |")));
        // Rowspan value appears once; the delta sub-row leaves it blank.
        QVERIFY(md.contains(QStringLiteral("| Unlimited-OCR | 3B-A0.5B | 93.23 | 0.045 |")));
        QVERIFY(md.contains(QStringLiteral("|  |  | ↑ 6.22 | ↓ 0.041 |")));
        // The second section header starts at the first column despite the
        // still-active rowspan, rather than being shifted under it.
        QVERIFY(md.contains(QStringLiteral("| End-to-end Model (v1.6) |  |  |  |")));
        QVERIFY(!md.contains(QStringLiteral("<table")));
        QVERIFY(!md.contains(QStringLiteral("<td")));
    }

    // With "keep tables as HTML" the model's <table> block is passed through
    // verbatim instead of being flattened into a GFM pipe table.
    void keepsTableAsHtmlWhenEnabled() {
        const QString raw = QStringLiteral(
            R"(<|det|>table [0, 0, 100, 100]<|/det|><table><tr><td colspan="2">A</td><td>B</td></tr><tr><td>1</td><td>2</td><td>3</td></tr></table>
)");

        DetTokensParser parser(ParserOptions{true, true});
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const QString md = r.pages.first().text;
        QVERIFY(md.contains(QStringLiteral("<table>")));
        QVERIFY(md.contains(QStringLiteral("colspan=\"2\"")));
        QVERIFY(!md.contains(QStringLiteral("| --- |")));

        // rebuildText() honours the flag too; the default still flattens.
        QVERIFY(parser.rebuildText(r.pages.first()).contains(QStringLiteral("<table>")));
        QVERIFY(DetTokensParser().rebuildText(r.pages.first())
                    .contains(QStringLiteral("| --- |")));
    }

    // Table with inline math and escaped pipe characters inside cells.
    void parsesTableWithMathAndPipes() {
        const QString raw = QStringLiteral(
            R"(<|det|>table [0, 0, 100, 100]<|/det|><table><tr><td>Formula</td><td>Notes</td></tr><tr><td>\( a | b \)</td><td>A | B</td></tr></table>
)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const QString md = r.pages.first().text;
        // The cell with inline math converts \( a | b \) to $a \| b$ (with pipe escaped for table row)
        QVERIFY(md.contains(QStringLiteral(R"($a \| b$)")));
        // The text cell escapes literal pipe
        QVERIFY(md.contains(QStringLiteral(R"(A \| B)")));
    }

    // A wrapped image token with alt text keeps that text as the Markdown alt.
    void imageBlockKeepsAltText() {
        const QString raw = QStringLiteral(
            R"(<|det|>image [100, 200, 300, 400]<|/det|>Figure 1 - Overview\n)"
            R"(<|det|>text [100, 500, 800, 600]<|/det|>Body text\n)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const QString md = r.pages.first().text;
        QVERIFY(md.contains(QStringLiteral("![Figure 1 - Overview](image://ocr/crop/0)")));
    }

    // An image block with no alt text falls back to the default "Image" label.
    void imageBlockWithoutTextGetsDefaultAlt() {
        const QString raw = QStringLiteral("<|det|>image [100, 200, 300, 400]<|/det|>");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const QString md = r.pages.first().text;
        QVERIFY(md.contains(QStringLiteral("![Image](image://ocr/crop/0)")));
    }

    // A chart block behaves exactly like an image block: it keeps any alt text
    // and expands to the same image://ocr/crop/<box> placeholder in Markdown.
    void chartBlockBehavesLikeImage() {
        const QString raw = QStringLiteral(
            R"(<|det|>chart [499, 601, 875, 803]<|/det|>Figure 3 | Latency plot
)"
            R"(<|det|>text [112, 853, 884, 903]<|/det|>Same pattern holds.
)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const OcrPage& page = r.pages.first();
        // The chart token is exposed as a box with label "chart".
        QCOMPARE(page.boxes.at(0).label, QStringLiteral("chart"));
        QVERIFY(page.boxes.size() >= 1);

        const QString md = page.text;
        QVERIFY(md.contains(QStringLiteral("![Figure 3 | Latency plot](image://ocr/crop/0)")));
    }

    // A chart block with no text falls back to the default "Image" alt.
    void chartBlockWithoutTextGetsDefaultAlt() {
        const QString raw = QStringLiteral("<|det|>chart [499, 601, 875, 803]<|/det|>");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const QString md = r.pages.first().text;
        QVERIFY(md.contains(QStringLiteral("![Image](image://ocr/crop/0)")));
    }

    // After a box is removed, rebuildText must re-index the image URLs so
    // they keep pointing at the right boxes.
    void rebuildTextShiftsImageIndices() {
        const QString raw = QStringLiteral(
            "image [0, 0, 100, 100]\n"
            "text [0, 200, 100, 300]Body\n"
            "image [0, 400, 100, 500]\n");

        DetTokensParser parser;
        OcrResult r = parser.parse(raw);
        QVERIFY(r.success);
        OcrPage page = r.pages.first();

        QVERIFY(page.text.contains(QStringLiteral("![Image](image://ocr/crop/0)")));
        QVERIFY(page.text.contains(QStringLiteral("![Image](image://ocr/crop/2)")));

        // Drop the first image and regenerate the text from the remaining boxes.
        page.boxes.removeAt(0);
        const QString rebuilt = parser.rebuildText(page);

        QVERIFY(rebuilt.contains(QStringLiteral("Body")));
        // The remaining image now sits at index 1.
        QVERIFY(rebuilt.contains(QStringLiteral("![Image](image://ocr/crop/1)")));
        QVERIFY(!rebuilt.contains(QStringLiteral("image://ocr/crop/0")));
        QVERIFY(!rebuilt.contains(QStringLiteral("image://ocr/crop/2")));
    }

    // The model labels bibliography entries "ref_text". They are ordinary
    // paragraphs (not headings/italics) that carry trailing `\n` escapes like
    // any other wrapped block. Mirrors the real reference-list stream
    // (reftext_example.txt): several ref_text blocks then a page_number tail.
    void parsesReferenceTextBlocks() {
        const QString raw = QStringLiteral(
            R"(<|det|>ref_text [115, 101, 885, 135]<|/det|>[31] W. Wang, Z. Gao, L. Gu, et al. Internvl3.5: Advancing open-source multimodal models in versatility, reasoning, and efficiency. arXiv preprint arXiv:2508.18265, 2025.\n)"
            R"(<|det|>ref_text [115, 144, 885, 194]<|/det|>[32] H. Wei, L. Kong, J. Chen, L. Zhao, Z. Ge, J. Yang, J. Sun, C. Han, and X. Zhang. Vary: Scaling up the vision vocabulary for large vision-language model. In European Conference on Computer Vision, pages 408–424. Springer, 2024.\n)"
            R"(<|det|>page_number [489, 923, 511, 935]<|/det|>14)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);
        QCOMPARE(r.pages.size(), 1);

        const OcrPage& page = r.pages.first();
        QCOMPARE(page.boxes.size(), 3);

        // The reference entry keeps its "ref_text" label.
        const BoundingBox& ref = page.boxes.at(0);
        QCOMPARE(ref.label, QStringLiteral("ref_text"));
        // The trailing \n escape is decoded, then trimmed away.
        QCOMPARE(ref.text, QStringLiteral(
            "[31] W. Wang, Z. Gao, L. Gu, et al. Internvl3.5: Advancing open-source multimodal models in versatility, reasoning, and efficiency. arXiv preprint arXiv:2508.18265, 2025."));
        // Coordinates are normalized from the raw 0-1000 pixel range.
        QVERIFY(qFuzzyCompare(ref.rect.y(), 0.101));
        QVERIFY(qFuzzyCompare(ref.rect.height(), 0.034));

        // The page-number tail is picked up after the reference entries.
        QCOMPARE(page.boxes.at(2).label, QStringLiteral("page_number"));
        QCOMPARE(page.boxes.at(2).text, QStringLiteral("14"));

        // References render as plain paragraphs — no heading/italic markers.
        const QString md = page.text;
        QVERIFY(md.contains(QStringLiteral("Vary: Scaling up the vision vocabulary")));
        QVERIFY(!md.contains(QStringLiteral("## [31]")));
        QVERIFY(!md.contains(QStringLiteral("*[31]")));
        QVERIFY(md.contains(QStringLiteral("*14*")));
        QVERIFY(!md.contains(QStringLiteral("<|ref")));
    }

    // The model sometimes glitches and emits a near-duplicate block with an
    // almost-identical bbox (within ±10 px). The parser must detect this,
    // replace the earlier block with the later one, and flag the page.
    void detectsAndReplacesNearDuplicateBboxes() {
        // Simulates the garbled-duplicate-then-correct pattern:
        // - First "text" at [112,838,884,903] is a garbled repeat of earlier text.
        // - "equation" at [437,795,885,827] then a near-duplicate at [437,794,885,829].
        // - Second "text" at [112,838,884,903] is the correct continuation.
        const QString raw = QStringLiteral(
            R"(<|det|>text [112, 738, 883, 786]<|/det|>Original text block.\n)"
            R"(<|det|>text [112, 838, 884, 903]<|/det|>Garbled duplicate of original.\n)"
            R"(<|det|>equation [437, 795, 885, 827]<|/det|>\[\mathbf{o}_t = \sum \alpha_{tj} \mathbf{v}_j\]\n)"
            R"(<|det|>text [112, 838, 884, 903]<|/det|>Correct continuation text.\n)"
            R"(<|det|>equation [437, 794, 885, 829]<|/det|>\[\mathbf{o}_t = \sum \alpha_{tj} \mathbf{v}_j. \tag{4}\]\n)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);
        QCOMPARE(r.pages.size(), 1);

        const OcrPage& page = r.pages.first();

        // The two near-duplicate pairs reduce the 5 raw tokens to 3 boxes:
        //   [0] text [112,738,883,786] — no duplicate, stays as-is
        //   [1] text [112,838,884,903] — first occurrence replaced by second
        //   [2] equation [437,795,885,827] — first occurrence replaced by second [437,794,885,829]
        QCOMPARE(page.boxes.size(), 3);

        // The duplicated "text" box was replaced — should now contain the
        // *second* text ("Correct continuation text.").
        QCOMPARE(page.boxes.at(1).text, QStringLiteral("Correct continuation text."));

        // The duplicated "equation" box was replaced — should now be the
        // *second* equation variant.
        QVERIFY(page.boxes.at(2).text.contains(QStringLiteral("tag{4}")));

        // hasDuplicates must be true.
        QVERIFY(page.hasDuplicates);

        // The Markdown output must not contain the garbled text.
        const QString md = page.text;
        QVERIFY(!md.contains(QStringLiteral("Garbled duplicate")));
        QVERIFY(md.contains(QStringLiteral("Correct continuation")));
    }

    // --- Parser options (ADR 88) ---------------------------------------------

    // A reply with no layout tokens still parses (the text is kept as one
    // block) but records a diagnostic — this is what the footer shows when a
    // model/parser pair does not match.
    void reportsNoTokensAsDiagnostic() {
        const QString raw = QStringLiteral(
            "Sure! Here is the text of the page you asked for, transcribed "
            "in plain paragraphs without any layout markup at all.");
        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);

        QVERIFY(r.success);
        QCOMPARE(r.pages.size(), 1);
        QVERIFY(r.pages.first().text.contains(QStringLiteral("plain paragraphs")));
        QCOMPARE(r.notes.size(), 1);
        QVERIFY(r.notes.first().contains(QStringLiteral("No layout tokens")));
    }

    // A short non-layout answer ("OK") is not a parse failure, so no noise.
    void shortReplyWithoutTokensIsSilent() {
        DetTokensParser parser;
        const OcrResult r = parser.parse(QStringLiteral("OK"));
        QVERIFY(r.success);
        QVERIFY(r.notes.isEmpty());
    }

    // A well-formed reply produces no diagnostics.
    void tokenizedReplyHasNoNotes() {
        DetTokensParser parser;
        const OcrResult r = parser.parse(
            QStringLiteral("text [10, 10, 400, 100]A paragraph of recognized text.\n"));
        QVERIFY(r.success);
        QVERIFY(r.notes.isEmpty());
    }

    // bboxRange is the model's coordinate scale: 0-10000 coordinates must
    // normalize into the same [0, 1] rects as the usual 0-1000 space.
    void honorsBboxRange() {
        const QString raw = QStringLiteral(
            "text [100, 200, 4000, 3000]Wide scale body text.\n");

        ParserOptions wide;
        wide.bboxRange = 10000;
        const OcrResult r = DetTokensParser(wide).parse(raw);

        QVERIFY(r.success);
        const OcrPage &page = r.pages.first();
        QCOMPARE(page.boxes.size(), 1);
        QVERIFY(qFuzzyCompare(page.boxes.first().rect.x(), 0.01));
        QVERIFY(qFuzzyCompare(page.boxes.first().rect.y(), 0.02));
        QVERIFY(qFuzzyCompare(page.boxes.first().rect.width(), 0.39));
        QVERIFY(qFuzzyCompare(page.boxes.first().rect.height(), 0.28));
    }

    // rebuildText is the parser's own contract: the raw parser has no
    // fragments, so it must return the page text rather than an empty string.
    void rawParserRebuildsToPageText() {
        const RawParser parser;
        OcrPage page;
        page.text = QStringLiteral("just text");
        QCOMPARE(parser.rebuildText(page), QStringLiteral("just text"));
        QCOMPARE(parser.displayName(), QStringLiteral("Raw text"));
    }

    // Every registered model id must resolve to a parser that exists —
    // catches a new adapter shipping with a parser id nobody registered.
    void registeredParsersAreCreatable() {
        const QStringList ids = ParserFactory::registeredIds();
        QVERIFY(ids.contains(QStringLiteral("det_tokens")));
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
    void autoIdIsSelectableButNeverCreatesAParser() {
        QCOMPARE(ParserFactory::selectableIds().first(), ParserFactory::kAutoId);
        QCOMPARE(ParserFactory::selectableIds().size(),
                 ParserFactory::selectableDisplayNames().size());
        QVERIFY(!ParserFactory::create(ParserFactory::kAutoId));

        // An unregistered id still degrades to 'raw' rather than crashing.
        const auto fallback = ParserFactory::create(QStringLiteral("no-such-parser"));
        QVERIFY(fallback);
        QCOMPARE(fallback->id(), QStringLiteral("raw"));
    }

    // --- Label map (resources/profiles/labels.json, ADR 88) ------------------

    // The unit-test binary carries no resource bundle, so the compiled-in
    // fallback table must behave like the shipped one for the shared labels.
    void builtInLabelMapStylesSharedLabels() {
        QCOMPARE(blockStyleForLabel(QStringLiteral("title")).style, BlockStyle::Heading);
        QCOMPARE(blockStyleForLabel(QStringLiteral("table")).style, BlockStyle::Table);
        QCOMPARE(blockStyleForLabel(QStringLiteral("equation")).style, BlockStyle::Equation);
        QCOMPARE(blockStyleForLabel(QStringLiteral("image")).style,
                 BlockStyle::ImagePlaceholder);
        QCOMPARE(blockStyleForLabel(QStringLiteral("page_caption")).style,
                 BlockStyle::PlainText);
    }

    // A JSON document overrides the built-in table, per model id.
    void jsonOverridesApplyPerModel() {
        BlockStyleMap map;
        QJsonObject root;
        QJsonObject defaults;
        defaults.insert(QStringLiteral("title"), QStringLiteral("text"));
        defaults.insert(QStringLiteral("image"), QStringLiteral("image"));
        defaults.insert(QStringLiteral("image_block"), QStringLiteral("image"));
        root.insert(QStringLiteral("default"), defaults);
        QJsonObject overrides;
        QJsonObject lfm;
        lfm.insert(QStringLiteral("image_block"), QStringLiteral("text"));
        lfm.insert(QStringLiteral("code_caption"), QStringLiteral("italic"));
        overrides.insert(QStringLiteral("lfm25-vl-3b"), lfm);
        root.insert(QStringLiteral("overrides"), overrides);
        map.applyJson(root);

        // The default table replaced the built-in one...
        QCOMPARE(map.styleForLabel(QStringLiteral("title")).style, BlockStyle::PlainText);
        QCOMPARE(map.styleForLabel(QStringLiteral("image_block")).style,
                 BlockStyle::ImagePlaceholder);
        // ...and the model override wins for the named model only.
        QCOMPARE(map.styleForLabel(QStringLiteral("image_block"), QStringLiteral("lfm25-vl-3b")).style,
                 BlockStyle::PlainText);
        QCOMPARE(map.styleForLabel(QStringLiteral("code_caption"), QStringLiteral("lfm25-vl-3b")).style,
                 BlockStyle::Italic);
        // A label the override does not name still falls through to the default.
        QCOMPARE(map.styleForLabel(QStringLiteral("image"), QStringLiteral("lfm25-vl-3b")).style,
                 BlockStyle::ImagePlaceholder);
    }

    // The shipped labels.json must parse and must cover the vocabulary the
    // LFM2.5-VL prompt advertises.
    void shippedLabelMapCoversLfmVocabulary() {
        QFile file(QStringLiteral(":/profiles/labels.json"));
        if (!file.open(QIODevice::ReadOnly))
            QSKIP("labels.json resource not linked into this test binary");
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());

        BlockStyleMap map;
        map.applyJson(doc.object());

        // Every label the LFM2.5-VL prompt advertises must be named explicitly
        // (defaults or overrides) — a typo must not degrade it to plain text.
        const QJsonObject defaults = doc.object().value(QStringLiteral("default")).toObject();
        const QJsonObject lfm = doc.object().value(QStringLiteral("overrides")).toObject()
                                    .value(QStringLiteral("lfm25-vl-3b")).toObject();
        const QStringList lfmLabels{
            QStringLiteral("text"),      QStringLiteral("title"),     QStringLiteral("list"),
            QStringLiteral("table"),     QStringLiteral("table_caption"),
            QStringLiteral("table_footnote"), QStringLiteral("image"),
            QStringLiteral("image_block"), QStringLiteral("image_caption"),
            QStringLiteral("image_footnote"), QStringLiteral("chart"),
            QStringLiteral("equation"),  QStringLiteral("formula_number"),
            QStringLiteral("code"),      QStringLiteral("code_caption"),
            QStringLiteral("algorithm"), QStringLiteral("aside_text"),
            QStringLiteral("ref_text"),  QStringLiteral("phonetic"),
            QStringLiteral("page_header"), QStringLiteral("page_footer"),
            QStringLiteral("page_number"), QStringLiteral("page_footnote")};
        for (const QString &label : lfmLabels) {
            const bool named = defaults.contains(label) || lfm.contains(label);
            QVERIFY2(named, qPrintable(QStringLiteral("unmapped label: %1").arg(label)));
        }

        // The labels that must NOT degrade to plain text.
        QCOMPARE(map.styleForLabel(QStringLiteral("image_block"), QStringLiteral("lfm25-vl-3b")).style,
                 BlockStyle::ImagePlaceholder);
        QCOMPARE(map.styleForLabel(QStringLiteral("code_caption"), QStringLiteral("lfm25-vl-3b")).style,
                 BlockStyle::Italic);
        QCOMPARE(map.styleForLabel(QStringLiteral("table"), QStringLiteral("lfm25-vl-3b")).style,
                 BlockStyle::Table);
    }

};

QTEST_MAIN(TestDetParser)
#include "test_det_parser.moc"