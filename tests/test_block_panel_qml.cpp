// The block panel's header: the name of the block in the interface language,
// the state of the block, the switch of the layers and the actions. The action
// that takes the user's text back stands in the header next to the yellow
// status (ADR 339).
//
// The panel reads the block's state from the controller singleton; the test
// registers a double (tests/fake_block_controller.qml) with a recognized block
// and a correction.

#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QtTest>

namespace {

// The content of the panel is a tree: the buttons and the fields stand under
// the layouts, not under the panel itself.
QList<QQuickItem *> descendantsOf(QQuickItem *item)
{
    QList<QQuickItem *> all;
    for (QQuickItem *child : item->childItems()) {
        all.append(child);
        all.append(descendantsOf(child));
    }
    return all;
}

// The place of an element in the window: the elements of a header stand in
// different parents, and only the scene tells where each of them is.
double sceneX(QObject *item)
{
    auto *quickItem = qobject_cast<QQuickItem *>(item);
    return quickItem ? quickItem->mapToScene(QPointF(0, 0)).x() : 0.0;
}

}  // namespace

class TestBlockPanelQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void theNameOfTheBlockIsInTheInterfaceLanguage();
    void theSwitchHasThreeSegments();
    void theSwitchIsGoneWhenOnlyTheMarkupIsThere();
    void theSwitchIsGoneWithoutBothLayers();
    void theLayersAreNamed();
    void theNoteStandsWithItsLayer();
    void theSwitchMovesTheDocument();
    void thePanelSwitchesToACorrectionThatLandsOnTheBlock();
    void theComparisonShowsBothLayers();
    void theComparisonTintsTheDiff();
    void theResetActionIsGone();
    void theSwitchGoesToTheNextLineWhenTheHeaderDoesNotFit();
    void theStatusWordsTheBlock();
    void theBlockWithoutTextHasNoDot();
    void theStatusFollowsTheVerdict();
    void theStatusFollowsTheEffectiveLayer();
    void theReviewAnswerDamagesTheBlock();
    void theNameDoesNotMoveWhenTheDotIsGone();
    void theRestoreActionStandsInTheHeader();
    void theNoticeRowIsGone();
    void thePanelCoversTheSpaceUnderIt();
    void theScoreStandsWithItsLayer();
    void theApproveActionStandsWithItsLayer();
    void theChipStandsInTheComparison();
    void theVlmLayerStandsWithoutTheOcrText();
    void theApproveActionStandsOnTheVisibleCard();
    void theApproveTipFollowsTheAction();
    void theHeaderKnowsItsWidth();
    void theHeaderKeepsItsOrder();
    void theLayerHeaderKeepsItsOrder();
    void theChipSaysInTheDocument();
    void theApproveActionWaitsForTheIdleApp();

private:
    std::unique_ptr<QObject> loadPanel();
    std::unique_ptr<QObject> createUnderlay();
    bool backgroundSpot(QQuickItem *panel, QPointF *spot);
    QList<QObject *> findAllByClassName(QObject *root, const QLatin1String &className);
    QObject *childWithText(QObject *root, const QLatin1String &className, const QString &text);
    QObject *theSwitch(QObject *root);
    QObject *thePanelWithTitle(QObject *root, const QString &title);
    bool isVisible(QObject *item);
    void clickSegment(QObject *switchControl, int index);
    void restoreControllerDefaults();

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

// The item under the panel: the preview of the page stands there in the app,
// and it counts the presses that reach it.
std::unique_ptr<QObject> TestBlockPanelQml::createUnderlay()
{
    m_warnings.clear();

    QQmlComponent component(m_engine.get());
    component.setData(QByteArrayLiteral("import QtQuick\n"
                                        "MouseArea {\n"
                                        "    property int presses: 0\n"
                                        "    onClicked: presses++\n"
                                        "}"),
                      QUrl());
    if (!component.isReady()) {
        m_warnings.append(component.errorString());
        return nullptr;
    }

    std::unique_ptr<QObject> object(component.create());
    if (!object) {
        m_warnings.append(component.errorString());
        return nullptr;
    }

    auto *item = qobject_cast<QQuickItem *>(object.get());
    if (!item)
        return nullptr;
    item->setParentItem(m_parent);
    // The underlay stands below the panel: the panel is what the press must
    // stop at.
    item->setZ(-1);
    item->setWidth(m_parent->width());
    item->setHeight(m_parent->height());
    return object;
}

