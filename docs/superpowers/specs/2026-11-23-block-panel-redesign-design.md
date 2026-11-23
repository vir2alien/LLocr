# The block panel reads like a text — design

**Date:** 2026-11-23
**Status:** approved

## The problem

The block panel (the inspector that floats over the image preview) speaks the
model's language and the pipeline's language:

* the header carries the raw label of the block (`equation`, `image_caption`)
  — the name a model uses, not a name a person reads;
* the header carries the actions of the pipeline («Различия», «Сбросить
  исправление», «Утвердить») and no state: the operator sees
  buttons where a state belongs;
* the body shows two cards at once, each of them a switch (clicking a card
  makes its version the one the document shows) — the operator must read the
  cards to learn what he is looking at;
* the verdict lives in a colored badge, the confidence inside it, and the
  word of the layer in a bold title — three pieces of information in one
  colored pill.

The operator wants one thing: read the block's text, see whether the machine
trusts it, and decide. The panel must answer that in one look.

## The decision

The panel becomes **a header of state and a text block**:

* the header names the block (in the interface language) and carries the
  block's state as a dot;
* a three-segment switch chooses what the panel shows: the recognized text,
  the correction, or both;
* the body is one text panel — or two, one above the other, in the
  comparison mode;
* the header of a text panel says what the panel is, what the checking model
  thought of it and how sure it was.

The switch follows the app's own `Common/SegmentedControl.qml`; the colors
are pastel; the actions that the pipeline made obsolete are gone.

## The header

One row, left to right:

1. **The state dot** (10 px). The same language the block's frame uses on the
   preview (ADR 238): gray — not judged / markup only, red — a mismatch, a
   damage or a broken formula, green — verified, approved or written by hand.
   The dot and the name stand as far from the left edge as they stand from
   the top one — the insents of the header are equal.
2. **The name of the block** — the translated label (see below). It elides;
   the actions keep their room.
3. **The switch** — `OCR | Исправление | Сравнение`.
4. **The approve action** — one word for the action: «Утвердить», and
   «Отозвать» while the approval stands. The layer the panel shows does not
   change the name: the panel approves the block, not a layer of it.
5. **The close button** (✕).

The switch and the buttons of the header stand on one line: their height is
the height of the switch (24 px).

The switch says what it can do: a block that carries the markup alone has
nothing to switch between, so the switch is gone; a block without a
correction has no second variant, so the segments «Исправление» and
«Сравнение» stay in the row but say they are not available — the switch of
the app grew the per-segment switch-off for it.

The divergence notice («Документ отличается от блока») and the action that
settles it («Вернуть текст блока») stay in the header row (ADR 307): the
notice gives way, the actions do not.

«Сбросить исправление» is gone: a repeated fix and the choice of the layer
in the document make it needless.

## The text panel

One panel per layer. Its header:

```
Распознанный текст (OCR) · Проверка: не совпало 0%        [В документе]
```

* the **title** says what the panel is;
* the **note** — what the pipeline did with this layer (the fixer's «не
  требует исправления») — stands next to the title, in the muted color;
* the **verdict** and the **confidence** of the checking model follow the
  title, separated by a dot; the confidence is left out where the layer
  carries a human state (approved, written by hand) — a human state has no
  score of its own (ADR 187);
* the **chip** «В документе» stands at the right of the layer the document
  shows. The layer in view is named by the switch of the header, so the
  panel carries no circle of its own.

The body is the text of the layer; a formula, a table and a snippet are read
in the monospace face, as today.

### The modes

| The switch | The body |
| --- | --- |
| OCR | the recognized text |
| Исправление | the correction, or the empty state «Исправлений пока нет» when there is none |
| Сравнение | both panels, one above the other: the recognized text above, the correction below |

In the comparison mode the fragments the correction dropped are tinted in the
recognized text (pastel red), the fragments it put in are tinted in the
correction (pastel green). The tinting belongs to the comparison mode alone:
a single layer is read clean.

The switch also moves the document: choosing «Исправление» puts the
correction into the document, choosing «OCR» puts the recognized text back —
the same effect the click on a card had (ADR 164, ADR 185).

## The names of the blocks

`BlockNames.qml` gains a map of the label a model carries → the name a person
reads, in the singular: `equation` → «Уравнение», `text` → «Текст»,
`title` → «Заголовок», and the whole vocabulary the layout models speak —
captions, footnotes, running heads and feet, page numbers, formulas, lists,
code, algorithms, transcriptions, asides, document and paragraph titles.

A label nobody knows stays as it is: nothing is lost, and the panel never
shows an empty name.

## The technical frame

* `Controller.highlightDifferences` is removed: the comparison is a state of
  the view now, and the tinting follows it. The diff ranges themselves stay
  (`selectedBlockRemovedRanges` / `selectedBlockAddedRanges`).
* `MainWindow/BlockVersionCard.qml` becomes `MainWindow/BlockTextPanel.qml`:
  the panel of a text layer with the header above.
* `MainWindow/BlockEditPanel.qml` keeps the header and the body and reads
  both from the controller as before.
* The colors live in `Theme.qml`: the pastel tints of the panels and the
  header of a panel.
* The tests: `tests/test_block_panel_qml.cpp` checks the new header — the
  translated name, the three segments, the comparison mode, the absence of
  the reset action.

## What is not in the scope

* the block frames on the preview (ADR 238) — they keep their own language;
* the editor and the export — untouched;
* the pipeline — the panel reads it, it does not change it.
