# The block panel: one status vocabulary, two headers

> A design note of 27 November 2026. It covers the block inspector
> (`MainWindow/BlockEditPanel.qml`, `MainWindow/BlockTextPanel.qml`), the
> status of the page (`PageStatus.qml`) and the click that passes through the
> panel. The durable record is in the ADR log (`docs/07-glossary.md`,
> ADR 338–340).

## The problem

The block panel grew a language of its own. The header carried a dot without
a word, the panel of a layer carried the verdict of the checking model
(«Проверка: Не совпало 43%»), the block carried a state of its own
(«Исправлено», «Прочитано», «Размечено»), and the notice about the document
text differing from the block stood under the text as a separate element.
Three places said three different things about the same block, and the
operator had to learn which of them was the truth.

Two more problems came with it:

- The panel is a plain `Rectangle`, so it does not take the mouse. A click
  that misses a button inside the panel reaches the preview underneath: the
  operator selects another block or closes the panel they were reading.
- The status of the page was a mapping of three numbers
  (`PageRecognitionMode`, `PageVerification`, `PageOrientationProblem`) whose
  words did not match the words of the block.

## What is decided

### 1. The panel covers the space under it

`BlockEditPanel.qml` gets a `MouseArea` that fills the panel and stands
**under** its content (declared first). The area swallows every press over
the background of the panel, so a click that misses a button stays inside the
panel. The buttons, the switch and the text fields are declared above the
area and keep their events.

### 2. One status vocabulary for a block

The block's status is the state of the effective layer — the one the
document carries — with one exception: a block the user has written is the user's, and
the model's verdict about the previous text does not speak for it.

| State | Word | Dot |
| --- | --- | --- |
| The block carries no text at all (markup only) | — no dot, no word | — |
| The user wrote the text (in the WYSIWYG editor or into the layer) | Отредактирован | yellow |
| The formula's LaTeX is broken | Неверный синтаксис | red |
| The block could not be read | Повреждён | red |
| The decision model rejected the text | Есть ошибки | red |
| The decision model confirmed the text, or the user approved it | Нет ошибок | green |
| Nobody judged the block, but it carries a text | Распознан | gray |

The word stands in the header of the module as ordinary text; the dot carries
the color. The status of the block is derived, not stored: the effective layer
gives the verdict, the user layer (`BoundingBox::userText`, ADR 296) or a
hand-written layer (`BoxCheckStatus::Edited`) gives the yellow, and a block
without a text gives nothing at all.

### 3. The header of the module

    dot → name of the block type → status (text) → «Вернуть текст блока»
        → switch → ✕

«Вернуть текст блока» stands only where the user wrote the text in the
editor — there is something to take back there. The approve action is gone
from this row: it belongs to the layer it acts on.

### 4. The header of a text layer

    OCR / VLM → note → «· Оценка:» → «43%» → «Утвердить» → «В документе»

- The layer names itself **OCR** or **VLM**. The correction and the text
  the reader wrote are both the VLM's word.
- The note (grey) is what the pipeline did to this layer: «Не требует
  исправления», «Прочитано с изображения», the kind of the broken LaTeX.
- The score reads «· Оценка: 43%» — the prefix is ordinary text, the number
  is green when the check confirmed the text and red when it rejected it.
  A layer without a score of its own (approved, written by hand, not
  judged) shows neither the prefix nor the number.
- «Утвердить» (tooltip: «Игнорировать результат проверки») stands when the
  layer in view is OCR or VLM and the block is not yellow. While the
  approval stands the button reads «Отозвать». The action acts on the layer
  in view — the switch moves the document, so the layer the operator reads is
  the layer the block carries.
- The chip «В документе» stands in the comparison alone, on the layer the
  document carries, and not while the block is yellow.

### 5. The switch

**OCR | VLM | Разница**. It stands only when the block carries both an OCR
result and a correction — there is nothing to switch between otherwise, and
the panel shows the one layer that is there. The switch keeps moving the
document: the layer the operator reads is the layer the document carries
(ADR 164, ADR 185).

### 6. The notice under the text is gone

The row «Текст в документе отличается от блока» with the action that settles
it is removed. The yellow status and the button in the header say the same
thing in the place where the state of the block belongs.

### 7. The status of the page

The page speaks with the same words as the block:

| Condition | Word | Color |
| --- | --- | --- |
| The page carries no markup block | — nothing | — |
| There is markup, not every block is recognized | Размечено | gray |
| Every block is recognized, one of them is not judged | Распознано | gray |
| Every block is judged, one of them has an error | Есть ошибки | red |
| Every block is judged or approved, one of them was written by hand | Отредактировано | yellow |
| Every block is judged, no error | Проверено | green |

An approved block counts as a judged one with a green result. An error
outranks a hand-written block: the page needs a human eye first. The
orientation problem keeps the top place it had — a page that lies on its
side needs a turn before anything else is said about it.

The page speaks in two places: the label of the work panel and the chip of
the thumbnail. Both take the word and its color from `PageStatus.qml`.

## The data behind it

Everything the panel needs is already there:

- `Controller.selectedBlockCheckStatus` / `selectedBlockCorrectedCheckStatus`
  — the verdicts of the two layers (`BoxCheckStatus`).
- `Controller.selectedBlockCorrected` — the correction layer.
- `Controller.selectedBlockDocumentDiverged` — the user wrote the block's
  text in the editor (`BoundingBox::userText`).
- `Controller.selectedBlockConfidence` / `selectedBlockCorrectedConfidence`
  — the score of the decision model.
- `Controller.currentPageRecognitionMode` / `currentPageVerification` /
  `currentPageEdited` — the state of the page.

`PageStatus.qml` grows a `statusColor()` beside `statusText()`, and
`statusText()` takes the edited flag of the page. The C++ side does not
change: the aggregation of the verification (`PageStatus::verificationAggregate`)
and the recognition mode (`PageStatus::recognitionModeAfterBlockText`) already
answer what the page status needs.

## What is checked

- `tests/test_block_panel_qml.cpp` — the panel's header, the switch, the
  chip, the approve action and the status: rewritten for the new words, the
  new order and the removed row.
- A new test for the layout of the page status — the word and its color for
  every state of the page.
- `tests/test_page_status.cpp` — the C++ aggregation does not change.

## The decisions in the ADR log

- **ADR 338** — the block's status vocabulary and the page's status.
- **ADR 339** — the headers of the module and of the layer.
- **ADR 340** — the panel covers the space under it.
