// The block panel's header: the name of the block in the interface language,
// the state of the block, the switch of the layers and the actions. The
// divergence notice and the action that settles it stand under the header
// row — the row has no room for them (ADR 323).
//
// The panel reads the block's state from the controller singleton; the test
// registers a double (tests/fake_block_controller.qml) with a recognized block,
// a correction and a document that diverges from it.

#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>

class TestBlockPanelQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void theNameOfTheBlockIsInTheInterfaceLanguage();
    void theSwitchHasThreeSegments();
    void theSwitchIsGoneWhenOnlyTheMarkupIsThere();
    void theSegmentsSayWhenThereIsNoSecondVariant();
    void theNoteStandsWithItsLayer();
    void theSwitchMovesTheDocument();
    void thePanelSwitchesToACorrectionThatLandsOnTheBlock();
    void theComparisonShowsBothLayers();
    void theComparisonTintsTheDiff();
    void theResetActionIsGone();
    void theNoticeStandsUnderTheText();
    void theSwitchGoesToTheNextLineWhenTheHeaderDoesNotFit();
    void theApproveActionIsNamed();
    void theStateFollowsTheVerdict();

private:
    std::unique_ptr<QObject> loadPanel();
    QList<QObject *> findAllByClassName(QObject *root, const QLatin1String &className);
    QObject *childWithText(QObject *root, const QLatin1String &className, const QString &text);
    QObject *theSwitch(QObject *root);
    QObject *thePanelWithTitle(QObject *root, const QString &title);
    bool isVisible(QObject *item);
    void clickSegment(QObject *switchControl, int index);

    QString m_qmlRoot;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_parent = nullptr;
    QObject *m_controller = nullptr;
    QStringList m_warnings;
};

void TestBlockPanelQml::initTestCase()
{
    // The app runs the Fusion style, and the native macOS style warns about
    // every customized control.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!m_qmlRoot.isEmpty());

    const QString fake = QFINDTESTDATA(QStringLiteral("fake_block_controller.qml"));
    QVERIFY(!fake.isEmpty());

    // The theme, the names of the blocks, the panel of a text layer and the
    // controller double are loaded from the source tree, under the module the
    // app uses.
    const auto registerFile = [](const QString &path, const char *name, bool singleton) {
        const QUrl url = QUrl::fromLocalFile(path);
        if (singleton)
            qmlRegisterSingletonType(url, "LLocr", 1, 0, name);
        else
            qmlRegisterType(url, "LLocr", 1, 0, name);
    };
    registerFile(m_qmlRoot + QStringLiteral("/Theme.qml"), "Theme", true);
    registerFile(m_qmlRoot + QStringLiteral("/BlockNames.qml"), "BlockNames", true);
    registerFile(fake, "Controller", true);
    registerFile(m_qmlRoot + QStringLiteral("/MainWindow/BlockTextPanel.qml"), "BlockTextPanel", false);

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    m_window = std::make_unique<QQuickWindow>();
    m_parent = new QQuickItem(m_window->contentItem());
    m_parent->setSize(QSizeF(700, 320));
    m_window->resize(700, 320);
    m_window->show();

    m_controller = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Controller"));
    QVERIFY(m_controller);
}

std::unique_ptr<QObject> TestBlockPanelQml::loadPanel()
{
    m_warnings.clear();

    const QString path = QDir(m_qmlRoot).filePath(QStringLiteral("MainWindow/BlockEditPanel.qml"));
    QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
    if (!component.isReady()) {
        m_warnings.append(component.errorString());
        return nullptr;
    }

    std::unique_ptr<QObject> object(component.create());
    if (!object) {
        m_warnings.append(component.errorString());
        return nullptr;
    }
    if (auto *item = qobject_cast<QQuickItem *>(object.get())) {
        item->setParentItem(m_parent);
        item->setWidth(700);
        item->setHeight(320);
    }
    // The layout is applied in the polish: let it happen before the test
    // measures the row.
    QTest::qWait(20);
    return object;
}

