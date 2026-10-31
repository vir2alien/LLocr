# Протокол чтения блока: `read_block` рядом с `full_block`

**Дата:** 2026-10-31 · **ADR:** 259 · **Замеры:** `OCRtest/fix_models/READ_PROTOCOL.md`

**Что это.** У задачи правки появляется второй протокол: модель **читает блок с
картинки сама**, а распознанный текст приходит ей как **образец записи
документа** — не как кандидат для исправления. Ответ — полный блок, который
модель прочитала; гейт сравнивает его с текущим текстом блока, и приложение
решает, менять текст или нет.

**Зачем.** Замер на корпусе с неверными кандидатами: чтение точнее правки
(CER 0.040 у чтения с якорем против 0.050 у правки; 0.015 у чтения без якоря), а
попадание кириллических индексов — 0.84 против 0.74 у правки. На `p03b14`
читатель починил `н.впр`, который чинитель оставил латинским `H.впр`.

**Почему не «просто выкинуть кандидата».** Без кандидата читатель не знает
записи документа и переписывает 9 из 10 верных блоков, из них 5 — с осмысленным
изменением (начертание, регистр, символ, потерянный подстрочный индекс). С
образцом записи профиль ущерба совпадает с базовым: 3 осмысленных, 1
испорченный по содержанию.

---

## 1. Провод

### 1.1 Запрос роли `fix` при `protocol: "read_block"`

```
system: read-requests.json → systemPrompt   ({{LANGUAGE}} подставляется как сейчас)
user:   [ {type: text,  text: <тип-подсказка блока>},
          {type: image, image_url: <кроп блока>},
          {type: text,  text: "The block as it was recognized (a sample of the
                              document's notation, not the answer):
                              <RECOGNIZED>
                              {текст блока}
                              </RECOGNIZED>"} ]
```

Отличие от `full_block` — ровно две вещи: рамка вокруг распознанного текста
(`<RECOGNIZED>` вместо `<CANDIDATE>`) и тексты промптов. Порядок частей тот же
(тип-подсказка → картинка → текст); ось порядка измерена и рычагом не
является.

### 1.2 Ответ

`FIX` + полный блок, как модель его прочитала, либо `REVIEW` — блок не
читается.

`OK` из схемы убран: модель, которая не видела кандидата, не может сказать
«кандидат верен». Ответ вне схемы (включая `OK`) ⇒ `Review` с причиной в
подвале — так же, как отклонение от схемы в `full_block`. Почему не «ничего не
делать»: молчаливое «ничего не делать» снимает с блока стухое `Mismatch`, то
есть прячет настоящую ошибку; красная метка «нужен человек» хотя бы видна.
Вероятность мала: промпт прямо велит `FIX | REVIEW`.

### 1.3 Что делает приложение

Гейт не меняется — `FixDiffGate::fixBlock(candidate, body, blockType)` сравнивает
прочитанное с текущим текстом блока:

| ответ модели | сравнение | вердикт | что с блоком |
| --- | --- | --- | --- |
| `FIX` + текст | эквивалентен по правилам гейта | `Ok` | текст не меняется, блок подтверждён, стухое `Mismatch` снимается |
| `FIX` + текст | отличается | `Fixed` | текст блока заменяется прочитанным |
| `REVIEW` | — | `Review` | блок помечен «нужен человек» |
| пусто или обрезано бюджетом | — | `Review` + причина | то же |

Слой коррекции (ADR 164) работает страховкой: исходный текст остаётся в
`box.text`, прочитанное — в `box.correctedText`, слой переключается.

### 1.4 Гейт: два новых носителя

`FixDiffGate::mathNormalize` знает `\[…\]`≡`$$…$$` и `\tag{n}`≡`(n)`. Замер
показал ещё два носителя, которые гейт пропускал:

- `\eqno{n}` и `\eqno(n)` ≡ `\tag{n}` ≡ `(n)` — другое написание номера формулы;
- `{{x}}` ≡ `{x}` — удвоенная группа рендерится как одинарная.

С ними переписанных верных блоков на риск-корпусе становится: `full_block` 3,
`read_block` (без якоря) 7, `read_block` с якорем **3** — ровно как у базы.

## 2. Где живёт выбор

### 2.1 Профиль роли в профиле модели

```json
"fix": { "protocol": "read_block", "promptProfile": "read-requests", … }
```