// A point over the panel's own background: no button, no field and no label
// stands there. The mouse area of the panel is not content — it is the
// background itself.
bool TestBlockPanelQml::backgroundSpot(QQuickItem *panel, QPointF *spot)
{
    const QRectF panelRect = panel->mapRectToScene(QRectF(0, 0, panel->width(), panel->height()));
    const QList<QObject *> mouseAreas = findAllByClassName(panel, QLatin1String("QQuickMouseArea"));

    QList<QQuickItem *> content;
    for (QQuickItem *item : descendantsOf(panel)) {
        if (!item->isVisible() || mouseAreas.contains(item))
            continue;
        content.append(item);
    }

    const int steps = 32;
    for (int row = 1; row < steps; ++row) {
        for (int column = 1; column < steps; ++column) {
            const QPointF candidate(panelRect.left() + panelRect.width() * column / steps, panelRect.top() + panelRect.height() * row / steps);
            bool overContent = false;
            for (QQuickItem *item : content) {
                if (item->contains(item->mapFromScene(candidate))) {
                    overContent = true;
                    break;
                }
            }
            if (!overContent) {
                *spot = candidate;
                return true;
            }
        }
    }
    return false;
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

// The state the double starts from: a test that changes it gives it back,
// so the next test reads the same block.
void TestBlockPanelQml::restoreControllerDefaults()
{
    m_controller->setProperty("selectedBlockText", QStringLiteral("E = mc^2"));
    m_controller->setProperty("selectedBlockCorrected", QStringLiteral("E = mc^2 + 1"));
    m_controller->setProperty("selectedBlockUsesCorrected", false);
    m_controller->setProperty("selectedBlockCheckStatus", 1);
    m_controller->setProperty("selectedBlockCorrectedCheckStatus", 4);
    m_controller->setProperty("selectedBlockDocumentDiverged", false);
    m_controller->setProperty("selectedBlockFixStatus", 0);
    m_controller->setProperty("selectedBlockConfidence", 0.9);
    m_controller->setProperty("selectedBlockCorrectedConfidence", 0.38);
    m_controller->setProperty("selectedBlockSyntaxProblem", 0);
    m_controller->setProperty("busy", false);
    m_controller->setProperty("checkBusy", false);
    QTest::qWait(20);
}

// The double keeps its own state between the tests, whatever a test did
// before it failed.
void TestBlockPanelQml::cleanup()
{
    restoreControllerDefaults();
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

// The switch names the layers of the block: the recognized text, the VLM's
// word and the difference between them (ADR 339).
void TestBlockPanelQml::theSwitchHasThreeSegments()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *switchControl = theSwitch(panel.get());
    QVERIFY2(switchControl, "the panel must carry the switch of the layers");

    const QVariantList options = switchControl->property("options").toList();
    QCOMPARE(options.size(), 3);
    QCOMPARE(options.at(0).toMap().value("label").toString(), QStringLiteral("OCR"));
    QCOMPARE(options.at(1).toMap().value("label").toString(), QStringLiteral("VLM"));
    QCOMPARE(options.at(2).toMap().value("label").toString(), QStringLiteral("Difference"));
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

    restoreControllerDefaults();

    QVERIFY2(!switchVisible, "the switch must be gone where there is nothing to switch between");
}

// The switch stands only where there is something to switch between: a block
// with an OCR text alone, or a correction alone, has one layer.
void TestBlockPanelQml::theSwitchIsGoneWithoutBothLayers()
{
    m_controller->setProperty("selectedBlockCorrected", "");

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *switchControl = theSwitch(panel.get());
    QVERIFY(switchControl);
    QVERIFY2(!isVisible(switchControl), "the switch must be gone where there is nothing to switch between");

    restoreControllerDefaults();
    QVERIFY2(isVisible(theSwitch(panel.get())), "the switch must stand where both layers are there");
}

// The layers name themselves: the recognized text and the VLM's word.
void TestBlockPanelQml::theLayersAreNamed()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY2(thePanelWithTitle(panel.get(), QStringLiteral("OCR")), "the panel must show the recognized text as OCR");
    QVERIFY2(thePanelWithTitle(panel.get(), QStringLiteral("VLM")), "the panel must show the correction as VLM");
}