QList<QObject *> TestBlockPanelQml::findAllByClassName(QObject *root, const QLatin1String &className)
{
    QList<QObject *> found;
    const QList<QObject *> children = root->children();
    for (QObject *child : children) {
        // A QML type's class name carries the engine's suffix
        // ("LLOButton_QMLTYPE_23"), so the name is matched by its prefix.
        const QString name = QString::fromLatin1(child->metaObject()->className());
        if (name == className || name.startsWith(className + QStringLiteral("_QMLTYPE")))
            found.append(child);
        found.append(findAllByClassName(child, className));
    }
    return found;
}

QObject *TestBlockPanelQml::childWithText(QObject *root, const QLatin1String &className, const QString &text)
{
    const QList<QObject *> candidates = findAllByClassName(root, className);
    for (QObject *candidate : candidates) {
        if (candidate->property("text").toString() == text)
            return candidate;
    }
    return nullptr;
}

// The switch of the layers: the panel carries exactly one — the visible one.
// The panel keeps two copies of it (the header row and the row below it),
// and only one of them stands at a time.
QObject *TestBlockPanelQml::theSwitch(QObject *root)
{
    const QList<QObject *> candidates = findAllByClassName(root, QLatin1String("SegmentedControl"));
    for (QObject *candidate : candidates) {
        if (isVisible(candidate))
            return candidate;
    }
    return candidates.isEmpty() ? nullptr : candidates.first();
}

// The panel of a text layer by its title.
QObject *TestBlockPanelQml::thePanelWithTitle(QObject *root, const QString &title)
{
    const QList<QObject *> candidates = findAllByClassName(root, QLatin1String("BlockTextPanel"));
    for (QObject *candidate : candidates) {
        if (candidate->property("title").toString() == title)
            return candidate;
    }
    return nullptr;
}

// The item's own flag: an element the panel hides takes no place in the row.
bool TestBlockPanelQml::isVisible(QObject *item)
{
    return item && item->property("visible").toBool();
}

// The switch reports the choice; the panel follows it.
void TestBlockPanelQml::clickSegment(QObject *switchControl, int index)
{
    QVERIFY(switchControl);
    QMetaObject::invokeMethod(switchControl, "activated", Q_ARG(int, index));
    QTest::qWait(20);
}

// The panel names the block in the language of the interface: the label of the
// model («equation») is not what the operator reads.
void TestBlockPanelQml::theNameOfTheBlockIsInTheInterfaceLanguage()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *name = childWithText(panel.get(), QLatin1String("LLOLabel"), QStringLiteral("Equation"));
    QVERIFY2(name, "the panel must name the block in the interface language");
    QVERIFY2(!childWithText(panel.get(), QLatin1String("LLOLabel"), QStringLiteral("equation")), "the label of the model must not stand in the panel");
}

// The switch has three segments: the recognized text, the correction and the
// comparison of both.
void TestBlockPanelQml::theSwitchHasThreeSegments()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *switchControl = theSwitch(panel.get());
    QVERIFY2(switchControl, "the panel must carry the switch of the layers");

    const QVariantList options = switchControl->property("options").toList();
    QCOMPARE(options.size(), 3);
    QCOMPARE(options.at(0).toMap().value("label").toString(), QStringLiteral("OCR"));
    QCOMPARE(options.at(1).toMap().value("label").toString(), QStringLiteral("Correction"));
    QCOMPARE(options.at(2).toMap().value("label").toString(), QStringLiteral("Comparison"));
}

// A block that carries the markup alone has nothing to switch between: the
// switch of the layers is gone.
void TestBlockPanelQml::theSwitchIsGoneWhenOnlyTheMarkupIsThere()
{
    m_controller->setProperty("selectedBlockText", "");
    m_controller->setProperty("selectedBlockCorrected", "");

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *switchControl = theSwitch(panel.get());
    QVERIFY2(switchControl, "the panel must keep the switch in the tree");
    const bool switchVisible = isVisible(switchControl);

    m_controller->setProperty("selectedBlockText", "E = mc^2");
    m_controller->setProperty("selectedBlockCorrected", "E = mc^2 + 1");

    QVERIFY2(!switchVisible, "the switch must be gone where there is nothing to switch between");
}

