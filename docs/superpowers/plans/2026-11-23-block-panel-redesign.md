# The block panel reads like a text — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The block inspector names the block in the interface language, carries the block's state as a dot, and shows the recognized text, the correction or both through a three-segment switch — with pastel panels.

**Architecture:** `BlockNames.qml` learns the labels the layout models speak (`equation`, `image_caption`, …) and names them in the interface language. `MainWindow/BlockVersionCard.qml` becomes `MainWindow/BlockTextPanel.qml` — a panel of one text layer with a header that names the layer, carries the verdict of the checking model and the confidence. `MainWindow/BlockEditPanel.qml` keeps the header (the state dot, the name, the switch, the approve action, the close button) and the body (one panel, or two in the comparison mode). The comparison mode replaces the `Controller.highlightDifferences` property: the tinting of the diff follows the mode of the view.

**Tech Stack:** Qt6 / QML (QtQuick Controls 2), CMake, QtTest, Qt LinguistTools.

**Spec:** `docs/superpowers/specs/2026-11-23-block-panel-redesign-design.md`

## Global Constraints

- QML: sizes and fonts only via `Theme.*` (`src/qml/Theme.qml`) — never hardcode a font or an icon size in QML.
- The UI must not touch the network or the filesystem (AGENTS.md rule 2).
- Comments only in extreme cases (AGENTS.md rule 7): no restating of the code, no section banners, no TODOs, no commented-out code.
- Every decision change goes into `docs/07-glossary.md` (rule 5) — the new ADR is **323**; the docs are bilingual, so `docs/07-glossary.ru.md` gets its mirror.
- Build system is CMake (rule 4); reuse the ready-made `build/` tree, never re-configure from scratch.
- Tests that construct `SettingsStore` declare `TestSettingsIsolation m_settingsIsolation;` as the **first** member (`tests/testsettings.h`).
- A QML test registers the singletons it needs under the `LLocr` module URI (`tests/test_block_panel_qml.cpp` does it for `Theme.qml`).
- The full gate before the commit: `cmake --build build -j 8` → `ctest --test-dir build` → `scripts/check.sh --format-only` (clang-format) → `qmllint`.
- The translations live in `resources/i18n/llocr_ru.ts`; the `.qm` must stay in the target's resources (ADR 304 — no `QM_FILES_OUTPUT_VARIABLE`).

## File Map