// The fixer's answer speaks about the recognized text, so it stands with it:
// in the header of the OCR panel, not in the header of the module.
void TestBlockPanelQml::theNoteStandsWithItsLayer()
{
    m_controller->setProperty("selectedBlockFixStatus", 1);
    m_controller->setProperty("selectedBlockCorrected", "");

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY2(original, "the panel must show the recognized text");
    QCOMPARE(original->property("noteText").toString(), QStringLiteral("No correction required"));

    m_controller->setProperty("selectedBlockFixStatus", 0);
    restoreControllerDefaults();
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
    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY2(original, "the panel must carry the panel of the recognized text");
    QObject *correction = thePanelWithTitle(panel.get(), QStringLiteral("VLM"));
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

    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY2(original, "the comparison must show the recognized text");
    QVERIFY(isVisible(original));

    QObject *correction = thePanelWithTitle(panel.get(), QStringLiteral("VLM"));
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

    QObject *correction = thePanelWithTitle(panel.get(), QStringLiteral("VLM"));
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
    item->setWidth(300);
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

// The block speaks one vocabulary of states (ADR 338): the effective layer
// gives the verdict, the user's own text gives the yellow, and a block
// without a text has no status at all.
void TestBlockPanelQml::theStatusWordsTheBlock()
{
    m_controller->setProperty("selectedBlockText", "");
    m_controller->setProperty("selectedBlockCorrected", "");
    m_controller->setProperty("selectedBlockCheckStatus", 0);

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));
    QCOMPARE(panel->property("statusWord").toString(), QString());

    m_controller->setProperty("selectedBlockText", "E = mc^2");
    QTest::qWait(20);
    QCOMPARE(panel->property("statusWord").toString(), QStringLiteral("Recognized"));

    m_controller->setProperty("selectedBlockCheckStatus", 1);
    QTest::qWait(20);
    QCOMPARE(panel->property("statusWord").toString(), QStringLiteral("No errors"));

    m_controller->setProperty("selectedBlockCheckStatus", 5);
    QTest::qWait(20);
    QCOMPARE(panel->property("statusWord").toString(), QStringLiteral("No errors"));

    m_controller->setProperty("selectedBlockCheckStatus", 4);
    QTest::qWait(20);
    QCOMPARE(panel->property("statusWord").toString(), QStringLiteral("Errors"));

    m_controller->setProperty("selectedBlockCheckStatus", 3);
    QTest::qWait(20);
    QCOMPARE(panel->property("statusWord").toString(), QStringLiteral("Damaged"));

    m_controller->setProperty("selectedBlockCheckStatus", 7);
    QTest::qWait(20);
    QCOMPARE(panel->property("statusWord").toString(), QStringLiteral("Invalid syntax"));

    // The user wrote the text: the block is the user's, whatever the model
    // said about the previous text.
    m_controller->setProperty("selectedBlockCheckStatus", 1);
    m_controller->setProperty("selectedBlockDocumentDiverged", true);
    QTest::qWait(20);
    QCOMPARE(panel->property("statusWord").toString(), QStringLiteral("Edited"));
    QCOMPARE(panel->property("userEdited").toBool(), true);

    restoreControllerDefaults();
}