// Without a correction there is no second variant: the segments that need one
// say so instead of answering a click.
void TestBlockPanelQml::theSegmentsSayWhenThereIsNoSecondVariant()
{
    m_controller->setProperty("selectedBlockCorrected", "");

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *switchControl = theSwitch(panel.get());
    QVERIFY(switchControl);
    QVERIFY(isVisible(switchControl));

    const QVariantList options = switchControl->property("options").toList();
    QCOMPARE(options.size(), 3);
    const bool ocrEnabled = options.at(0).toMap().value("enabled").toBool();
    const bool correctionEnabled = options.at(1).toMap().value("enabled").toBool();
    const bool comparisonEnabled = options.at(2).toMap().value("enabled").toBool();

    m_controller->setProperty("selectedBlockCorrected", "E = mc^2 + 1");

    QCOMPARE(ocrEnabled, true);
    QCOMPARE(correctionEnabled, false);
    QCOMPARE(comparisonEnabled, false);
}

// The fixer's answer speaks about the recognized text, so it stands with it:
// in the header of the OCR panel, not in the header of the module.
void TestBlockPanelQml::theNoteStandsWithItsLayer()
{
    m_controller->setProperty("selectedBlockFixStatus", 1);
    m_controller->setProperty("selectedBlockCorrected", "");

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("Recognized text (OCR)"));
    QVERIFY2(original, "the panel must show the recognized text");
    QCOMPARE(original->property("noteText").toString(), QStringLiteral("No correction required"));

    m_controller->setProperty("selectedBlockFixStatus", 0);
    m_controller->setProperty("selectedBlockCorrected", "E = mc^2 + 1");
    m_controller->setProperty("selectedBlockCheckStatus", 1);
    m_controller->setProperty("selectedBlockConfidence", 0.9);
}

// The switch moves the document: the layer the operator reads is the layer
// the document carries (ADR 164).
void TestBlockPanelQml::theSwitchMovesTheDocument()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *switchControl = theSwitch(panel.get());
    QVERIFY(switchControl);
    QCOMPARE(m_controller->property("selectedBlockUsesCorrected").toBool(), false);

    clickSegment(switchControl, 1);
    QCOMPARE(m_controller->property("selectedBlockUsesCorrected").toBool(), true);

    clickSegment(switchControl, 0);
    QCOMPARE(m_controller->property("selectedBlockUsesCorrected").toBool(), false);
}

// The fix and the read land a fresh correction on the block in view and move
// the block to it: the panel follows, without the block being chosen again
// (ADR 185).
void TestBlockPanelQml::thePanelSwitchesToACorrectionThatLandsOnTheBlock()
{
    // The panel is open on a block that carries its recognized text alone.
    m_controller->setProperty("selectedBlockCorrected", "");
    m_controller->setProperty("selectedBlockUsesCorrected", false);

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *switchControl = theSwitch(panel.get());
    QVERIFY(switchControl);
    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("Recognized text (OCR)"));
    QVERIFY2(original, "the panel must carry the panel of the recognized text");
    QObject *correction = thePanelWithTitle(panel.get(), QStringLiteral("Correction (AI)"));
    QVERIFY2(correction, "the panel must carry the panel of the correction");
    QVERIFY(isVisible(original));
    QVERIFY(!isVisible(correction));
    QCOMPARE(switchControl->property("currentIndex").toInt(), 0);

    // The fix lands on the block in view: a fresh correction, and the block
    // points at it.
    m_controller->setProperty("selectedBlockCorrected", "E = mc^2 + 1");
    m_controller->setProperty("selectedBlockUsesCorrected", true);
    QTest::qWait(20);

    const bool correctionShown = isVisible(correction);
    const bool originalShown = isVisible(original);
    const int segment = switchControl->property("currentIndex").toInt();

    m_controller->setProperty("selectedBlockUsesCorrected", false);

    QVERIFY2(correctionShown, "the panel must switch to the correction that landed on the block");
    QVERIFY2(!originalShown, "the recognized text must give way to the correction that landed");
    QCOMPARE(segment, 1);
}