Пустой `protocol` ⇒ `full_block`: старые профили и пользовательские модели
работают как раньше. Поле `protocol` уже парсится (`ModelProfiles.cpp:277`),
сегодня оно не читается.

### 2.2 Общий профиль промптов

`resources/profiles/requests/read-requests.json` — по ADR 182 тексты задачи
живут в одном месте и разделяются моделями. Профили промптов грузятся по
glob (`src/CMakeLists.txt`, `requests/*.json`) — правка CMake не нужна.

Ключи те же, что у `fix-requests.json`: `systemPrompt`, `blockPrompts`,
`blockAliases`, `maxOutputByBlock`, `reasoningModes`.

- `systemPrompt` — измеренный текст (§4.1), дословно.
- `blockPrompts` — все 15 типов (§4.2).
- `maxOutputByBlock` — как в `fix-requests.json`, кроме `equation`: 2048 →
  **4096** (на `p05b09` измеренный ответ резался бюджетом — единственный
  `Review` в замере без кандидата).
- `blockAliases` — те же.
- `reasoningModes` — те же (`off` / `low` / `med` / `high`); по умолчанию
  `reasoning_budget_tokens` = 0, то есть мышление выключено.

### 2.3 Кто читает, а кто чинит

| модель | правка (`full_block`) | чтение без кандидата | чтение с якорем | в v1 |
| --- | --- | --- | --- | --- |
| `gemma-4-12b` | 0.050 | 0.015 | **0.040** | **читатель** |
| `gemma-4-e4b` | 0.153 | 0.102 (0.087 с подсказкой) | не измерено | чинитель — риск на ней не снят |
| `minicpm-o-4.5` (снята с диска) | 0.154 | 0.076 (0.051 с подсказкой) | не измерено | — |
| `qwen3.5-9b` | **0.077** | 0.236 † | не измерено | чинитель — правка лучше чтения |
| `qwen3.5-4b` | **0.142** | 0.187 † | не измерено | чинитель |

† Для `qwen3.5-4b` и `qwen3.5-9b` измерен протокол **транскрипции** (другая
схема ответа, без гейта), а не тот, что внедряется. Числа показывают
направление, но прямого сравнения с внедряемым протоколом для этих двух моделей
нет.

Риск снят только на `gemma-4-12b`; на остальные модели он не переносится.

## 3. Код

### 3.1 `src/models/FixModel.cpp`

- `buildRequestBody`: при `read_block` часть с распознанным текстом собирается
  как `<RECOGNIZED>` с припиской «a sample of the document's notation, not the
  answer»; при `full_block` — как сейчас, в `<CANDIDATE>` с припиской
  «OCR candidate:».
- `parseResponse`: при `read_block` ветка `OK` пропускается, всё остальное
  (ветка `FIX`, гейт, `REVIEW`, отклонение от схемы) — как сейчас. Текст
  отклонения при `read_block` называет схему `FIX/REVIEW`, а не `OK/FIX/REVIEW`.
- Протокол берётся из профиля роли: `ModelProfiles::roleFor(modelId, "fix")` —
  он уже доступен в `buildRequestBody`, а `parseResponse` получает его из
  `request.modelId` (тот же статический список профилей).
- Роль `fix` с `protocol: "read_block"` обязана называть профиль промптов.
  Без него системный промпт пуст, модель не знает схему ответа, и все ответы
  уходят в `Review` — это следствие текущего устройства профилей, а не
  отдельная проверка.

### 3.2 `src/models/FixDiffGate.cpp`

`mathNormalize` учится двум носителям (§1.4). Правка локальная: сравнение
идёт только для текста с обратной косой чертой.

### 3.3 `src/core/CheckRequest.h`

Комментарий поля `recognizedText`: текст блока нужен гейту как база сравнения;
в сеть он уходит только в `full_block` (в `read_block` — как образец записи).

### 3.4 Профили

- `resources/profiles/models/gemma-4-12b.json`: `roles.fix.protocol` →
  `read_block`, `roles.fix.promptProfile` → `read-requests`.
- `resources/profiles/requests/read-requests.json` — новый файл (§4).

### 3.5 Чего в v1 нет

- **Персональная подсказка о скрипте.** По замеру она помогает слабым читателям
  (e4b 0.102 → 0.087) и вредит сильным (gemma-12b 0.015 → 0.020), то есть она
  per-model. Механизм — «общий системный промпт + свой» в `applyPromptProfile`
  (сейчас он затирает собственный текст модели общим). В v1 ни одна
  модель-читатель свой текст не везёт, поэтому правка не делается; она
  понадобится, когда читателем станет слабая модель.
