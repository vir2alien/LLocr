# The block panel: one status vocabulary, two headers — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The block panel gets one closed vocabulary of states (block and page speak the same words), the two headers read as the design says, and the panel no longer lets a click through to the preview under it.

**Architecture:** Everything happens in QML — the status of a block is derived from the effective layer plus the user layer, and the status of a page is derived from the recognition mode, the verification aggregate and the edited flag. The C++ side keeps its aggregation (`PageStatus::verificationAggregate`, `PageStatus::recognitionModeAfterBlockText`) and does not change. `PageStatus.qml` grows a `statusColor()` beside `statusText()`; `BlockEditPanel.qml` owns the status of the block and passes it down to the layer cards.

**Tech Stack:** Qt 6.10.3 (QtQuick, QtQuick.Controls, QtQuick.Layouts), QML singletons, C++/QtTest for the unit and the QML tests, CMake (`build/` tree, Unix Makefiles).

**Spec:** `docs/superpowers/specs/2026-11-27-block-panel-statuses-design.md` (Russian mirror: `.ru.md`). The decisions live in the ADR log — ADR 338 (the vocabulary), ADR 339 (the headers), ADR 340 (the panel covers the space under it), `docs/07-glossary.md`.

## Global Constraints

- The stack does not change: Qt 6 + C++ + QML, CMake (ADR 100). No new dependencies.
- Reuse the configured tree `build/` — do not re-configure from scratch. Build with `cmake --build build -j 8`, test with `ctest --test-dir build`.
- Sizes, fonts and colors come from `Theme.*` (`src/qml/Theme.qml`) — never a literal color or font size in QML. The new yellow is `Theme.warning` (already in the theme).
- Comments only where the code cannot be read from its names; no narration, no section banners, no TODO notes.
- The layer targets stay as they are (`llocr_core` → `llocr_config` → … → `llocr_app` → `llocr`); the tests link the same targets, never a hand-listed copy of the sources.
- Every document of the project is bilingual: English primary, `*.ru.md` mirror. The ADR log is updated in `docs/07-glossary.md` **and** `docs/07-glossary.ru.md` (ADR 338–340 are already written and committed).
- The Russian catalog `resources/i18n/llocr_ru.ts` is refreshed with lupdate and the new words are translated by hand — the shipped app must not fall back to English.
- The whole verification is `scripts/check.sh` (build → ctest → clang-format → qmllint). It is what CI runs.

---

## File map

| File | What it owns after this change |
| --- | --- |
| `src/qml/MainWindow/BlockEditPanel.qml` | The module: the status of the block, the header of the module, the switch of the layers, the approve action, the layer cards. |
| `src/qml/MainWindow/BlockTextPanel.qml` | The card of one layer: its name, the note of the pipeline, the score, the approve action and the chip «В документе». |
| `src/qml/PageStatus.qml` | The word and the color of the page — one mapping for the whole app. |
| `src/qml/MainWindow/WorkPanel.qml` | The label of the page in the work panel — it takes the word and the color from `PageStatus`. |
| `src/qml/MainWindow/ThumbDelegate.qml` | The dot and the label of the page in the thumbnail list. |
| `tests/fake_block_controller.qml` | The controller double of the panel test. |
| `tests/test_block_panel_qml.cpp` | The tests of the panel: the headers, the switch, the statuses, the approve action. |
| `tests/test_page_status_qml.cpp` | **New** — the tests of the page status (the word and the color for every state). |
| `tests/CMakeLists.txt` | The registration of the new test. |
| `resources/i18n/llocr_ru.ts` | The Russian words of the changed strings. |

---

### Task 1: The panel covers the space under it

The panel is a plain `Rectangle` and takes no mouse: a click that missed a button inside the panel reaches the preview underneath and selects another block or closes the panel. A `MouseArea` that fills the panel and stands under its content swallows every press over the panel's background.

**Files:**
- Modify: `src/qml/MainWindow/BlockEditPanel.qml` (the root `Rectangle`, before the `ColumnLayout`)
- Test: `tests/test_block_panel_qml.cpp`

**Interfaces:**
- Consumes: nothing (the first task of the change).
- Produces: the panel root carries a `QQuickMouseArea` child that fills it — the later tasks must declare their content **after** it, so the area stays under the content and the buttons keep their clicks.

- [ ] **Step 1: Write the failing test**

Add the slot declaration to the class in `tests/test_block_panel_qml.cpp` (next to `theStateFollowsTheVerdict`):

```cpp
    void thePanelCoversTheSpaceUnderIt();
```

and the test body:

```cpp
// The panel takes the clicks over its own background: a click that missed a
// button must not reach the preview under it (ADR 340).
void TestBlockPanelQml::thePanelCoversTheSpaceUnderIt()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    auto *panelItem = qobject_cast<QQuickItem *>(panel.get());
    QVERIFY(panelItem);

    QQuickItem *shield = nullptr;
    for (QObject *child : panel->children()) {
        if (QString::fromLatin1(child->metaObject()->className()) != QStringLiteral("QQuickMouseArea"))
            continue;
        shield = qobject_cast<QQuickItem *>(child);
    }
    QVERIFY2(shield, "the panel must take the clicks over its own background");
    QCOMPARE(shield->parentItem(), panelItem);
    QCOMPARE(shield->width(), panelItem->width());
    QCOMPARE(shield->height(), panelItem->height());
}
```

- [ ] **Step 2: Run the test to verify it fails**

```sh
cmake --build build -j 8 --target test_block_panel_qml
ctest --test-dir build -R test_block_panel_qml --output-on-failure
```

Expected: FAIL — «the panel must take the clicks over its own background» (no `QQuickMouseArea` child).

- [ ] **Step 3: Add the mouse area**

In `src/qml/MainWindow/BlockEditPanel.qml`, right after the root's properties (before the `Shortcut` and the `ColumnLayout`):