// A block the user drew and nothing has been recognized in yet carries no
// status: neither the dot nor the word (ADR 338).
void TestBlockPanelQml::theBlockWithoutTextHasNoDot()
{
    m_controller->setProperty("selectedBlockText", "");
    m_controller->setProperty("selectedBlockCorrected", "");
    m_controller->setProperty("selectedBlockCheckStatus", 0);

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QQuickItem *dot = panel->findChild<QQuickItem *>(QStringLiteral("statusDot"));
    // The block carries no text: the dot of the state goes away.
    const bool hiddenWithoutText = !dot || !dot->isVisible();

    // The text arrives: the dot of the state stands again.
    m_controller->setProperty("selectedBlockText", "E = mc^2");
    QTest::qWait(20);
    const bool shownWithText = dot && dot->isVisible();

    // The state of the double is restored before the assertions: a failed
    // assertion must not leak into the next test.
    restoreControllerDefaults();

    QVERIFY2(dot, "the panel must carry the dot of the state");
    QVERIFY2(hiddenWithoutText, "a block without a text must not show the dot of the state");
    QVERIFY2(shownWithText, "a block with a text must show the dot of the state");
}

// The state of the block wears the color of its verdict: gray — nobody
// judged it, green — it is right, red — it is wrong, yellow — the user
// wrote it (ADR 338).
void TestBlockPanelQml::theStatusFollowsTheVerdict()
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

    m_controller->setProperty("selectedBlockDocumentDiverged", true);
    QTest::qWait(20);
    QCOMPARE(panel->property("stateColor").value<QColor>(), theme->property("warning").value<QColor>());

    restoreControllerDefaults();
}

// The header speaks about the layer the block carries, as the engine counts
// it (BoxTransitions::effectiveCheckStatus): the correction's verdict where
// the block points at a correction that exists, else the recognized text's.
void TestBlockPanelQml::theStatusFollowsTheEffectiveLayer()
{
    m_controller->setProperty("selectedBlockText", "E = mc^2");
    m_controller->setProperty("selectedBlockCorrected", "E = mc^2");
    m_controller->setProperty("selectedBlockUsesCorrected", true);
    m_controller->setProperty("selectedBlockCheckStatus", 1);
    m_controller->setProperty("selectedBlockCorrectedCheckStatus", 4);

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(theme);

    // The block points at the correction, and the correction exists: the
    // block's verdict is the correction's — the block is wrong.
    QCOMPARE(panel->property("statusWord").toString(), QStringLiteral("Errors"));
    QCOMPARE(panel->property("stateColor").value<QColor>(), theme->property("error").value<QColor>());

    // The block points at the recognized text: its verdict speaks.
    m_controller->setProperty("selectedBlockUsesCorrected", false);
    QTest::qWait(20);
    QCOMPARE(panel->property("statusWord").toString(), QStringLiteral("No errors"));
    QCOMPARE(panel->property("stateColor").value<QColor>(), theme->property("success").value<QColor>());

    restoreControllerDefaults();
}

// The fixer's REVIEW: the correction text is gone, but the verdict stays —
// the block is damaged, as the preview and the page say
// (BoxTransitions::fixMarkedUnreadable).
void TestBlockPanelQml::theReviewAnswerDamagesTheBlock()
{
    m_controller->setProperty("selectedBlockText", "E = mc^2");
    m_controller->setProperty("selectedBlockCorrected", "");
    m_controller->setProperty("selectedBlockUsesCorrected", true);
    m_controller->setProperty("selectedBlockCheckStatus", 1);
    m_controller->setProperty("selectedBlockCorrectedCheckStatus", 3);
    m_controller->setProperty("selectedBlockFixStatus", 3);

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(theme);

    QCOMPARE(panel->property("statusWord").toString(), QStringLiteral("Damaged"));
    QCOMPARE(panel->property("stateColor").value<QColor>(), theme->property("error").value<QColor>());

    restoreControllerDefaults();
}

