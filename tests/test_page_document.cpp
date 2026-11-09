#include <QtTest>

#include "core/BoxTransitions.h"
#include "core/OcrResult.h"
#include "parsers/BlockRender.h"
#include "parsers/ParserOptions.h"

using namespace llocr;

namespace {

OcrPage twoBlockPage()
{
    OcrPage page;
    BoundingBox title;
    title.label = QStringLiteral("title");
    title.text = QStringLiteral("Intro");
    page.boxes.append(title);
    BoundingBox body;
    body.label = QStringLiteral("text");
    body.text = QStringLiteral("Body text");
    page.boxes.append(body);
    return page;
}

}  // namespace

class TestPageDocument : public QObject
{
    Q_OBJECT

private slots:
    // The document is the join of the blocks' document text: canonical when
    // the user's layer is empty, the user's markdown verbatim when it is not.
    void documentFollowsUserTextPerBlock()
    {
        const ParserOptions options;
        OcrPage page = twoBlockPage();

        const auto canonical = renderPageText(page, options);
        QCOMPARE(canonical.text, QStringLiteral("## Intro\n\nBody text"));
        QCOMPARE(canonical.ranges.size(), 2);

        // The user rewrites the second block; the first stays canonical.
        BoxTransitions::setUserText(page.boxes[1], QStringLiteral("Body text, mine"));
        const auto edited = renderPageText(page, options);
        QCOMPARE(edited.text, QStringLiteral("## Intro\n\nBody text, mine"));
        QCOMPARE(edited.ranges.at(1).boxIndex, 1);
        QCOMPARE(edited.text.mid(edited.ranges.at(1).start, edited.ranges.at(1).length), QStringLiteral("Body text, mine"));

        // Writing back the canonical text releases the layer.
        BoxTransitions::setUserText(page.boxes[1], QStringLiteral("Body text"));
        QVERIFY(page.boxes[1].userText.isEmpty());
        QCOMPARE(renderPageText(page, options).text, QStringLiteral("## Intro\n\nBody text"));

        // An empty user text is a release too.
        BoxTransitions::setUserText(page.boxes[1], QStringLiteral("Body text, mine"));
        BoxTransitions::setUserText(page.boxes[1], QString());
        QVERIFY(page.boxes[1].userText.isEmpty());
    }

    // Legacy projects carry one page text with the user's edits already in it;
    // the migration maps that text back into per-block layers. Blocks whose
    // fragment matches the canonical render stay layerless; the edited one
    // takes its document fragment as userText.
    void legacyTextMigratesIntoUserLayers()
    {
        const ParserOptions options;
        OcrPage page = twoBlockPage();

        const QString legacy = QStringLiteral("## Intro\n\nBody text, rewritten by me");
        const bool mapped = migrateLegacyPageText(page, legacy, options);
        QVERIFY(mapped);
        QVERIFY(page.boxes[0].userText.isEmpty());
        QCOMPARE(page.boxes[1].userText, QStringLiteral("Body text, rewritten by me"));
        QCOMPARE(renderPageText(page, options).text, legacy);
    }

    // A legacy text the blocks cannot reproduce in order (blocks shuffled by
    // an old save) does not map: nothing is layered, the canonical document
    // stands.
    void unmappableLegacyTextLeavesCanonicalDocument()
    {
        const ParserOptions options;
        OcrPage page = twoBlockPage();

        const bool mapped = migrateLegacyPageText(page, QStringLiteral("Body text\n\n# Intro"), options);
        QVERIFY(!mapped);
        QVERIFY(page.boxes[0].userText.isEmpty());
        QVERIFY(page.boxes[1].userText.isEmpty());
    }

    // A paragraph the user appended after the last block joins that block's
    // layer, so nothing the user wrote is dropped.
    void appendedTailJoinsTheLastBlock()
    {
        const ParserOptions options;
        OcrPage page = twoBlockPage();

        const QString legacy = QStringLiteral("## Intro\n\nBody text\n\nAn extra paragraph");
        QVERIFY(migrateLegacyPageText(page, legacy, options));
        QCOMPARE(page.boxes[0].userText, QString());
        QCOMPARE(page.boxes[1].userText, QStringLiteral("Body text\n\nAn extra paragraph"));
        QCOMPARE(renderPageText(page, options).text, legacy);
    }
};

QTEST_GUILESS_MAIN(TestPageDocument)
#include "test_page_document.moc"