```qml
    // The panel takes the clicks over its own background: a click that missed
    // a button stays inside the panel and does not reach the preview under it
    // (ADR 340). The area stands under the content, so the buttons and the
    // text fields keep their clicks.
    MouseArea {
        anchors.fill: parent
    }
```

- [ ] **Step 4: Run the test to verify it passes**

```sh
cmake --build build -j 8 --target test_block_panel_qml
ctest --test-dir build -R test_block_panel_qml --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```sh
git add src/qml/MainWindow/BlockEditPanel.qml tests/test_block_panel_qml.cpp
git commit -m "fix: the block panel takes the clicks over its own background"
```

---

### Task 2: The status of the block and the header of the module

The block's status becomes a closed vocabulary of six words, the color is carried by the dot, and the header of the module reads: the dot, the name of the block type, the status, «Вернуть текст блока» (only where the user wrote the text), the switch, the close. The approve action leaves the module — it acts on a layer (ADR 339), and the row under the text is gone: the yellow status and the button in the header say the same thing.

**Files:**
- Modify: `src/qml/MainWindow/BlockEditPanel.qml`
- Test: `tests/test_block_panel_qml.cpp`, `tests/fake_block_controller.qml`

**Interfaces:**
- Consumes: the `QQuickMouseArea` of Task 1 (the header content stays above it).
- Produces: the panel root exposes `statusWord` (string, empty when the block has no status) and `stateColor` (the color of the dot); the panel exposes `userEdited` (bool) — Task 4 reads it for the approve action and the chip.

- [ ] **Step 1: Write the failing test**

In `tests/fake_block_controller.qml`, make the double start from a block nobody wrote into, so the tests that need the divergence turn it on themselves:

```qml
    property bool currentPageEdited: false
    property bool selectedBlockDocumentDiverged: false
```

In `tests/test_block_panel_qml.cpp`, delete the slot `theApproveActionIsNamed` and its body — the approve action leaves the module in this task and comes back in the header of the layer in Task 4, where `theApproveActionStandsWithItsLayer` covers it. Replace the slot `theStateFollowsTheVerdict` with these four slots:

```cpp
    void theStatusWordsTheBlock();
    void theStatusFollowsTheVerdict();
    void theRestoreActionStandsInTheHeader();
    void theNoticeRowIsGone();
```

and the bodies:

```cpp
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

    m_controller->setProperty("selectedBlockDocumentDiverged", false);
    m_controller->setProperty("selectedBlockCheckStatus", 1);
    QTest::qWait(20);
}

// The state of the block wears the color of its verdict: gray — nobody
// judged it, green — it is right, red — it is wrong, yellow — the user
// wrote it (ADR 338).
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

    m_controller->setProperty("selectedBlockDocumentDiverged", true);
    QTest::qWait(20);
    QCOMPARE(panel->property("stateColor").value<QColor>(), theme->property("warning").value<QColor>());

    m_controller->setProperty("selectedBlockDocumentDiverged", false);
    m_controller->setProperty("selectedBlockCheckStatus", 1);
    QTest::qWait(20);
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

    m_controller->setProperty("selectedBlockDocumentDiverged", false);
}

// The row under the text is gone: the status and the action of the header
// say the same thing (ADR 339).
void TestBlockPanelQml::theNoticeRowIsGone()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY2(!childWithText(panel.get(), QLatin1String("LLOLabel"),
                            QStringLiteral("The document text differs from the block")),
             "the panel must not keep the notice under the text");
}
```

- [ ] **Step 2: Run the test to verify it fails**

```sh
cmake --build build -j 8 --target test_block_panel_qml
ctest --test-dir build -R test_block_panel_qml --output-on-failure
```

Expected: FAIL — `statusWord` is not a property of the panel, the notice row is still there.

- [ ] **Step 3: The status of the block**

In `src/qml/MainWindow/BlockEditPanel.qml`, add the new state next to the property `markupOnly` and remove the property `overallStatus` (the header row of Task 2, Step 4 does not read it):

```qml
    // The user wrote the block's text: in the WYSIWYG editor (the document
    // layer, ADR 296) or into the layer of the block itself. The block is the
    // user's now, and the model's verdict about the previous text does not
    // speak for it (ADR 338).
    readonly property bool userEdited: Controller.selectedBlockDocumentDiverged
                                      || effectiveStatus === 6
    // A block the user drew and nothing has been recognized in yet: it is
    // markup, not a recognized text. Such a block has no status — the status
    // comes with the recognition.
    readonly property bool markupOnly: effectiveStatus === 0
                                       && Controller.selectedBlockText.length === 0
                                       && Controller.selectedBlockCorrected.length === 0
    readonly property bool showsStatus: !root.markupOnly || root.userEdited
    // The state of the block in the header: the word and the color of the
    // dot (ADR 338).
    readonly property string statusWord: root.showsStatus
                                        ? root.blockStatusWord(root.effectiveStatus) : ""
    readonly property color stateColor: root.blockStatusColor(root.effectiveStatus)

    function blockStatusWord(status) {
        if (root.userEdited)
            return qsTr("Edited")
        if (root.readDamaged)
            return qsTr("Damaged")
        if (root.readConfirmed)
            return qsTr("No errors")
        if (status === 7)
            return qsTr("Invalid syntax")
        if (status === 3)
            return qsTr("Damaged")
        if (status === 4)
            return qsTr("Errors")
        if (status === 1 || status === 5)
            return qsTr("No errors")
        return qsTr("Recognized")
    }
    function blockStatusColor(status) {
        if (root.userEdited)
            return Theme.warning
        if (root.readDamaged || status === 3 || status === 4 || status === 7)
            return Theme.error
        if (root.readConfirmed || status === 1 || status === 5)
            return Theme.success
        return Theme.nothing
    }
