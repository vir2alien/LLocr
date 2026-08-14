#include <QtTest>

#include "core/OcrResult.h"
#include "parsers/DetTokensParser.h"

using namespace llocr;

class TestDetParser : public QObject {
    Q_OBJECT

private slots:
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
        QVERIFY(md.contains("![Image]()"));                    // image placeholder
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
};

QTEST_MAIN(TestDetParser)
#include "test_det_parser.moc"