// The name of the block keeps its place when the dot of the state goes away.
void TestBlockPanelQml::theNameDoesNotMoveWhenTheDotIsGone()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *name = childWithText(panel.get(), QLatin1String("LLOLabel"), QStringLiteral("Equation"));
    QVERIFY2(name, "the panel must name the block");
    auto *nameItem = qobject_cast<QQuickItem *>(name);
    QVERIFY(nameItem);
    const qreal xWithDot = nameItem->x();

    // A block the user drew and nothing has been recognized in yet carries no
    // dot of the state.
    m_controller->setProperty("selectedBlockText", "");
    m_controller->setProperty("selectedBlockCorrected", "");
    m_controller->setProperty("selectedBlockCheckStatus", 0);
    m_controller->setProperty("selectedBlockUsesCorrected", false);
    QTest::qWait(20);
    const qreal xWithoutDot = nameItem->x();

    restoreControllerDefaults();

    QCOMPARE(xWithoutDot, xWithDot);
}

// The action that takes the user's text back stands in the header of the
// module: the yellow status and the action stand together (ADR 339).
void TestBlockPanelQml::theRestoreActionStandsInTheHeader()
{
    m_controller->setProperty("selectedBlockDocumentDiverged", true);

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *restore = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("Restore the block text"));
    QVERIFY2(restore, "the panel must offer to restore the block's text");

    QObject *close = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("✕"));
    QVERIFY(close);

    auto *restoreItem = qobject_cast<QQuickItem *>(restore);
    auto *closeItem = qobject_cast<QQuickItem *>(close);
    QVERIFY(restoreItem);
    QVERIFY(closeItem);

    // Both stand in the header row of the module.
    QCOMPARE(restoreItem->parentItem(), closeItem->parentItem());

    restoreControllerDefaults();
}

// The row under the text is gone: the status and the action of the header
// say the same thing (ADR 339).
void TestBlockPanelQml::theNoticeRowIsGone()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY2(!childWithText(panel.get(), QLatin1String("LLOLabel"), QStringLiteral("The document text differs from the block")), "the panel must not keep the notice under the text");
}

// The panel takes the clicks over its own background: a press that missed a
// button must not reach the preview under it, and the buttons of the panel
// must keep their own clicks (ADR 340).
void TestBlockPanelQml::thePanelCoversTheSpaceUnderIt()
{
    const std::unique_ptr<QObject> underlay = createUnderlay();
    QVERIFY2(underlay, qPrintable(m_warnings.join(QStringLiteral("; "))));

    m_controller->setProperty("restoreCalls", 0);
    // The action that restores the block's text stands in the header only
    // where the document diverges from the block.
    m_controller->setProperty("selectedBlockDocumentDiverged", true);

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));
    auto *panelItem = qobject_cast<QQuickItem *>(panel.get());
    QVERIFY(panelItem);

    // The press over the panel's own background stops at the panel: the item
    // under it must not see it.
    QPointF background;
    QVERIFY2(backgroundSpot(panelItem, &background), "the panel must have a background of its own to press");
    QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, background.toPoint());
    QTest::qWait(20);
    QCOMPARE(underlay->property("presses").toInt(), 0);

    // The button of the panel keeps its click: the area of the panel stands
    // under the content, so it does not swallow the press.
    QObject *restore = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("Restore the block text"));
    QVERIFY2(restore, "the panel must offer to restore the block's text");
    auto *restoreItem = qobject_cast<QQuickItem *>(restore);
    QVERIFY(restoreItem);
    QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, restoreItem->mapToScene(QPointF(restoreItem->width() / 2.0, restoreItem->height() / 2.0)).toPoint());
    QTest::qWait(20);
    QCOMPARE(m_controller->property("restoreCalls").toInt(), 1);

    restoreControllerDefaults();
}