```

`readDamaged`, `readConfirmed` and `readApplied` already exist further down the file — move them above this block (the functions above read them).

The old function `statusWord(status)` was renamed to `layerStatusWord(status)` — a QML property and a function cannot share one name, and the property `statusWord` is the one the brief asks for. The two layer cards read it (`verdictText:`). `dotColor(status)` keeps its name — the card call sites are replaced in Task 4.

- [ ] **Step 4: The header of the module**

In the same file, replace the header row with:

```qml
        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(closeButton.implicitHeight, switchControl.implicitHeight)
            spacing: Theme.spacingSmall

            Rectangle {
                id: statusDot
                visible: root.showsStatus
                Layout.preferredWidth: 10
                Layout.preferredHeight: 10
                // The dot stands as far from the left edge as it stands from
                // the top one — the header's insents are equal.
                Layout.leftMargin: Theme.spacing
                radius: 5
                color: root.stateColor
                border.color: root.stateColor
                border.width: 1
            }

            LLOLabel {
                id: blockNameLabel
                text: BlockNames.labelName(Controller.selectedBlockLabel)
                font.bold: true
                color: Theme.textPrimary
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                // The row keeps room for the state, the switch and the
                // actions; the label gives way.
                Layout.minimumWidth: 0
                Layout.maximumWidth: Math.max(0, root.rowWidth - root.actionReserve - root.stateRoom)
            }

            LLOLabel {
                id: statusLabel
                visible: root.showsStatus
                text: root.statusWord
                color: Theme.textSecondary
            }

            LLOButton {
                // The action that takes the user's text back (ADR 296): it
                // stands with the yellow status, in the header.
                id: restoreButton
                visible: root.documentDiverged
                text: qsTr("Restore the block text")
                implicitHeight: switchControl.implicitHeight
                topPadding: 0
                bottomPadding: 0
                onClicked: Controller.releaseSelectedBlockUserText()
            }

            Item {
                Layout.fillWidth: true
            }

            SegmentedControl {
                id: switchControl
                visible: root.hasLayers && root.switchInHeader
                options: root.switchOptions
                currentIndex: root.viewIndex
                onActivated: (index) => root.selectView(index)
            }

            LLOButton {
                id: closeButton
                text: "✕"
                implicitWidth: 24
                implicitHeight: switchControl.implicitHeight
                topPadding: 0
                bottomPadding: 0
                onClicked: Controller.selectedBoxIndex = -1
            }
        }
```

The approve button (`approveButton`) is removed from this row — Task 4 puts the action into the header of the layer it acts on.

The width budget of the row follows the new content:

```qml
    readonly property real actionReserve:
          (root.switchInHeader && switchControl.visible ? switchControl.implicitWidth : 0)
        + (restoreButton.visible ? restoreButton.implicitWidth : 0)
        + closeButton.implicitWidth
        + Theme.spacingSmall * 6

    readonly property real stateRoom:
          (statusLabel.visible ? statusLabel.implicitWidth : 0)
        + Theme.spacingSmall * 3

    readonly property real headerNeed:
          Theme.spacing
        + 10
        + blockNameLabel.implicitWidth
        + root.stateRoom
        + (root.hasLayers ? switchControl.implicitWidth : 0)
        + (restoreButton.visible ? restoreButton.implicitWidth : 0)
        + closeButton.implicitWidth
        + Theme.spacingSmall * 5
```

The old `stateRoom` (three words), `showOverallStatus` and the labels `overallWord`, `syntaxWord` and `readWord` are removed — the status of the block is one word now, and the notes of the pipeline stand with their layer (Task 4).

- [ ] **Step 5: The divergence of the document**

The panel's own condition for the divergence keeps only the per-block flag — the flag of the page was redundant with it:

```qml
    // The document keeps the user's own wording for this block (ADR 296):
    // the panel says so in the header and offers to restore the block's text.
    readonly property bool documentDiverged: root.isTextBlock
                                             && Controller.selectedBlockDocumentDiverged
```

- [ ] **Step 6: The row under the text is gone**

Delete the whole `RowLayout` with the id `noticeRow` (the label «The document text differs from the block» and the button «Restore the block text»).

- [ ] **Step 7: Run the test to verify it passes**

```sh
cmake --build build -j 8 --target test_block_panel_qml
ctest --test-dir build -R test_block_panel_qml --output-on-failure
```

Expected: PASS — the four new tests and the old ones that still apply. The old test `theNoticeStandsUnderTheText` is removed (the notice is gone), and `theApproveActionIsNamed` was removed in Step 1 — the action comes back in the header of the layer in Task 4.

- [ ] **Step 8: Commit**

```sh
git add src/qml/MainWindow/BlockEditPanel.qml tests/test_block_panel_qml.cpp tests/fake_block_controller.qml
git commit -m "feat: one vocabulary of states for the block"
```

---

### Task 3: The switch of the layers

The switch reads **OCR | VLM | Разница** and stands only where the block carries both an OCR result and a correction — there is nothing to switch between otherwise, and the panel shows the one layer that is there.

**Files:**
- Modify: `src/qml/MainWindow/BlockEditPanel.qml`
- Test: `tests/test_block_panel_qml.cpp`

**Interfaces:**
- Consumes: `userEdited` and `statusWord` of Task 2.
- Produces: `hasLayers` (bool — both layers are there) and `switchOptions` (the three segments). Task 4 reads `hasLayers` to decide which layer stands in view.

- [ ] **Step 1: Write the failing test**

Replace the slots `theSwitchHasThreeSegments` and `theSegmentsSayWhenThereIsNoSecondVariant` with:

```cpp
    void theSwitchHasThreeSegments();
    void theSwitchIsGoneWithoutBothLayers();
```

and the bodies:

```cpp
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

    m_controller->setProperty("selectedBlockCorrected", "E = mc^2 + 1");
    QTest::qWait(20);
    QVERIFY2(isVisible(theSwitch(panel.get())), "the switch must stand where both layers are there");
}
```

- [ ] **Step 2: Run the test to verify it fails**

```sh
cmake --build build -j 8 --target test_block_panel_qml
ctest --test-dir build -R test_block_panel_qml --output-on-failure
```

Expected: FAIL — the segments still say «Correction» and «Comparison», and the switch is still there without a correction.

- [ ] **Step 3: The segments and the visibility**

In `src/qml/MainWindow/BlockEditPanel.qml`:

```qml
    // The switch of the layers: the recognized text, the VLM's word and the
    // difference between them.
    readonly property var switchOptions: [
        { label: qsTr("OCR") },
        { label: qsTr("VLM") },
        { label: qsTr("Difference") }
    ]