// The comparison shows both layers, one above the other.
void TestBlockPanelQml::theComparisonShowsBothLayers()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *switchControl = theSwitch(panel.get());
    QVERIFY(switchControl);

    clickSegment(switchControl, 2);

    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("Recognized text (OCR)"));
    QVERIFY2(original, "the comparison must show the recognized text");
    QVERIFY(isVisible(original));

    QObject *correction = thePanelWithTitle(panel.get(), QStringLiteral("Correction (AI)"));
    QVERIFY2(correction, "the comparison must show the correction");
    QVERIFY(isVisible(correction));

    // One above the other: the correction stands below the recognized text.
    auto *originalItem = qobject_cast<QQuickItem *>(original);
    auto *correctionItem = qobject_cast<QQuickItem *>(correction);
    QVERIFY(originalItem);
    QVERIFY(correctionItem);
    QVERIFY2(correctionItem->y() > originalItem->y(), "the correction must stand below the recognized text");
}

// The tinting of the diff belongs to the comparison: a single layer is read
// clean (ADR 323).
void TestBlockPanelQml::theComparisonTintsTheDiff()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *switchControl = theSwitch(panel.get());
    QVERIFY(switchControl);

    QObject *correction = thePanelWithTitle(panel.get(), QStringLiteral("Correction (AI)"));
    QVERIFY(correction);
    QVERIFY2(!correction->property("html").toString().contains("background-color"), "a single layer must be read clean");

    clickSegment(switchControl, 2);
    QVERIFY2(correction->property("html").toString().contains("background-color"), "the comparison must tint what the correction changed");
}

// The reset of the correction is gone: a repeated fix and the choice of the
// layer make it needless.
void TestBlockPanelQml::theResetActionIsGone()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY2(!childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("Reset correction")), "the panel must not offer to reset the correction");
}

// The notice and the action that settle it stand under the text: the text
// comes first, the divergence of the document and the way to settle it are
// read after it.
void TestBlockPanelQml::theNoticeStandsUnderTheText()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *notice = childWithText(panel.get(), QLatin1String("LLOLabel"), QStringLiteral("The document text differs from the block"));
    QVERIFY2(notice, "the panel must tell that the document differs from the block");

    QObject *restore = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("Restore the block text"));
    QVERIFY2(restore, "the panel must offer to restore the block's text");

    QObject *close = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("✕"));
    QVERIFY(close);

    auto *noticeItem = qobject_cast<QQuickItem *>(notice);
    auto *restoreItem = qobject_cast<QQuickItem *>(restore);
    auto *closeItem = qobject_cast<QQuickItem *>(close);
    QVERIFY(noticeItem);
    QVERIFY(restoreItem);
    QVERIFY(closeItem);

    // The notice stands in a row of its own — in the same column of the
    // panel as the header row and the text.
    QQuickItem *headerRow = closeItem->parentItem();
    QVERIFY(headerRow);
    QVERIFY2(noticeItem->parentItem() != headerRow, "the notice must not crowd the header row");
    QCOMPARE(noticeItem->parentItem()->parentItem(), headerRow->parentItem());
    QCOMPARE(restoreItem->parentItem(), noticeItem->parentItem());

    // The notice stands below the text of the block, not above it.
    QObject *textPanel = thePanelWithTitle(panel.get(), QStringLiteral("Recognized text (OCR)"));
    QVERIFY(textPanel);
    auto *textItem = qobject_cast<QQuickItem *>(textPanel);
    QVERIFY(textItem);
    // The notice and the text live in different parents, so their places are
    // compared in the scene.
    const double noticeY = noticeItem->mapToScene(QPointF(0, 0)).y();
    const double textBottom = textItem->mapToScene(QPointF(0, textItem->height())).y();
    QVERIFY2(noticeY > textBottom, qPrintable(QStringLiteral("notice y=%1, text bottom=%2, text height=%3").arg(noticeY).arg(textBottom).arg(textItem->height())));
}