// The score of the layer stands with the layer: the prefix is ordinary text,
// the number wears the color of the verdict.
void TestBlockPanelQml::theScoreStandsWithItsLayer()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY(original);
    QCOMPARE(original->property("scoreText").toString(), QStringLiteral("90%"));

    QObject *theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(theme);
    QCOMPARE(original->property("scoreColor").value<QColor>(), theme->property("success").value<QColor>());

    // A rejected text: the number turns red.
    m_controller->setProperty("selectedBlockCheckStatus", 4);
    QTest::qWait(20);
    QCOMPARE(original->property("scoreColor").value<QColor>(), theme->property("error").value<QColor>());

    restoreControllerDefaults();
}

// The approve action acts on the layer it stands in: it is in the header of
// the layer, not in the header of the module.
void TestBlockPanelQml::theApproveActionStandsWithItsLayer()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY(original);
    QVERIFY2(original->property("approveVisible").toBool(), "the layer in view must offer to approve the block");

    // The action is gone where the block is the user's.
    m_controller->setProperty("selectedBlockDocumentDiverged", true);
    QTest::qWait(20);
    QVERIFY2(!original->property("approveVisible").toBool(), "the approve action must be gone while the block is yellow");

    restoreControllerDefaults();
}

// The chip marks the layer the document carries — in the comparison alone.
void TestBlockPanelQml::theChipStandsInTheComparison()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QObject *vlm = thePanelWithTitle(panel.get(), QStringLiteral("VLM"));
    QVERIFY(original);
    QVERIFY(vlm);

    // The OCR layer is the one the document carries (the double says so).
    QVERIFY2(!original->property("inDocument").toBool(), "the chip belongs to the comparison alone");
    QVERIFY2(!vlm->property("inDocument").toBool(), "the chip belongs to the comparison alone");

    clickSegment(theSwitch(panel.get()), 2);
    QVERIFY2(original->property("inDocument").toBool(), "the comparison must mark the layer the document carries");
    QVERIFY2(!vlm->property("inDocument").toBool(), "the comparison must mark one layer only");
}

// A block with a correction but without the recognized text carries one layer:
// the card of the VLM's word, and no switch.
void TestBlockPanelQml::theVlmLayerStandsWithoutTheOcrText()
{
    m_controller->setProperty("selectedBlockText", "");
    m_controller->setProperty("selectedBlockCorrected", "E = mc^2 + 1");

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *vlm = thePanelWithTitle(panel.get(), QStringLiteral("VLM"));
    QVERIFY(vlm);
    QVERIFY2(isVisible(vlm), "the card of the VLM's word must stand");

    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY(original);
    QVERIFY2(!isVisible(original), "the card of the recognized text must be gone");
    QVERIFY2(!isVisible(theSwitch(panel.get())), "the switch must be gone where there is nothing to switch between");

    restoreControllerDefaults();
}

// The approve action stands on the card that is in view — also when the block
// carries one layer only (ADR 339).
void TestBlockPanelQml::theApproveActionStandsOnTheVisibleCard()
{
    // A block with a correction but without the recognized text: the card of
    // the VLM's word stands alone, and it offers the action.
    m_controller->setProperty("selectedBlockText", "");
    m_controller->setProperty("selectedBlockCorrected", "E = mc^2 + 1");

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *vlm = thePanelWithTitle(panel.get(), QStringLiteral("VLM"));
    QVERIFY(vlm);
    QVERIFY(isVisible(vlm));
    QVERIFY2(vlm->property("approveVisible").toBool(), "the layer in view must offer to approve the block");

    // The switch stands on the VLM, but the card of the VLM is not there:
    // the card in view offers the action.
    m_controller->setProperty("selectedBlockText", "E = mc^2");
    m_controller->setProperty("selectedBlockCorrected", "E = mc^2");
    m_controller->setProperty("selectedBlockUsesCorrected", true);
    QTest::qWait(20);

    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY(original);
    QVERIFY(isVisible(original));
    QVERIFY2(original->property("approveVisible").toBool(), "the layer in view must offer to approve the block");

    restoreControllerDefaults();
}