```

and the property that decides whether there is anything to switch between:

```qml
    // A block with nothing to switch between: the markup alone, a picture, a
    // block with one layer only. The switch of the layers has nothing to say
    // and is gone.
    readonly property bool hasLayers: Controller.selectedBlockText.length > 0
                                     && root.correctionStored
```

The switch itself (both copies — the one in the header row and the one on the line below) stays as it is; only its options and its visibility change. The `enabled` flag of the segments is gone with the options that carried it: where the switch stands, all three segments can be chosen.

The correction that disappears takes its segment with it — the panel returns to the OCR layer:

```qml
    onCorrectionStoredChanged: {
        if (!root.correctionStored && root.viewIndex !== 0)
            root.viewIndex = 0
    }
```

(the handler stays as it is; it is listed here so the implementer does not touch it).

- [ ] **Step 4: Run the test to verify it passes**

```sh
cmake --build build -j 8 --target test_block_panel_qml
ctest --test-dir build -R test_block_panel_qml --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```sh
git add src/qml/MainWindow/BlockEditPanel.qml tests/test_block_panel_qml.cpp
git commit -m "feat: the switch of the layers reads OCR, VLM, Difference"
```

---

### Task 4: The header of a text layer

The card of a layer names the layer (**OCR** or **VLM**), carries the note of the pipeline, the score («Оценка: 43%»), the approve action and the chip «В документе» (ADR 339).

**Files:**
- Modify: `src/qml/MainWindow/BlockTextPanel.qml`, `src/qml/MainWindow/BlockEditPanel.qml`
- Test: `tests/test_block_panel_qml.cpp`

**Interfaces:**
- Consumes: `userEdited`, `hasLayers`, `correctionStored` of Tasks 2–3; `Controller.selectedBlockDocumentDiverged`.
- Produces: `BlockTextPanel` gains the properties `scoreText`, `scoreColor`, `approveVisible`, `approveText` and the signal `approveRequested()`; it loses `verdictText`, `confidenceText`, `verdictColor`, `empty` and `emptyText`.

- [ ] **Step 1: Write the failing test**

In `tests/test_block_panel_qml.cpp`, keep the slots `thePanelSwitchesToACorrectionThatLandsOnTheBlock`, `theComparisonShowsBothLayers` and `theComparisonTintsTheDiff` — their behavior does not change, only the names of the layers they look for. Replace «Recognized text (OCR)» with «OCR» and «Correction (AI)» with «VLM» in them:

```cpp
    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY2(original, "the panel must carry the panel of the recognized text");
    QObject *vlm = thePanelWithTitle(panel.get(), QStringLiteral("VLM"));
    QVERIFY2(vlm, "the panel must carry the panel of the VLM's word");
```