**Created:**
- `src/qml/MainWindow/BlockTextPanel.qml` — the panel of one text layer: the header (the layer's circle, the title, the verdict, the confidence, the «В документе» chip) and the text.

**Modified:**
- `src/qml/BlockNames.qml` — `labelNames` (the label a model carries → the name a person reads) and `labelName(label)`.
- `src/qml/MainWindow/BlockEditPanel.qml` — the new header (the dot, the name, the switch, the approve action, the close button), the body (one panel or two), the divergence strip.
- `src/qml/Theme.qml` — the pastel colors of the panels.
- `src/app/BlockInspector.h` / `.cpp` — `highlightDifferences` is gone.
- `src/app/AppController.h` / `.cpp` — the property, the setter and the signal of `highlightDifferences` are gone.
- `tests/fake_block_controller.qml` — the double follows the new panel.
- `tests/test_block_panel_qml.cpp` — the new header is checked.
- `resources/i18n/llocr_ru.ts` — the new words.
- `docs/07-glossary.md` / `.ru.md` — ADR 323.

**Deleted:**
- `src/qml/MainWindow/BlockVersionCard.qml` — replaced by `BlockTextPanel.qml`.

---

### Task 1: The names of the blocks

**Files:**
- Modify: `src/qml/BlockNames.qml`

**Interfaces:**
- Produces: `BlockNames.labelName(label: string): string` — the name of a block by the label the model carries; an unknown label stays as it is.

- [ ] **Step 1: Add the map and the function to `BlockNames.qml`**

```qml
    // The name of a block by the label the model carries. The models speak
    // their own vocabulary («equation», «image_caption»); the operator reads
    // a name.
    readonly property var labelNames: ({
        "text": qsTr("Text"),
        "title": qsTr("Heading"),
        "doc_title": qsTr("Document title"),
        "paragraph_title": qsTr("Paragraph heading"),
        "abstract": qsTr("Abstract"),
        "table": qsTr("Table"),
        "table_title": qsTr("Table title"),
        "table_caption": qsTr("Table caption"),
        "table_footnote": qsTr("Table footnote"),
        "equation": qsTr("Equation"),
        "formula": qsTr("Formula"),
        "inline_formula": qsTr("Inline formula"),
        "formula_number": qsTr("Formula number"),
        "image": qsTr("Image"),
        "chart": qsTr("Chart"),
        "figure": qsTr("Figure"),
        "image_caption": qsTr("Image caption"),
        "figure_caption": qsTr("Figure caption"),
        "image_footnote": qsTr("Image footnote"),
        "caption": qsTr("Caption"),
        "code": qsTr("Code"),
        "code_caption": qsTr("Code caption"),
        "algorithm": qsTr("Algorithm"),
        "list": qsTr("List"),
        "list_item": qsTr("List item"),
        "index": qsTr("Index"),
        "phonetic": qsTr("Transcription"),
        "aside_text": qsTr("Aside"),
        "ref_text": qsTr("Reference text"),
        "reference": qsTr("Reference"),
        "key_value": qsTr("Key-value field"),
        "header": qsTr("Running header"),
        "footer": qsTr("Running footer"),
        "page_number": qsTr("Page number"),
        "seal": qsTr("Seal")
    })

    // The name of a block by the label a model carries. An unknown label
    // stays as it is: nothing is lost.
    function labelName(label) {
        if (!label)
            return ""
        return labelNames[label] !== undefined ? labelNames[label] : label
    }
```

- [ ] **Step 2: Check the QML file passes the lint**

Run: `~/Qt/6.10.3/macos/bin/qmllint src/qml/BlockNames.qml`
Expected: no errors.

---

### Task 2: The panel of a text layer

**Files:**
- Create: `src/qml/MainWindow/BlockTextPanel.qml`
- Delete: `src/qml/MainWindow/BlockVersionCard.qml`
- Modify: `src/qml/Theme.qml`

**Interfaces:**
- Consumes: `Theme.cardSurface`, `Theme.cardBorder`, `Theme.divider`, `Theme.textPrimary`, `Theme.textSecondary`, `Theme.textMuted`.
- Produces: `BlockTextPanel` — the properties `title` (string), `verdictText` (string), `confidenceText` (string), `verdictColor` (color), `inDocument` (bool), `compared` (bool), `html` (string), `mono` (bool), `empty` (bool), `emptyText` (string).

- [ ] **Step 1: Add the pastel colors to `Theme.qml`**

```qml
    // The panels of the block inspector: the header of a panel is a soft
    // strip, the text stands on the card itself.
    readonly property color panelHeaderBg: dark ? "#232323" : "#f4f5f7"
    readonly property color panelBodyBg: dark ? "#2f2f2f" : "#ffffff"
```

- [ ] **Step 2: Create `src/qml/MainWindow/BlockTextPanel.qml`**

```qml
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr

import "../Common"

// One layer of a block: the recognized text or the correction. The panel
// names its layer, carries the verdict of the checking model and the text
// itself. The circle at the left says whether the layer is the one the
// document shows.
Rectangle {
    id: root

    property string title: ""
    property string verdictText: ""
    property string confidenceText: ""
    property color verdictColor: Theme.textMuted
    property bool inDocument: false
    property string html: ""
    property bool mono: false
    property bool empty: false
    property string emptyText: ""

    implicitHeight: layout.implicitHeight + 2 * Theme.spacingSmall
    color: Theme.panelBodyBg
    border.color: Theme.cardBorder
    border.width: 1
    radius: Theme.radius

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spacingSmall
        spacing: Theme.spacingSmall

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            Rectangle {
                Layout.preferredWidth: 10
                Layout.preferredHeight: 10
                radius: 5
                color: root.inDocument ? Theme.textPrimary : "transparent"
                border.color: Theme.border
                border.width: 1
            }

            LLOLabel {
                text: root.title
                font.bold: true
                color: Theme.textPrimary
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
                Layout.maximumWidth: Math.max(0, root.width - 320)
            }

            LLOLabel {
                visible: root.verdictText.length > 0
                text: qsTr("· Check:")
                color: Theme.textMuted
            }

            LLOLabel {
                visible: root.verdictText.length > 0
                text: root.verdictText
                color: root.verdictColor
                font.bold: true
            }

            LLOLabel {
                visible: root.confidenceText.length > 0
                text: root.confidenceText
                color: root.verdictColor
            }

            Item {
                Layout.fillWidth: true
            }

            Rectangle {
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

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }

        LLOLabel {
            visible: root.empty
            Layout.fillWidth: true
            text: root.emptyText
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }

        Rectangle {
            visible: !root.empty
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(Math.max(textView.contentHeight, 18) + 2 * Theme.spacingSmall, 120)
            color: Theme.panelBodyBg
            border.color: Theme.cardBorder
            border.width: 1
            radius: Theme.radius
            clip: true

            ScrollView {
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                contentWidth: availableWidth

                TextArea {
                    id: textView
                    width: parent.width
                    readOnly: true
                    textFormat: TextArea.RichText
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    color: Theme.textPrimary
                    font: root.mono ? Theme.mono : Theme.caption
                    background: null
                    topPadding: 0
                    bottomPadding: 0
                    text: root.html
                }
            }
        }
    }
}
```

- [ ] **Step 3: Delete `src/qml/MainWindow/BlockVersionCard.qml`**

Run: `git rm src/qml/MainWindow/BlockVersionCard.qml`

- [ ] **Step 4: Check the lint**

Run: `~/Qt/6.10.3/macos/bin/qmllint src/qml/MainWindow/BlockTextPanel.qml`
Expected: no errors.

---

### Task 3: The new header and the body of the panel

**Files:**
- Modify: `src/qml/MainWindow/BlockEditPanel.qml`

**Interfaces:**
- Consumes: `BlockNames.labelName(label)`, `BlockTextPanel`, `Common/SegmentedControl.qml` (`options`, `currentIndex`, `activated(int)`), `Controller.selectedBlock*`.
- Produces: the panel's own `stateColor` (color) — the color of the state dot.

- [ ] **Step 1: Write the new header row**

The row is: the state dot, the name of the block, the state word, the switch, the approve action, the close button. The divergence notice and its action move to a strip under the row (the row has no room for them at the panel's width).

```qml
    readonly property color stateColor: root.dotColor(root.effectiveStatus)
    readonly property bool compareMode: false
    readonly property int switchIndex: root.compareMode ? 2
                                        : (Controller.selectedBlockUsesCorrected ? 1 : 0)
    readonly property var switchOptions: [
        { label: qsTr("OCR") },
        { label: qsTr("Correction") },
        { label: qsTr("Comparison") }
    ]

    function selectView(index) {
        if (index === 2) {
            root.compareMode = true
            return
        }
        root.compareMode = false
        Controller.selectedBlockUsesCorrected = (index === 1)
    }
```

- [ ] **Step 2: Write the body**

```qml
        // The recognized text: the OCR result and the verdict it earned.
        BlockTextPanel {
            id: originalPanel
            Layout.fillWidth: true
            visible: root.showOriginal
            title: qsTr("Recognized text (OCR)")
            verdictText: root.statusWord(Controller.selectedBlockCheckStatus)
            confidenceText: root.layerScore(Controller.selectedBlockConfidence, Controller.selectedBlockCheckStatus)
            verdictColor: root.dotColor(Controller.selectedBlockCheckStatus)
            inDocument: !Controller.selectedBlockUsesCorrected
            html: root.originalHtml
            mono: root.monoBlock
        }

        // The correction layer: the universal model's fix, judged separately
        // from the text.
        BlockTextPanel {
            id: correctionPanel
            Layout.fillWidth: true
            visible: root.showCorrection
            title: root.readApplied ? qsTr("Read text (AI)") : qsTr("Correction (AI)")
            verdictText: root.statusWord(Controller.selectedBlockCorrectedCheckStatus)
            confidenceText: root.layerScore(Controller.selectedBlockCorrectedConfidence, Controller.selectedBlockCorrectedCheckStatus)
            verdictColor: root.dotColor(Controller.selectedBlockCorrectedCheckStatus)
            inDocument: Controller.selectedBlockUsesCorrected && root.correctionStored
            html: root.correctedHtml
            mono: root.monoBlock
            empty: !root.correctionStored
            emptyText: qsTr("No correction yet")
        }
```

with

```qml
    readonly property bool showOriginal: root.isTextBlock
                                         && (root.compareMode || root.switchIndex === 0)
    readonly property bool showCorrection: root.isTextBlock && root.correctionStored
                                           && (root.compareMode || root.switchIndex === 1)
```

- [ ] **Step 3: Drop the reset action**

The «Reset correction» button and `Controller.revertBlockCorrection()` leave the panel: a repeated fix and the choice of the layer make it needless.

- [ ] **Step 4: The tinting follows the comparison mode**

```qml
    readonly property string originalHtml: layerHtml(Controller.selectedBlockText,
                                                    root.compareMode ? Controller.selectedBlockRemovedRanges : [],
                                                    Theme.diffRemovedBg,
                                                    Theme.diffRemovedText)
    readonly property string correctedHtml: layerHtml(Controller.selectedBlockCorrected,
                                                     root.compareMode ? Controller.selectedBlockAddedRanges : [],
                                                     Theme.diffAddedBg,
                                                     Theme.diffAddedText)
```

- [ ] **Step 5: Check the lint**

Run: `~/Qt/6.10.3/macos/bin/qmllint src/qml/MainWindow/BlockEditPanel.qml`
Expected: no errors.

---

### Task 4: The C++ loses the diff switch

**Files:**
- Modify: `src/app/BlockInspector.h`, `src/app/BlockInspector.cpp`, `src/app/AppController.h`, `src/app/AppController.cpp`

**Interfaces:**
- Removes: `BlockInspector::setHighlightDifferences(bool)`, `BlockInspector::highlightDifferencesChanged()`, `AppController::setHighlightDifferences(bool)`, `AppController::highlightDifferencesChanged()`, the property `AppController::highlightDifferences`.
- Keeps: `AppController::selectedBlockRemovedRanges`, `AppController::selectedBlockAddedRanges` and the signal `blockDiffChanged`.

- [ ] **Step 1: Remove the member, the setter and the signal from `BlockInspector`**

- [ ] **Step 2: Remove the property, the setter, the getter and the signal from `AppController`**

- [ ] **Step 3: Remove the connection in `AppController`'s constructor**

- [ ] **Step 4: Build**

Run: `cmake --build build -j 8`
Expected: the build succeeds.

---

### Task 5: The test of the panel

**Files:**
- Modify: `tests/fake_block_controller.qml`, `tests/test_block_panel_qml.cpp`

**Interfaces:**
- Consumes: the panel's `stateColor` property and the switch's `activated(int)`.

- [ ] **Step 1: Update the double**

`tests/fake_block_controller.qml` loses `highlightDifferences` and `revertBlockCorrection()`; it gains a corrected text with a verdict, so the comparison mode has two layers to show.

- [ ] **Step 2: Write the new test cases**

```cpp
void TestBlockPanelQml::theNameOfTheBlockIsInTheInterfaceLanguage();
void TestBlockPanelQml::theSwitchHasThreeSegments();
void TestBlockPanelQml::theComparisonShowsBothLayers();
void TestBlockPanelQml::theResetActionIsGone();
void TestBlockPanelQml::theNoticeStandsUnderTheHeaderRow();
void TestBlockPanelQml::theApproveActionNamesTheLayer();
void TestBlockPanelQml::theStateDotFollowsTheVerdict();
```

- [ ] **Step 3: Run the test**

Run: `cmake --build build -j 8 --target test_block_panel_qml && ctest --test-dir build -R test_block_panel_qml --output-on-failure`
Expected: PASS.

---

### Task 6: The words and the decision

**Files:**
- Modify: `resources/i18n/llocr_ru.ts`, `docs/07-glossary.md`, `docs/07-glossary.ru.md`

- [ ] **Step 1: Refresh the catalog**

Run: `~/Qt/6.10.3/macos/bin/lupdate src -ts resources/i18n/llocr_ru.ts`
Expected: the new words appear as unfinished.

- [ ] **Step 2: Translate the new words into Russian**

Every new `<source>` gets a `<translation>`: «Уравнение», «Заголовок», «Текст», «Таблица», «Формула», «Изображение», «Сравнение», «Исправление», «Проверка:», «В документе», «Исправлений пока нет», «Утвердить», «Отозвать», «· Проверка:».

- [ ] **Step 3: Write ADR 323**

The ADR states: the panel names the block in the interface language; the header carries the state as a dot; the switch has three segments and moves the document; the comparison mode is a state of the view, and the `highlightDifferences` property is gone; «Сбросить исправление» is gone.

- [ ] **Step 4: The Russian mirror of the ADR**

- [ ] **Step 5: The full gate**

Run: `scripts/check.sh`
Expected: the build, the tests, the format check and the lint pass.