// The tooltip of the approve action follows its label: the button says what
// it will do, and the tooltip says the same.
void TestBlockPanelQml::theApproveTipFollowsTheAction()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY(original);
    QCOMPARE(original->property("approveText").toString(), QStringLiteral("Approve"));
    QCOMPARE(original->property("approveTip").toString(), QStringLiteral("Ignore the check result"));

    // The block is approved: the action takes the approval back.
    m_controller->setProperty("selectedBlockCheckStatus", 5);
    QTest::qWait(20);
    QCOMPARE(original->property("approveText").toString(), QStringLiteral("Revoke"));
    QCOMPARE(original->property("approveTip").toString(), QStringLiteral("Revoke the approval"));

    restoreControllerDefaults();
}

// The header knows its own width: the estimate may not be short of the row,
// or the switch would stay in a row that cannot hold it (ADR 323).
void TestBlockPanelQml::theHeaderKnowsItsWidth()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(theme);
    const qreal slack = theme->property("spacingSmall").toInt();

    QObject *close = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("✕"));
    QVERIFY(close);
    auto *headerRow = qobject_cast<QQuickItem *>(close)->parentItem();
    QVERIFY(headerRow);

    // The row's own width: the estimate of the header must not be short of
    // it, and must not waste the room of the row either.
    const qreal natural = headerRow->implicitWidth();
    const qreal estimate = panel->property("headerNeed").toReal();
    QVERIFY2(estimate >= natural, "the estimate of the header must not be short of the row");
    QVERIFY2(estimate <= natural + slack * 2, "the estimate of the header must not waste the room of the row");
}

// The header of the module reads in one order: the dot of the state, the
// name of the block, the state itself, the action that takes the text back,
// the switch and the close (ADR 339).
void TestBlockPanelQml::theHeaderKeepsItsOrder()
{
    // The document diverges from the block: the action that takes the text
    // back stands in the header.
    m_controller->setProperty("selectedBlockDocumentDiverged", true);

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QQuickItem *dot = panel->findChild<QQuickItem *>(QStringLiteral("statusDot"));
    QVERIFY2(dot, "the panel must carry the dot of the state");

    QObject *name = childWithText(panel.get(), QLatin1String("LLOLabel"), QStringLiteral("Equation"));
    QVERIFY2(name, "the panel must name the block");
    QObject *state = childWithText(panel.get(), QLatin1String("LLOLabel"), QStringLiteral("Edited"));
    QVERIFY2(state, "the header must carry the word of the state");
    QObject *restore = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("Restore the block text"));
    QVERIFY2(restore, "the panel must offer to restore the block's text");
    QObject *close = childWithText(panel.get(), QLatin1String("LLOButton"), QStringLiteral("✕"));
    QVERIFY(close);

    QVERIFY2(sceneX(dot) < sceneX(name), "the dot of the state comes first");
    QVERIFY2(sceneX(name) < sceneX(state), "the name of the block comes before the state");
    QVERIFY2(sceneX(state) < sceneX(restore), "the state comes before the action that takes the text back");
    QVERIFY2(sceneX(restore) < sceneX(close), "the action comes before the close");

    // The switch stands in the header while the row holds it; when the row
    // cannot hold it, it goes to the line below (ADR 323).
    if (panel->property("switchInHeader").toBool()) {
        QObject *switchControl = theSwitch(panel.get());
        QVERIFY(switchControl);
        QVERIFY2(sceneX(restore) < sceneX(switchControl), "the switch comes after the action");
        QVERIFY2(sceneX(switchControl) < sceneX(close), "the switch comes before the close");
    }

    restoreControllerDefaults();
}