(the variable `correction` may keep its name — the card is the VLM's word now, the name of the variable is free).

Add the slots:

```cpp
    void theLayersAreNamed();
    void theNoteStandsWithItsLayer();
    void theScoreStandsWithItsLayer();
    void theApproveActionStandsWithItsLayer();
    void theChipStandsInTheComparison();
```

and the bodies:

```cpp
// The layers name themselves: the recognized text and the VLM's word.
void TestBlockPanelQml::theLayersAreNamed()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QVERIFY2(thePanelWithTitle(panel.get(), QStringLiteral("OCR")),
             "the panel must show the recognized text as OCR");
    QVERIFY2(thePanelWithTitle(panel.get(), QStringLiteral("VLM")),
             "the panel must show the correction as VLM");
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
    m_controller->setProperty("selectedBlockCorrected", "E = mc^2 + 1");
    m_controller->setProperty("selectedBlockCheckStatus", 1);
    m_controller->setProperty("selectedBlockConfidence", 0.9);
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
}

// The approve action acts on the layer it stands in: it is in the header of
// the layer, not in the header of the module.
void TestBlockPanelQml::theApproveActionStandsWithItsLayer()
{
    const std::unique_ptr<QObject> panel = loadPanel();
    QVERIFY2(panel, qPrintable(m_warnings.join(QStringLiteral("; "))));

    QObject *original = thePanelWithTitle(panel.get(), QStringLiteral("OCR"));
    QVERIFY(original);
    QVERIFY2(original->property("approveVisible").toBool(),
             "the layer in view must offer to approve the block");

    // The action is gone where the block is the user's.
    m_controller->setProperty("selectedBlockDocumentDiverged", true);
    QTest::qWait(20);
    QVERIFY2(!original->property("approveVisible").toBool(),
             "the approve action must be gone while the block is yellow");

    m_controller->setProperty("selectedBlockDocumentDiverged", false);
    QTest::qWait(20);
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
    QVERIFY2(original->property("inDocument").toBool(),
             "the comparison must mark the layer the document carries");
    QVERIFY2(!vlm->property("inDocument").toBool(),
             "the comparison must mark one layer only");
}
```

The old slot `theApproveActionIsNamed` is removed: the action is named «Approve» / «Revoke» and stands in the layer now — `theApproveActionStandsWithItsLayer` covers it, and the name of the action is checked by the translation task.

- [ ] **Step 2: Run the test to verify it fails**

```sh
cmake --build build -j 8 --target test_block_panel_qml
ctest --test-dir build -R test_block_panel_qml --output-on-failure
```

Expected: FAIL — the layers are still named «Recognized text (OCR)» and «Correction (AI)», and the card has no `scoreText`.

- [ ] **Step 3: The card of a layer**

In `src/qml/MainWindow/BlockTextPanel.qml`, replace the properties block with:

```qml
    property string title: ""
    property string noteText: ""
    property string scoreText: ""
    property color scoreColor: Theme.nothing
    property bool inDocument: false
    property string html: ""
    property bool mono: false
    // The approve action belongs to the layer it acts on (ADR 339).
    property bool approveVisible: false
    property string approveText: qsTr("Approve")

    signal approveRequested()
```

and the header row with:

```qml
            RowLayout {
                id: headerRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Theme.spacingSmall
                anchors.rightMargin: Theme.spacingSmall
                spacing: Theme.spacingSmall

                LLOLabel {
                    id: titleLabel
                    text: root.title
                    font.bold: true
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    // The name of the layer keeps its room: the note and the
                    // score give way to it, not the other way round.
                    Layout.maximumWidth: Math.max(0, root.width - root.rightRoom - root.noteRoom)
                }

                LLOLabel {
                    // What the pipeline did with this layer: the note stands
                    // next to the layer it speaks about.
                    id: noteLabel
                    visible: root.noteText.length > 0
                    text: "· " + root.noteText
                    color: Theme.textMuted
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    Layout.maximumWidth: root.noteLimit
                }

                LLOLabel {
                    id: scorePrefix
                    visible: root.scoreText.length > 0
                    text: qsTr("· Score:")
                    color: Theme.textMuted
                }

                LLOLabel {
                    id: scoreLabel
                    visible: root.scoreText.length > 0
                    text: root.scoreText
                    color: root.scoreColor
                    font.bold: true
                }

                LLOButton {
                    id: approveButton
                    visible: root.approveVisible
                    text: root.approveText
                    topPadding: 0
                    bottomPadding: 0
                    onClicked: root.approveRequested()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Ignore the check result")
                }

                Item {
                    Layout.fillWidth: true
                }

                Rectangle {
                    id: documentChipBox
                    visible: root.inDocument
                    Layout.preferredWidth: documentChip.implicitWidth + 12
                    Layout.preferredHeight: 18
                    radius: 9
                    color: Theme.selected
                    border.color: Theme.border
                    border.width: 1

                    LLOLabel {
                        id: documentChip
                        anchors.centerIn: parent
                        text: qsTr("In the document")
                        color: Theme.textSecondary
                        font: Theme.caption
                    }
                }
            }
```

The room the row keeps for the elements on the right:

```qml
    readonly property real rightRoom: (scorePrefix.visible ? scorePrefix.implicitWidth : 0)
                                  + (scoreLabel.visible ? scoreLabel.implicitWidth : 0)
                                  + (approveButton.visible ? approveButton.implicitWidth : 0)
                                  + (documentChipBox.visible ? documentChipBox.implicitWidth : 0)
                                  + Theme.spacingSmall * 4
```

The empty state of the card is gone with the empty layer: a layer that is not there has no card (the panel shows the one layer that is there). Delete the `LLOLabel` that shows `root.emptyText` and the `visible: !root.empty` of the `ScrollView` — the text always stands.

- [ ] **Step 4: The layers in the module**

In `src/qml/MainWindow/BlockEditPanel.qml`, replace the two `BlockTextPanel` blocks with:

```qml
        // The recognized text: the OCR result and the score it earned.
        BlockTextPanel {
            id: originalPanel
            Layout.fillWidth: true
            visible: root.showOriginal
            title: qsTr("OCR")
            noteText: root.originalNote
            scoreText: root.layerScore(Controller.selectedBlockConfidence, Controller.selectedBlockCheckStatus)
            scoreColor: root.scoreColor(Controller.selectedBlockCheckStatus)
            approveVisible: root.showOriginal && !root.compareMode && !root.userEdited
            approveText: root.approved ? qsTr("Revoke") : qsTr("Approve")
            onApproveRequested: root.approve()
            inDocument: root.compareMode && root.documentCarriesOcr && !root.userEdited
            html: root.originalHtml
            mono: root.monoBlock
        }

        // The VLM's layer: the correction of the universal model or the text
        // the reader wrote from the image. It stands only where there is one.
        BlockTextPanel {
            id: vlmPanel
            Layout.fillWidth: true
            visible: root.showVlm
            title: qsTr("VLM")
            noteText: root.vlmNote
            scoreText: root.layerScore(Controller.selectedBlockCorrectedConfidence,
                                        Controller.selectedBlockCorrectedCheckStatus)
            scoreColor: root.scoreColor(Controller.selectedBlockCorrectedCheckStatus)
            approveVisible: root.showVlm && !root.compareMode && !root.userEdited
            approveText: root.approved ? qsTr("Revoke") : qsTr("Approve")
            onApproveRequested: root.approve()
            inDocument: root.compareMode && root.documentCarriesVlm && !root.userEdited
            html: root.correctedHtml
            mono: root.monoBlock
        }
```

- [ ] **Step 5: The properties the layers read**

In the same file, replace the properties `showOriginal` and `showCorrection` with the ones below, add the notes and the color of the score, remove the old functions `layerStatusWord(status)` and `dotColor(status)` — the cards read the new ones now — and remove `correctionInView` (nothing reads it any more; the layers in view are `showOriginal` and `showVlm` now):

```qml
    // The layer in view: the OCR text or the VLM's word. The card of a layer
    // that is not there does not stand.
    readonly property bool showOriginal: root.isTextBlock
                                         && Controller.selectedBlockText.length > 0
                                         && (root.compareMode || root.viewIndex === 0 || !root.hasLayers)
    readonly property bool showVlm: root.isTextBlock
                                    && root.correctionStored
                                    && (root.compareMode || root.viewIndex === 1 || !root.hasLayers)
    // The layer the block carries: the VLM's word when the block points at
    // it, else the recognized text.
    readonly property bool documentCarriesVlm: Controller.selectedBlockUsesCorrected
                                               && root.correctionStored
    readonly property bool documentCarriesOcr: !root.documentCarriesVlm
    // What the pipeline did to the recognized text: the fixer's answer and
    // the reader's (ADR 260). The kind of the broken LaTeX belongs to the
    // layer that carries it (ADR 194).
    readonly property string originalNote: root.layerNote(Controller.selectedBlockCheckStatus,
                                               root.readDamaged ? qsTr("Could not read the block")
                                               : root.readConfirmed ? qsTr("Read: the text matches the image")
                                               : root.fixNotNeeded ? qsTr("No correction required")
                                               : "")
    readonly property string vlmNote: root.layerNote(Controller.selectedBlockCorrectedCheckStatus,
                                          root.readApplied ? qsTr("Read from the image") : "")

    function layerNote(status, readNote) {
        if (status === 7)
            return root.syntaxProblem
        return readNote
    }

    // The score wears the color of the verdict it belongs to.
    function scoreColor(status) {
        if (status === 3 || status === 4 || status === 7)
            return Theme.error
        if (status === 1 || status === 5)
            return Theme.success
        return Theme.nothing
    }

    // The approve action acts on the layer in view — the switch moves the
    // document, so the layer the operator reads is the layer the block
    // carries (ADR 164).
    function approve() {
        if (root.approved)
            Controller.revertBlockApproval()
        else
            Controller.approveSelectedBlock()
    }
```

- [ ] **Step 6: Run the test to verify it passes**

```sh
cmake --build build -j 8 --target test_block_panel_qml
ctest --test-dir build -R test_block_panel_qml --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```sh
git add src/qml/MainWindow/BlockEditPanel.qml src/qml/MainWindow/BlockTextPanel.qml tests/test_block_panel_qml.cpp
git commit -m "feat: the header of a layer reads the name, the score and the action"
```

---

### Task 5: The status of the page

The page speaks the same words as the block: no markup block — no status, markup — «Размечено», unrecognized blocks — «Распознано», a checked page with an error — «Есть ошибки», a checked page the user wrote into — «Отредактировано», a clean page — «Проверено» (ADR 338).

**Files:**
- Modify: `src/qml/PageStatus.qml`, `src/qml/MainWindow/WorkPanel.qml`, `src/qml/MainWindow/ThumbDelegate.qml`
- Create: `tests/test_page_status_qml.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Controller.currentPageRecognitionMode`, `Controller.currentPageVerification`, `Controller.currentPageEdited`, `Controller.currentPageOrientationProblem`.
- Produces: `PageStatus.statusText(mode, verification, edited, orientationProblem)` (string) and `PageStatus.statusColor(mode, verification, edited, orientationProblem)` (color) — the two call sites below are their only users.

- [ ] **Step 1: Write the failing test**

Create `tests/test_page_status_qml.cpp`:

```cpp
// The page status: the word and the color the page speaks with (ADR 338).
// The mapping lives in QML, so the test reads the shipped file.

#include <QDir>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickStyle>
#include <QtTest>
#include <memory>

class TestPageStatusQml : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void aPageWithoutMarkupHasNoStatus();
    void aMarkedUpPageIsMarkedUp();
    void anUnjudgedPageIsRecognized();
    void aPageWithAnErrorHasErrors();
    void anEditedPageIsEdited();
    void aVerifiedPageIsVerified();
    void theOrientationOutranksEverything();

private:
    QString statusText(int mode, int verification, bool edited, int orientation);
    QString statusColor(int mode, int verification, bool edited, int orientation);
    QString themeColor(const QString &name);

    QString m_qmlRoot;
    std::unique_ptr<QQmlEngine> m_engine;
    QObject *m_status = nullptr;
    QObject *m_theme = nullptr;
    QStringList m_warnings;
};

