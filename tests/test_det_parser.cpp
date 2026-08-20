#include <QtTest>

#include "core/OcrResult.h"
#include "parsers/DetTokensParser.h"

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

    // Escaped newlines and doubled backslashes are decoded inside token content:
    // `\n` becomes a real newline and `\\(` becomes `\(`, which then converts
    // to inline math `$...$`.
    void unescapesWrappedContent() {
        const QString raw = QStringLiteral(
            R"(<|det|>text [112, 132, 884, 309]<|/det|>line one\nline two  \\( m + n \\)\n)"
            R"(<|det|>text [113, 780, 884, 860]<|/det|>see  \\( [10, 30, 33, 34] \\)\n)");

        DetTokensParser parser;
        const OcrResult r = parser.parse(raw);
        QVERIFY(r.success);

        const OcrPage& page = r.pages.first();
        QCOMPARE(page.boxes.size(), 2);
        // \n -> real newline; \\( -> \( inside the box text.
        QCOMPARE(page.boxes.at(0).text, QStringLiteral("line one\nline two  \\( m + n \\)"));

        const QString md = page.text;
        QVERIFY(md.contains(QStringLiteral("$m + n$")));
        QVERIFY(md.contains(QStringLiteral("$[10, 30, 33, 34]$")));
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
        // rowspan cell is reproduced on both data rows so columns line up.
        QVERIFY(md.contains(QStringLiteral("| DS-OCR | Text | 0.052 |")));
        QVERIFY(md.contains(QStringLiteral("| DS-OCR | R-order | 0.052 |")));
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
        // colspan=2 repeats the value across two columns (GFM has no colspan
        // natively), keeping the grid aligned with the 3-column data row.
        QVERIFY(md.contains(QStringLiteral("| A | A | B |")));
        QVERIFY(md.contains(QStringLiteral("| 1 | 2 | 3 |")));
        QVERIFY(!md.contains(QStringLiteral("<td")));
    }

    // Table with inline math and escaped pipe characters inside cells.
    void parsesTableWithMathAndPipes() {
        const QString raw = QStringLiteral(
            R"(<|det|>table [0, 0, 100, 100]<|/det|><table><tr><td>Formula</td><td>Notes</td></tr><tr><td>\\( a | b \\)</td><td>A | B</td></tr></table>
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

    // After a box is removed, rebuildPageText must re-index the image URLs so
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
        const QString rebuilt = rebuildPageText(page);

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
            R"(<|det|>equation [437, 795, 885, 827]<|/det|>\\[\\mathbf{o}_t = \\sum \\alpha_{tj} \\mathbf{v}_j\\]\n)"
            R"(<|det|>text [112, 838, 884, 903]<|/det|>Correct continuation text.\n)"
            R"(<|det|>equation [437, 794, 885, 829]<|/det|>\\[\\mathbf{o}_t = \\sum \\alpha_{tj} \\mathbf{v}_j. \\tag{4}\\]\n)");

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

        // Markdown output must not contain the garbled text.
        const QString md = page.text;
        QVERIFY(!md.contains(QStringLiteral("Garbled duplicate")));
        QVERIFY(md.contains(QStringLiteral("Correct continuation")));
    }

};

QTEST_MAIN(TestDetParser)
#include "test_det_parser.moc"