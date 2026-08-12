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
};

QTEST_MAIN(TestDetParser)
#include "test_det_parser.moc"