// The header of a layer reads: the name of the layer, the note of what the
// pipeline did with it, the score and the approve action (ADR 339).
void TestBlockPanelQml::theLayerHeaderKeepsItsOrder()
{
    // The pipeline left the recognized text alone: the note says so, and
    // the correction is gone — the card of the OCR's word stands alone.
    m_controller->setProperty("selectedBlockCorrected", "");
    m_controller->setProperty("selectedBlockCorrectedCheckStatus", 0);
    m_controller->setProperty("selectedBlockFixStatus", 1);

    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *ocr = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY(ocr);

    QObject *title = childWithText(ocr, QLatin1String("LLOLabel"), QStringLiteral("OCR"));
    QVERIFY2(title, "the card must name its layer");
    QObject *note = childWithText(ocr, QLatin1String("LLOLabel"), QStringLiteral("· No correction required"));
    QVERIFY2(note, "the card must say what the pipeline did with the layer");
    QObject *prefix = childWithText(ocr, QLatin1String("LLOLabel"), QStringLiteral("· Score:"));
    QVERIFY2(prefix, "the card must carry the prefix of the score");
    QObject *score = childWithText(ocr, QLatin1String("LLOLabel"), QStringLiteral("90%"));
    QVERIFY2(score, "the card must carry the score");
    QObject *approve = childWithText(ocr, QLatin1String("LLOButton"), QStringLiteral("Approve"));
    QVERIFY2(approve, "the card must offer to approve the layer");

    QVERIFY2(sceneX(title) < sceneX(note), "the name of the layer comes first");
    QVERIFY2(sceneX(note) < sceneX(prefix), "the note comes before the score");
    QVERIFY2(sceneX(prefix) < sceneX(score), "the prefix comes before the number");
    QVERIFY2(sceneX(score) < sceneX(approve), "the score comes before the approve action");

    restoreControllerDefaults();
}

// The chip of the document stands in the comparison alone, on the layer the
// document carries, and it says where it stands (ADR 339).
void TestBlockPanelQml::theChipSaysInTheDocument()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *ocr = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY(ocr);

    // The chip belongs to the comparison: it is not there while the layer
    // stands alone.
    QObject *chip = childWithText(ocr, QLatin1String("LLOLabel"), QStringLiteral("In the document"));
    QVERIFY2(chip, "the card must carry the chip of the document");
    auto *chipItem = qobject_cast<QQuickItem *>(chip);
    QVERIFY(chipItem);
    QVERIFY2(!chipItem->isVisible(), "the chip belongs to the comparison alone");

    clickSegment(theSwitch(panel.get()), 2);
    QVERIFY2(chipItem->isVisible(), "the comparison must mark the layer the document carries");

    // The chip stands at the right of the header, after the score.
    QObject *score = childWithText(ocr, QLatin1String("LLOLabel"), QStringLiteral("90%"));
    QVERIFY(score);
    QVERIFY2(sceneX(score) < sceneX(chip), "the chip stands after the score");

    // The block the user wrote: the chip goes away.
    m_controller->setProperty("selectedBlockDocumentDiverged", true);
    QTest::qWait(20);
    QVERIFY2(!chipItem->isVisible(), "a yellow block carries no chip");

    restoreControllerDefaults();
}

// The approve action waits for the idle app: while the check runs or the app
// is busy, the action cannot write an approval over the model's answer.
void TestBlockPanelQml::theApproveActionWaitsForTheIdleApp()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *ocr = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY(ocr);
    QObject *approve = childWithText(ocr, QLatin1String("LLOButton"), QStringLiteral("Approve"));
    QVERIFY2(approve, "the layer in view must offer to approve the block");
    QVERIFY2(approve->property("enabled").toBool(), "the idle app lets the action stand");

    // The check runs: the action waits.
    m_controller->setProperty("checkBusy", true);
    QTest::qWait(20);
    const bool waitsForTheCheck = !approve->property("enabled").toBool();

    // The app is busy with something else: the action waits as well.
    m_controller->setProperty("checkBusy", false);
    m_controller->setProperty("busy", true);
    QTest::qWait(20);
    const bool waitsForTheApp = !approve->property("enabled").toBool();

    restoreControllerDefaults();

    QVERIFY2(waitsForTheCheck, "the action must wait while the check runs");
    QVERIFY2(waitsForTheApp, "the action must wait while the app is busy");
}

QTEST_MAIN(TestBlockPanelQml)
#include "test_block_panel_qml.moc"