void TestPageStatusQml::initTestCase()
{
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!m_qmlRoot.isEmpty());

    const auto registerFile = [](const QString &path, const char *name) {
        const QUrl url = QUrl::fromLocalFile(path);
        qmlRegisterSingletonType(url, "LLocr", 1, 0, name);
    };
    registerFile(m_qmlRoot + QStringLiteral("/Theme.qml"), "Theme");
    registerFile(m_qmlRoot + QStringLiteral("/PageStatus.qml"), "PageStatus");

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QmlError &error : reported)
            m_warnings.append(error.toString());
    });

    m_status = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("PageStatus"));
    QVERIFY(m_status);
    m_theme = m_engine->singletonInstance<QObject *>(QStringLiteral("LLocr"), QStringLiteral("Theme"));
    QVERIFY(m_theme);
}

QString TestPageStatusQml::statusText(int mode, int verification, bool edited, int orientation)
{
    QVariant result;
    const bool ok = QMetaObject::invokeMethod(m_status, "statusText", Q_RETURN_ARG(QVariant, result),
                                              Q_ARG(QVariant, mode), Q_ARG(QVariant, verification),
                                              Q_ARG(QVariant, edited), Q_ARG(QVariant, orientation));
    if (!QTest::qVerify(ok, "the page status has a statusText()", __FILE__, __LINE__))
        return QString();
    return result.toString();
}

QString TestPageStatusQml::statusColor(int mode, int verification, bool edited, int orientation)
{
    QVariant result;
    const bool ok = QMetaObject::invokeMethod(m_status, "statusColor", Q_RETURN_ARG(QVariant, result),
                                              Q_ARG(QVariant, mode), Q_ARG(QVariant, verification),
                                              Q_ARG(QVariant, edited), Q_ARG(QVariant, orientation));
    if (!QTest::qVerify(ok, "the page status has a statusColor()", __FILE__, __LINE__))
        return QString();
    const QColor color = result.value<QColor>();
    return color.isValid() ? color.name() : QString();
}

QString TestPageStatusQml::themeColor(const QString &name)
{
    return m_theme->property(name.toUtf8().constData()).value<QColor>().name();
}

// A page without a markup block has nothing to say.
void TestPageStatusQml::aPageWithoutMarkupHasNoStatus()
{
    QCOMPARE(statusText(0, 0, false, 0), QString());
    QCOMPARE(statusText(2, 0, false, 0), QString());
}