// The header row that cannot hold the switch sends it to the line below: the
// name and the actions stay in the row, the switch gets its own line.
void TestBlockPanelQml::theSwitchGoesToTheNextLineWhenTheHeaderDoesNotFit()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    auto *item = qobject_cast<QQuickItem *>(panel.get());
    QVERIFY(item);

    QObject *close = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("✕"));
    QVERIFY(close);
    auto *closeItem = qobject_cast<QQuickItem *>(close);
    QVERIFY(closeItem);
    QQuickItem *headerRow = closeItem->parentItem();
    QVERIFY(headerRow);

    // A wide panel: the switch stands in the header row.
    QObject *wide = theSwitch(panel.get());
    QVERIFY(wide);
    QCOMPARE(qobject_cast<QQuickItem *>(wide)->parentItem(), headerRow);

    // A narrow panel: the header row cannot hold the switch.
    item->setWidth(420);
    QTest::qWait(20);

    QObject *narrow = theSwitch(panel.get());
    QVERIFY(narrow);
    auto *narrowItem = qobject_cast<QQuickItem *>(narrow);
    QVERIFY(narrowItem);
    QVERIFY2(narrowItem->parentItem() != headerRow, "the switch must leave the header row that cannot hold it");
    QVERIFY2(narrowItem->mapToScene(QPointF(0, 0)).y() > headerRow->mapToScene(QPointF(0, 0)).y(), "the switch must stand below the header row");

    // The switch that stands below still drives the panel.
    clickSegment(narrow, 1);
    QCOMPARE(m_controller->property("selectedBlockUsesCorrected").toBool(), true);
    m_controller->setProperty("selectedBlockUsesCorrected", false);

    // The width comes back: the switch returns to the header row.
    item->setWidth(700);
    QTest::qWait(20);

    QObject *back = theSwitch(panel.get());
    QVERIFY(back);
    QCOMPARE(qobject_cast<QQuickItem *>(back)->parentItem(), headerRow);
}

// The approve action says what it does — and it undoes what it did: while
// the approval stands, the button offers the revoke.
void TestBlockPanelQml::theApproveActionIsNamed()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    const bool approves = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("Approve")) != nullptr;

    // The layer in view does not change the name of the action: the panel
    // approves the block, whichever layer it shows.
    m_controller->setProperty("selectedBlockUsesCorrected", true);
    QTest::qWait(20);
    const bool approvesTheCorrection = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("Approve")) != nullptr;

    // An approved block offers the undo.
    m_controller->setProperty("selectedBlockCorrectedCheckStatus", 5);
    QTest::qWait(20);
    const bool revokes = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("Revoke")) != nullptr;

    m_controller->setProperty("selectedBlockCorrectedCheckStatus", 4);
    m_controller->setProperty("selectedBlockUsesCorrected", false);

    QVERIFY2(approves, "the panel must approve the block");
    QVERIFY2(approvesTheCorrection, "the action must keep its name while the correction is in view");
    QVERIFY2(revokes, "an approved block must offer to revoke the approval");
}

// The state of the block wears the color of its verdict: gray — nobody judged
// it, green — it is right, red — it is wrong (ADR 238).
void TestBlockPanelQml::theStateFollowsTheVerdict()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(theme);

    QCOMPARE(panel->property("stateColor").value<QColor>(), theme->property("success").value<QColor>());

    m_controller->setProperty("selectedBlockCheckStatus", 4);
    QTest::qWait(20);
    QCOMPARE(panel->property("stateColor").value<QColor>(), theme->property("error").value<QColor>());

    m_controller->setProperty("selectedBlockCheckStatus", 0);
    QTest::qWait(20);
    QCOMPARE(panel->property("stateColor").value<QColor>(), theme->property("nothing").value<QColor>());

    m_controller->setProperty("selectedBlockCheckStatus", 1);
    QTest::qWait(20);
}

QTEST_MAIN(TestBlockPanelQml)
#include "test_block_panel_qml.moc"