- **Отдельная задача «читатель»** в `ConnectionRole` — не заводилась: читатель
  пишет в тот же слой коррекции и делит гейт с чинителем.
- **Текстовые блоки с доказанным риском.** Протокол их охватывает, но доказательств
  по ним нет (§6).

## 4. Тексты промптов

### 4.1 Системный промпт (`read-requests.json` → `systemPrompt`)

Дословно как измеренный (`probe.py`: `FIX_NO_CANDIDATE_SYSTEM` + заметка об
образце записи). `{{LANGUAGE}}` подставляет приложение.

```
You produce the corrected text of one block of a technical document. The image is the only source of truth: write what the image shows, not what the formula usually looks like.
Document language: {{LANGUAGE}}.

Write the block exactly as the image shows it: the same characters, the same script, the same case, the same face (upright, italic, bold), the same position of every subscript and superscript, the same nesting depth, the same delimiters. For a formula, write the complete LaTeX of the formula, including its tag.

Read every axis on its own — a block can be right on one and wrong on another:
1. Script. Designations, indices and tags are written in the document's alphabet. Cyrillic read as Latin is an error even though the glyph looks right: judge by the designation, not by the glyph. Greek letters used as symbols are correct.
2. Case. Uppercase and lowercase differ and carry meaning.
3. Face. Upright, italic, bold and bold italic differ and carry meaning.
4. Position. Baseline, subscript, superscript and nesting depth differ.
5. Delimiters. Every opening bracket is paired; none is stray, duplicated, or missing.
6. Unit integrity. A designation, index or tag is one whole: read all of its characters, never mix scripts inside it.
7. Small print. Subscripts and superscripts are the hardest part of the image: read them at the size they are printed, do not guess them from a familiar formula.

Do not add content that is not in the image. Do not complete a formula you cannot fully read. Do not simplify, factor, expand or restructure it.

Reply with exactly one of these and nothing else:

FIX
<the complete corrected block>
REVIEW

Use REVIEW when a character is cut by the crop, covered, or blurred past recognition. No explanations, no code fences.

The block as it was recognized is given below as a sample of how this document writes: its delimiters, macros, fonts and tag style. Read the content yourself from the image — where the sample and the image disagree, follow the image. Never copy the sample's content, and never keep a character of it that the image contradicts.
```

Два предложения заголовка — «not what the formula usually looks like» и «Do not
complete a formula you cannot fully read» — оставлены как измерены: переписывать
их значило бы оторвать замер от протокола. Правила по типу блока несут
собственные формулировки.

### 4.2 Тип-подсказки

Правило переноса с промптов `fix-requests.json`: правило «Never fix»
(эквивалентность — сравнивать не с чем) уходит; «Always fix» → «Always get
right»; «Preserve the candidate's macro style» → «Keep the author's notation,
macro style…»; правила письма (не переверстывать строки, не менять формат
даты, не додумывать размытый символ) остаются.

Для `equation` — дословно измеренная подсказка (`probe.py`:
`NO_CANDIDATE_TYPE`):

```
Block type: mathematical formula.
Mode after FIX: the complete formula as LaTeX, written by you from the image.

Write the formula as the image shows it. Keep the author's notation, macro style and visible bold/italic distinctions; do not simplify, expand, factor or restructure it.

Always get right: base variables; subscript vs superscript (R_1 vs R_i vs R^1); Greek vs Latin (\alpha vs a, \nu vs v, \rho vs p, \kappa vs k); italic vs upright vs bold; operators (+, -, \pm, \times, \cdot, =, \approx, \neq); summation and integral limits; radicals; fractions; brackets; prime, hat, bar and vec accents; integral-sign class; a missing line, fragment, comma or period.

Tags, indices and designations are content, not carrier: a Latin suffix where the document uses Cyrillic is an error. Inside a tag, index or designation, a character that looks like a digit may be a letter the image shows — decide by the image.
```

Остальные 14 типов — перенос по правилу выше; их тексты пишутся в
`read-requests.json` при реализации. Что именно меняется в каждом типе:

| тип | уходит | приходит |
| --- | --- | --- |
| `text` | «Never fix: NFC/NFD, NBSP …» | «Write the text exactly as the image shows it: the same characters, the same punctuation, the same spacing.» |
| `title` | «Never fix: quote style, NBSP vs space» | — |
| `abstract` | «Never fix: NBSP, soft hyphens …» | — |
| `list` | «Never fix: bullet glyph … when the nesting level is unchanged» | «Write the bullet glyph, the marker and the indentation exactly as the image shows them.» |
| `code` | «Nothing is equivalent here.» | «Nothing here is carrier markup.» |
| `caption` | «Never fix: NBSP, quote style, line-wrap reflow» | — |
| `footnote` | «Never fix: NBSP, quote style» | — |
| `ref_text` | «Never fix: NBSP, quote style» | — |
| `header`, `footer` | «Never fix: NBSP, quote style» | — |
| `page_number` | — | — (правило «Never infer or complete an ambiguous digit: answer REVIEW» остаётся) |
| `seal` | «Never fix: NBSP, quote style» | — (правило «Never guess a smudged … character: answer REVIEW» остаётся) |
| `key_value` | «Never fix: NBSP, quote style» | — |
| `table` | «Output the table in the candidate's serialization … Change only the cells the image contradicts; reproduce every unchanged cell exactly.» | «Output the table in the sample's serialization (HTML if the sample is HTML, a pipe table if it is Markdown). Write every cell as the image shows it, and keep the cell position, rowspan, colspan, and empty cells as they are.» |

Во всех типах: «Mode after FIX: the complete corrected block.» → «Mode after
FIX: the complete block as you read it from the image.»; «Always fix:» → «Always
get right:»; «Preserve the candidate's …» → «Keep the author's …». Правила
письма (не переверстывать строки, не менять формат даты, не додумывать размытый
символ, не упрощать формулу) остаются как есть.

## 5. Проверка

### 5.1 Тесты (`tests/`)

- `test_fix_protocol`:
  - запрос при `protocol: "read_block"` (модель `gemma-4-12b`) содержит
    `<RECOGNIZED>` и не содержит `<CANDIDATE>`; при `full_block` — наоборот
    (регресс на прежний протокол);
  - разбор ответа при `read_block`: `FIX` с текстом ⇒ `Fixed` (через гейт);
    `REVIEW` ⇒ `Review`; `OK` ⇒ `Review` с причиной; пустой ответ ⇒ `Review`;
    ответ, обрезанный бюджетом (`finish_reason: length`) ⇒ `Review`;
  - разбор ответа при `full_block` не меняется (регресс).
- `test_fix_diff_gate`: `\eqno{n}` и `\eqno(n)` ≡ `\tag{n}` ≡ `(n)`;
  `{{x}}` ≡ `{x}`; строгие пары (е/ё, омоглифы, `\mathrm{R}` против `R`)
  по-прежнему различаются.
- `test_ocr_models`: у каждой модели с ролью `fix` объявлен протокол из
  известных (`full_block`, `read_block`), а `promptProfile` читаем.
- Регистрация `read-requests.json` в `tests/CMakeLists.txt` рядом с
  `fix-requests.json` — тесты грузят профили промптов как ресурс.

### 5.2 Приёмка

Прогон авто-пайплайна (разметка → распознавание → проверка → исправление →
проверка исправленного) на странице с формулами, три счёта: сколько блоков
правка переписала, сколько из них с осмысленным изменением (начертание,
регистр, символ, потерянный индекс), сколько ложных тревог. Сравнение — с
тем же документом под `full_block` (профиль `gemma-4-12b` до и после).

## 6. Оговорки

- Доказательства — только на формулах: риск-корпус и корпус с неверными
  кандидатами состоят из формул. Для текстовых блоков риск переписывания
  абзаца не измерен и заведомо выше (гейт абзац не сравнивает).
- Корпус риска — 10 блоков: направление, а не статистика.
- Риск снят на `gemma-4-12b`; на другие модели не переносится.
- Тип-подсказки для 14 типов, кроме `equation`, не измерены.
- Мышление для читателя с якорем не измерено; по умолчанию выключено, что
  соответствует замеру без якоря (мышление вредит читателям).
- Кириллические индексы (`2.69б`, `63а`) не починил ни один вариант ни одной
  модели — глиф неотличим от «6»/«a», нужно знание конвенции документа.
- Цена читателя — время: 134 с против 102 с на длинных формулах (CPU).