// The markup alone: the page says it is marked up.
void TestPageStatusQml::aMarkedUpPageIsMarkedUp()
{
    QCOMPARE(statusText(1, 0, false, 0), QStringLiteral("Marked up"));
    QCOMPARE(statusColor(1, 0, false, 0), themeColor(QStringLiteral("nothing")));
}

// Every block is recognized, one of them is not judged yet.
void TestPageStatusQml::anUnjudgedPageIsRecognized()
{
    QCOMPARE(statusText(3, 0, false, 0), QStringLiteral("Recognized"));
    QCOMPARE(statusColor(3, 0, false, 0), themeColor(QStringLiteral("nothing")));
}

// Every block is judged and one of them has an error.
void TestPageStatusQml::aPageWithAnErrorHasErrors()
{
    QCOMPARE(statusText(3, 1, false, 0), QStringLiteral("Errors"));
    QCOMPARE(statusColor(3, 1, false, 0), themeColor(QStringLiteral("error")));
}

// Every block is judged and one of them was written by hand.
void TestPageStatusQml::anEditedPageIsEdited()
{
    QCOMPARE(statusText(3, 2, true, 0), QStringLiteral("Edited"));
    QCOMPARE(statusColor(3, 2, true, 0), themeColor(QStringLiteral("warning")));
}

// Every block is judged and none of them has an error.
void TestPageStatusQml::aVerifiedPageIsVerified()
{
    QCOMPARE(statusText(3, 2, false, 0), QStringLiteral("Verified"));
    QCOMPARE(statusColor(3, 2, false, 0), themeColor(QStringLiteral("success")));
}

// The page lies on its side: it needs a turn before anything else.
void TestPageStatusQml::theOrientationOutranksEverything()
{
    QCOMPARE(statusText(3, 2, false, 1), QStringLiteral("On its side"));
    QCOMPARE(statusColor(3, 2, false, 1), themeColor(QStringLiteral("error")));
    QCOMPARE(statusText(3, 2, false, 2), QStringLiteral("Orientation unclear"));
}

QTEST_MAIN(TestPageStatusQml)
#include "test_page_status_qml.moc"
```

- [ ] **Step 2: Register the test**

In `tests/CMakeLists.txt`, next to the registration of `test_block_panel_qml`:

```cmake
# The page status: the word and the color of every state of the page
# (ADR 338). The mapping lives in QML, so the test reads the shipped file.
qt_add_executable(test_page_status_qml test_page_status_qml.cpp)

target_link_libraries(test_page_status_qml PRIVATE
    Qt6::Test
    Qt6::Quick
    Qt6::QuickControls2
    llocr_app
)
add_test(NAME test_page_status_qml COMMAND test_page_status_qml)
```

- [ ] **Step 3: Run the test to verify it fails**

```sh
cmake --build build -j 8 --target test_page_status_qml
ctest --test-dir build -R test_page_status_qml --output-on-failure
```

Expected: FAIL — `statusText` takes three arguments and knows no `edited` flag.

- [ ] **Step 4: The mapping of the page**

Replace the body of `src/qml/PageStatus.qml`:

```qml
pragma Singleton

import QtQuick

import LLocr

QtObject {
    // The page status (ADR 338): one vocabulary with the block. The
    // orientation the check could not settle outranks everything — the page
    // needs a turn by hand. A page without a markup block has nothing to
    // say. The recognition mode says how far the page came: the markup
    // alone («Размечено»), every block recognized («Распознано»). The
    // verification aggregate then speaks: an unanswered block keeps the page
    // at «Распознано», an error makes it erroneous, and a clean page is
    // «Проверено» — or «Отредактировано», when the user wrote a block of it.
    function statusKind(mode, verification, edited, orientationProblem) {
        if (orientationProblem === 1 || orientationProblem === 2)
            return "orientation"
        if (mode === 0 || mode === 2)
            return "none"
        if (mode === 1)
            return "markup"
        if (verification === 1)
            return "errors"
        if (verification === 2)
            return edited ? "edited" : "verified"
        return "recognized"
    }

    function statusText(mode, verification, edited, orientationProblem) {
        const kind = statusKind(mode, verification, edited, orientationProblem)
        if (kind === "orientation")
            return orientationProblem === 1 ? qsTr("On its side") : qsTr("Orientation unclear")
        if (kind === "none")
            return ""
        if (kind === "markup")
            return qsTr("Marked up")
        if (kind === "errors")
            return qsTr("Errors")
        if (kind === "edited")
            return qsTr("Edited")
        if (kind === "verified")
            return qsTr("Verified")
        return qsTr("Recognized")
    }

    function statusColor(mode, verification, edited, orientationProblem) {
        const kind = statusKind(mode, verification, edited, orientationProblem)
        if (kind === "orientation" || kind === "errors")
            return Theme.error
        if (kind === "edited")
            return Theme.warning
        if (kind === "verified")
            return Theme.success
        if (kind === "none")
            return "transparent"
        return Theme.nothing
    }
}
```

- [ ] **Step 5: The two places the page speaks**

In `src/qml/MainWindow/WorkPanel.qml`, replace the label:

```qml
            LLOLabel {
                visible: Controller.currentPageEditable
                text: PageStatus.statusText(Controller.currentPageRecognitionMode,
                                            Controller.currentPageVerification,
                                            Controller.currentPageEdited,
                                            Controller.currentPageOrientationProblem)
                color: PageStatus.statusColor(Controller.currentPageRecognitionMode,
                                              Controller.currentPageVerification,
                                              Controller.currentPageEdited,
                                              Controller.currentPageOrientationProblem)
            }
```

In `src/qml/MainWindow/ThumbDelegate.qml`, replace the property and the color of the dot:

```qml
    readonly property string statusKind: PageStatus.statusKind(pageRecognitionMode, pageVerification, edited, orientationProblem)
    readonly property string statusText: PageStatus.statusText(pageRecognitionMode, pageVerification, edited, orientationProblem)
    readonly property color statusColor: PageStatus.statusColor(pageRecognitionMode, pageVerification, edited, orientationProblem)
```

```qml
                Rectangle {
                    Layout.preferredWidth: 9
                    Layout.preferredHeight: 9
                    radius: 5
                    color: hasDuplicates ? "#d32f2f" : delegateRoot.statusColor
                    border.width: delegateRoot.statusKind === "none" ? 1 : 0
                    border.color: Theme.textMuted
                    ...
                }
```

(the `Accessible.name` of the dot keeps using `statusText`; the old `aggregateDone` property is removed — the aggregate is inside the mapping now).

- [ ] **Step 6: Run the test to verify it passes**

```sh
cmake --build build -j 8 --target test_page_status_qml
ctest --test-dir build -R test_page_status_qml --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```sh
git add src/qml/PageStatus.qml src/qml/MainWindow/WorkPanel.qml src/qml/MainWindow/ThumbDelegate.qml \
        tests/test_page_status_qml.cpp tests/CMakeLists.txt
git commit -m "feat: the page speaks the words of the block"
```

---

### Task 6: The Russian words and the whole verification

The catalog is refreshed with lupdate and the new words are translated by hand: the shipped app must not fall back to English.

**Files:**
- Modify: `resources/i18n/llocr_ru.ts`
- Modify: `docs/09-tasks-and-verification.md` and `docs/09-tasks-and-verification.ru.md` (the block and page statuses are described there — check and keep them in step)

**Interfaces:**
- Consumes: every string of Tasks 2–5.
- Produces: the catalog with the new words.

- [ ] **Step 1: Refresh the catalog**

```sh
cmake --build build --target update_translations
```

Expected: `resources/i18n/llocr_ru.ts` is rewritten — the new strings appear with an empty translation, the removed ones are gone.

- [ ] **Step 2: Translate the changed words by hand**

The new or changed entries (the context is the file the string lives in):

| Context | Source | Translation |
| --- | --- | --- |
| `BlockEditPanel` | `Edited` | `Отредактирован` |
| `BlockEditPanel` | `Errors` | `Есть ошибки` |
| `BlockEditPanel` | `Damaged` | `Повреждён` |
| `BlockEditPanel` | `Invalid syntax` | `Неверный синтаксис` |
| `BlockEditPanel` | `No errors` | `Нет ошибок` |
| `BlockEditPanel` | `Recognized` | `Распознан` |
| `BlockEditPanel` | `Could not read the block` | `Не удалось прочитать блок` |
| `BlockEditPanel` | `Read from the image` | `Прочитано с изображения` |
| `BlockTextPanel` | `· Score:` | `· Оценка:` |
| `BlockTextPanel` | `Ignore the check result` | `Игнорировать результат проверки` |
| `BlockTextPanel` | `In the document` | `В документе` |
| `BlockTextPanel` | `VLM` | `VLM` |
| `BlockEditPanel` | `VLM` | `VLM` |
| `BlockEditPanel` | `Difference` | `Разница` |
| `PageStatus` | `Marked up` | `Размечено` |
| `PageStatus` | `Recognized` | `Распознано` |
| `PageStatus` | `Errors` | `Есть ошибки` |
| `PageStatus` | `Edited` | `Отредактировано` |
| `PageStatus` | `Verified` | `Проверено` |

The strings that stay as they are: `OCR`, `Approve` → `Утвердить`, `Revoke` → `Отозвать`, `Restore the block text` → `Вернуть текст блока`, `No correction required` → `Не требует исправления`, `Read: the text matches the image`, `On its side` → `Лежит боком`, `Orientation unclear` → `Ориентация не определена`.

Remove the entries of the words that are gone: `Recognized text (OCR)`, `Correction (AI)`, `Read text (AI)`, `Comparison`, `Correction`, `· Check:`, `The document text differs from the block`, `Markup only`, `Recognized (without markup)`, `Verified (no errors)`, `Verified (with errors)`.

- [ ] **Step 3: Build the catalog and check that no word is left untranslated**

```sh
cmake --build build -j 8
grep -c "unfinished" resources/i18n/llocr_ru.ts
```

Expected: the count is not higher than it was before this change (29 before it — the pre-existing unfinished entries are not this change's business; every *new* entry is translated).

- [ ] **Step 4: The documents that describe the statuses**

`docs/09-tasks-and-verification.md` describes the block and page statuses. Read the parts that name the old words («Проверка», «Исправлено», «Прочитано», «Размечено») and bring them in step with the new vocabulary; do the same in `docs/09-tasks-and-verification.ru.md`.

The ADR log must speak with one voice: ADR 323 still declares the segments of the switch as «OCR | Исправление | Сравнение» and the rule that a segment without a second variant stays in the row but says it is unavailable. Mark it as amended by ADR 339 in `docs/07-glossary.md` and in `docs/07-glossary.ru.md`.

- [ ] **Step 5: The whole verification**

```sh
scripts/check.sh
```

Expected: `OK` — the build, all the tests, clang-format and qmllint are clean.

- [ ] **Step 6: Commit**

```sh
git add resources/i18n/llocr_ru.ts docs/09-tasks-and-verification.md docs/09-tasks-and-verification.ru.md
git commit -m "docs: the Russian words of the new statuses"
```

---

## Self-review

**Spec coverage:** the click that passes through the panel — Task 1; the six words of the block and the five of the page — Tasks 2 and 5; the header of the module — Task 2; the header of a layer (name, note, score, approve action, chip) — Task 4; the switch — Task 3; the removed row under the text — Task 2; the translations — Task 6.

**Placeholders:** none — every task carries the code it needs.

**Type consistency:** `statusWord` (string), `stateColor` (color), `userEdited` (bool), `hasLayers` (bool), `switchOptions` (var), `scoreText` (string), `scoreColor` (color), `approveVisible` (bool), `approveText` (string), `approveRequested()` (signal) — the same names in the implementation and in the tests.
