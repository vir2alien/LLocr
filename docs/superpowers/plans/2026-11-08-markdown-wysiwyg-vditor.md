# Markdown WYSIWYG Editor (Vditor) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Заменить вкладку «Предпросмотр» на WYSIWYG-редактор (Vditor в существующем WebEngineView) с двусторонней линковкой блоков и рамками, сохранив raw-вкладку и весь экспорт без изменений.

**Architecture:** Веб-компонент `editor.html`+`editor.js` (вендоренный Vditor, qrc `/editor`) ↔ QML-обёртка `MarkdownEditor.qml` ↔ `AppController`. Push идёт через `runJavaScript` (как в `MarkdownPreview.qml`), pull — через `QWebChannel`-мост `EditorBridge` (первый поток событий из веба в QML). Соответствие box→блок считается в C++ из ranges `renderPageText`.

**Tech Stack:** Qt6/QML, QtWebEngine (существует), QtWebChannel (новый компонент), Vditor dist (вендорится, MIT).

**Spec:** `docs/superpowers/specs/2026-11-08-markdown-wysiwyg-vditor-design.md` — план спорит из него; исполнителю читать оба.

## Global Constraints

- **Коммиты не делает агент** — пользователь коммитит сам; шагов `git commit` в плане нет.
- После каждой задачи зелёный `scripts/check.sh`; после задач, добавляющих файлы (T1, T2, T4, T5), первый прогон — `scripts/check.sh --configure` (новые QML/файлы требуют re-glob).
- Никакой JS-сборки (node/vite) в проекте: только вендоренный dist, коммитится в репо.
- Сеть запрещена: CSP `default-src 'none'` (см. T3), опция `cdn` Vditor = `qrc:/editor/vditor`.
- Слои (ADR 106): `blockBoxMap` → `llocr_parsers`; `EditorBridge` → `llocr_app`; тесты линкуют те же таргеты, никаких ручных копий источников.
- UI-строки — `qsTr` + правка `resources/i18n/llocr_ru.ts`; размеры/шрифты QML-хрома — только `Theme.*`.
- Экспорт не трогается: `Exporter.cpp`, `ExportController.cpp`, `export.js`, `export.html`, ADR 63-путь — вне плана.
- Комментарии — по правилу 7 AGENTS.md (почти никогда).
- Windows собирается только MSVC (ADR 54) — ничего не меняется, WebEngine остаётся.

## Review Focus

- Страница с несколькими крупными кропами (мегабайты base64 в тексте): ввод в WYSIWYG не должен фризить UI — pull троттлится 300 мс в `editor.js`, push дебаунсится 250 мс. Пин: чек-лист T8, пункт 1.
- Формулы `$$…$$`/`$…$`: ввод в соседнем блоке не должен ломать KaTeX-рендер формулы (`mathBlockPreview` включён явно в T3). Пин: чек-лист T8, пункт 2.
- Фоновый rebuild (`resultChanged`) при фокусе в WYSIWYG с незаписанной правкой: refresh откладывается до blur, правка не теряется. Пин: проводка T7, шаг 7; ручной пункт T8, пункт 5.
- Быстрая смена вкладок/страниц во время encode: побеждает последний запрос (generation guard — точная копия паттерна `requestPreviewMarkdown`, AppController.cpp:2398–2433). Пин: структура T6 + ручной пункт T8, пункт 6.
- Пустая страница / страница без текста: пустой документ в редакторе, без краша; `blockBoxMap("", {})` → пустой список. Пин: тест T1, шаг 1.

---

### Task 1: `blockBoxMap` — порядок боксов по верхнеуровневым блокам

**Files:**
- Modify: `src/parsers/BlockRender.h` (декларация рядом с `renderPageText`, IOutputParser.h-инклуд уже есть)
- Modify: `src/parsers/BlockRender.cpp:193-218` (реализация рядом с `renderPageText`)
- Create: `tests/test_block_box_map.cpp`
- Modify: `tests/CMakeLists.txt` (новый таргет по образцу `test_det_parser`, строки 83–98)

**Interfaces:**
- Consumes: `BlockTextRange { int boxIndex; int start; int length; }` (`src/core/OcrResult.h:115-119`), `QList<BlockTextRange>` из `IOutputParser::RebuiltPageText`.
- Produces: `namespace llocr { QVariantList blockBoxMap(const QString &pageText, const QList<BlockTextRange> &ranges); }` — T6 вызывает через `BlockInspector::blockBoxMap()`.

- [ ] **Step 1: Написать падающий тест** `tests/test_block_box_map.cpp` (QTest, `QTEST_GUILESS_MAIN`):

```cpp
// пустая страница → пустой список
QVERIFY(llocr::blockBoxMap(QString(), {}).isEmpty());
// три блока — по боксу: pageText "# T\n\nfirst\n\nsecond",
// ranges {{0,0,4},{1,6,5},{2,13,6}} → map == {0,1,2}
// бокс с несколькими блоками: pageText "one\n\ntwo\n\nthree",
// ranges {{5,0,13}} → map == {5,5,2}
// пользовательский текст вне диапазонов → -1
// ranges пусты (несоответствие после правок) → все -1
```

- [ ] **Step 2: Убедиться, что тест падает** — `ctest --test-dir build -R test_block_box_map` — FAIL (символ не определён).
- [ ] **Step 3: Реализовать `blockBoxMap`** в `src/parsers/BlockRender.cpp`: блоки — сегменты `pageText`, разделённые `"\n\n"` (канон `renderPageText:211`); оффсеты начал блоков накоплением; для каждого начала — диапазон, покрывающий его (`range.start <= blockStart < range.start + range.length`), иначе `-1`. Линейный проход по отсортированным `ranges`.
- [ ] **Step 4: Зарегистрировать таргет** в `tests/CMakeLists.txt` по образцу `test_det_parser` (83–98): `qt_add_executable(test_block_box_map test_block_box_map.cpp)`, линк `llocr_parsers Qt6::Core Qt6::Test`, `add_test(NAME test_block_box_map COMMAND test_block_box_map)` (глобальные `TIMEOUT`/headless применяются каталогом — ADR 99).
- [ ] **Step 5: Тест зелёный** — `cmake --build build -j 8 && ctest --test-dir build -R test_block_box_map` — PASS.

### Task 2: Вендоринг Vditor + qrc `/editor`

**Files:**
- Create: `scripts/fetch-vditor-dist.sh`
- Create (генерируется скриптом): `resources/editor/vditor/**`
- Modify: `THIRD_PARTY_NOTICES.md`
- Modify: `src/CMakeLists.txt:465-470` (рядом с блоком `/preview`)

**Interfaces:**
- Produces: `qrc:/editor/vditor/index.min.js`, `qrc:/editor/vditor/index.css`, подкаталоги `lute/`, `katex/`, `highlight/`, `images/` — T3 использует их как `cdn`-базу Vditor.

- [ ] **Step 1: Написать `scripts/fetch-vditor-dist.sh`**: переменная `VDITOR_VERSION` (пин), скачивание tarball `https://registry.npmjs.org/vditor/-/vditor-$VDITOR_VERSION.tgz` в новый временный каталог, распаковка, копирование в `resources/editor/vditor/` только: `dist/index.min.js`, `dist/index.css`, `dist/lute/`, `dist/katex/`, `dist/highlight/`, `dist/images/`. Отсутствие любого из KEEP-путей = ошибка скрипта (защита от смены раскладки dist между версиями).
- [ ] **Step 2: Определить и запинить версию**: `npm view vditor version` (или GitHub releases) — текущий стабильный 3.x; записать в `VDITOR_VERSION` скрипта.
- [ ] **Step 3: Запустить скрипт**, проверить наличие всех KEEP-путей и `resources/editor/vditor/lute/lute.min.js` (WASM-движок).
- [ ] **Step 4: Зарегистрировать ресурсы** в `src/CMakeLists.txt` после блока `preview` (465–470):

```cmake
file(GLOB_RECURSE EDITOR_FILES
    "${CMAKE_SOURCE_DIR}/resources/editor/*")
list(FILTER EDITOR_FILES EXCLUDE REGEX "\\.DS_Store$")

qt_add_resources(llocr "editor"
    PREFIX "/editor"
    BASE "${CMAKE_SOURCE_DIR}/resources/editor"
    FILES ${EDITOR_FILES}
)
```

- [ ] **Step 5: THIRD_PARTY_NOTICES.md** — секция Vditor (MIT, B3log) + Lute (MIT), версия из пина, путь `resources/editor/vditor`.
- [ ] **Step 6: Сборка зелёная** — `cmake --build build -j 8` (конфигурация перечитает новые ресурсы).

### Task 3: Страница редактора `editor.html` + `editor.js`

**Files:**
- Create: `resources/editor/editor.html`
- Create: `resources/editor/editor.js`

**Interfaces:**
- Consumes: qrc `/editor/vditor/**` (T2), `qrc:///qtwebchannel/qwebchannel.js` (штатный ресурс QtWebChannel).
- Produces (контракт, вызываеемый из QML через `runJavaScript`, T5): `pushPage(markdown, theme, blockMap)` — `markdown` — строка, `theme` = `{dark, bg, fg, hl}` (hex-строки), `blockMap` = массив int; `highlightBlocks(indices)` — массив int. В сторону QML через `bridge` (T4): `notifyPageReady()`, `notifyReferenceChanged(md)`, `notifyTextChanged(md)`, `notifyEditorFocus(bool)`, `notifyBlockClicked(int)`.

- [ ] **Step 1: `editor.html`** — каркас: `<div id="editor"></div>`; скрипты `qrc:/editor/vditor/index.min.js`, `qrc:///qtwebchannel/qwebchannel.js`, `qrc:/editor/editor.js`; стили `qrc:/editor/vditor/index.css` + инлайновый `<style>` (в т.ч. класс подсветки `.ll-hl`). CSP строго:

```
default-src 'none'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; font-src 'self'; connect-src 'self'
```

(если Vditor при отладке падает из-за eval — добавить `'unsafe-eval'` в `script-src`, зафиксировать в ADR почему).

- [ ] **Step 2: `editor.js`** — контракт:
  - Инициализация: `new QWebChannel(qt.webChannelTransport, ch => { bridge = ch.objects.editorBridge; initVditor(); })`; `bridge` глобальный.
  - `new Vditor("editor", { mode: "wysiwyg", height: "100%", cdn: "qrc:/editor/vditor", lang: "en_US", preview: { math: { inlineMath: true } }, toolbar: ["undo","redo","headings","bold","italic","strike","|","list","ordered-list","check","|","quote","inline-code","code","table"], after: () => bridge.notifyPageReady(), input: (v) => schedulePull(), focus: () => bridge.notifyEditorFocus(true), blur: () => bridge.notifyEditorFocus(false) })`.
  - `pushPage(markdown, theme, blockMap)`: тема — `vditor.setTheme(theme.dark ? "dark" : "classic")` + CSS-переменные на body (bg/fg/hl + dark-вариант кода/таблиц — зеркало `resources/preview/preview.js:18-33`); `if (markdown !== lastPushed)` → `lastPushed = markdown; vditor.setValue(markdown);` затем отложенно (`setTimeout(…, 0)`) `reference = vditor.getValue(); bridge.notifyReferenceChanged(reference);`; сохранить `blockMap`, вызвать `retagBlocks()`.
  - `schedulePull()`: троттлинг 300 мс trailing; при срабатывании `const v = vditor.getValue(); if (v !== reference) bridge.notifyTextChanged(v);`
  - `retagBlocks()`: верхнеуровневые дети контента wysiwyg получают `data-ll-block="<ordinal>"` в DOM-порядке; вызывается после каждого `input` (порядок может измениться) и после `pushPage`.
  - `highlightBlocks(indices)`: снять `.ll-hl` с прошлого, навесить на `[data-ll-block]` из списка, первый — `scrollIntoView({ block: "center" })`.
  - Клик: делегированный обработчик на контенте — `event.target.closest("[data-ll-block]")` → `bridge.notifyBlockClicked(+(idx))`.
- [ ] **Step 3: Сборка зелёная** — `cmake --build build -j 8` (ресурсы перечитаются; живой прогон страницы — T7/T8).

### Task 4: Мост `EditorBridge` + регистрация

**Files:**
- Create: `src/app/EditorBridge.h`, `src/app/EditorBridge.cpp`
- Create: `tests/test_editor_bridge.cpp`
- Modify: `CMakeLists.txt:45-66` (компонент `WebChannel`)
- Modify: `src/CMakeLists.txt` (источники `llocr_app`, алфавитно рядом с `ExportController`)
- Modify: `src/main.cpp:88-93` (`setupQmlEngine`)
- Modify: `tests/CMakeLists.txt` (новый таргет)

**Interfaces:**
- Consumes: ничего (чистый QObject).
- Produces: singleton QML `EditorBridge` (имя `editorBridge` для `channel.objects.editorBridge`) с сигналами `pageReady()`, `editorTextChanged(QString)`, `editorReferenceChanged(QString)`, `editorFocusChanged(bool)`, `editorBlockClicked(int)` — T5 подписывается на все пять.

- [ ] **Step 1: Падающий тест** `tests/test_editor_bridge.cpp`: `QSignalSpy` на каждом сигнале; вызов каждого `notify*-слота` с контрольными значениями (`QString("$$x$$")`, `true`, `3`) — emits ровно один сигнал с тем же значением. Линк `llocr_app Qt6::Test`, `add_test`.
- [ ] **Step 2: Убедиться, что тест падает** (класса нет).
- [ ] **Step 3: Реализовать** `EditorBridge` (`.h`/`.cpp`, namespace `llocr`): пять слотов `notifyPageReady()`, `notifyTextChanged(const QString &)`, `notifyReferenceChanged(const QString &)`, `notifyEditorFocus(bool)`, `notifyBlockClicked(int)`, каждый эмитит парный сигнал; конструктор ставит `setObjectName(QStringLiteral("editorBridge"))`.
- [ ] **Step 4: Регистрация**: `CMakeLists.txt` — `WebChannel` в список компонентов `find_package(Qt6 …)`; `src/CMakeLists.txt` — `app/EditorBridge.h`/`.cpp` в `llocr_app`; `main.cpp` — локальный `llocr::EditorBridge editorBridge;` в области `main()` рядом с прочими объектами (образец: `settingsStore`), параметр в `setupQmlEngine`, там же `qmlRegisterSingletonInstance("LLocr", 1, 0, "EditorBridge", &editorBridge);` (образец — строка 90).
- [ ] **Step 5: Тест зелёный** — `ctest --test-dir build -R test_editor_bridge` — PASS (после `check.sh --configure`).

### Task 5: `MarkdownEditor.qml` — обёртка WebEngine

**Files:**
- Create: `src/qml/MainWindow/MarkdownEditor.qml` (глоббится `qt_add_qml_module` — ADR 101; нужен re-configure)

**Interfaces:**
- Consumes: singleton `EditorBridge` (T4), `qrc:/editor/editor.html` (T3).
- Produces (для T7):

```qml
property string markdown        // закодированный вариант (data: URI)
property var blockMap           // массив int: бокс на верхнеуровневый блок
property bool dark
property color bgColor
property color fgColor
property string hl              // hex цвета подсветки выбранного блока
readonly property bool editorFocused
signal editorTextChanged(string markdown)   // уже отфильтровано эталоном
signal blockClicked(int blockIndex)
function highlightBox(boxIndex)
```

- [ ] **Step 1: Каркас** — копия защитного каркаса `MarkdownPreview.qml` (URL-фильтр `qrc:/editor/`, подавление контекст-меню, баннер ошибки загрузки, `_pageReady`), плюс: `import QtWebChannel`; `WebChannel { id: wc; registeredObjects: [EditorBridge] }`; `WebEngineView.webChannel: wc`; `settings.localContentCanAccessFileUrls/RemoteUrls: false`.
- [ ] **Step 2: Push-путь** — дебаунс `Timer` 250 мс на `markdown`/`dark`/`bgColor`/`fgColor`/`hl`/`blockMap` → `_pushPage()`: `runJavaScript("pushPage(" + JSON.stringify(markdown) + ", " + JSON.stringify({dark, bg, fg, hl}) + ", " + JSON.stringify(blockMap) + ")")`. Push дожидается `pageReady` от моста (см. шаг 3), как `_push()` дожидается `_pageReady` в `MarkdownPreview.qml:20-25`.
- [ ] **Step 3: Pull-путь** — `Connections { target: EditorBridge }`: `onPageReady` → `_channelReady = true` + `_pushPage()`; `onEditorReferenceChanged(md)` → `_reference = md`; `onEditorTextChanged(md)` → `if (md !== _reference) root.editorTextChanged(md)` (эталон-гвард §3 спеки); `onEditorBlockClicked(i)` → `root.blockClicked(i)`; `onEditorFocusChanged(f)` → `editorFocused = f`.
- [ ] **Step 4: `highlightBox(boxIndex)`** — индексы из `blockMap`, где значение == `boxIndex`, → `runJavaScript("highlightBlocks(" + JSON.stringify(indices) + ")")`.
- [ ] **Step 5: Верификация** — `scripts/check.sh --configure` (новый QML → re-glob), сборка+qmllint зелёные.

### Task 6: `AppController::requestEditorMarkdown` + `BlockInspector::blockBoxMap`

**Files:**
- Modify: `src/app/BlockInspector.h:69-72` (рядом с `blockTextRange`)
- Modify: `src/app/BlockInspector.cpp`
- Modify: `src/app/AppController.h:416` (рядом с `requestPreviewMarkdown`), сигнал рядом с `previewMarkdownReady` (`AppController.h:368`)
- Modify: `src/app/AppController.cpp:2398-2433` (рядом с `requestPreviewMarkdown`)

**Interfaces:**
- Consumes: `llocr::blockBoxMap(pageText, ranges)` (T1), `currentPageRebuild()` (приватный, `BlockInspector.cpp`).
- Produces:

```cpp
// BlockInspector.h — рядом с blockTextRange():
QVariantList blockBoxMap() const;   // через currentPageRebuild(): text + ranges

// AppController.h:
Q_INVOKABLE void requestEditorMarkdown(const QString &markdown);
// сигнал рядом с previewMarkdownReady:
void editorMarkdownReady(const QString &markdown, const QVariantList &blockMap);
// запись текста, пришедшего из WYSIWYG (ревёрс-подмена data: URI → image://):
Q_INVOKABLE void setCurrentPageTextFromEditor(const QString &text);
```

- [ ] **Step 1: Реализовать `BlockInspector::blockBoxMap()`**: `const auto rb = currentPageRebuild(); return llocr::blockBoxMap(rb.text, rb.ranges);` (rebuild мемоизирован — ADR 208-кэш уже там).
- [ ] **Step 2: Расширить `encodePreviewMarkdown`** (AppController.cpp:2435): выходной параметр `QList<QPair<QString, QString>> *refs = nullptr` (обратная совместимость — `requestPreviewMarkdown` не меняется); в цикле замены при успешной вставке data-URL `refs->append({dataUrl, m.capturedView().toString()})`.
- [ ] **Step 3: Реализовать `requestEditorMarkdown`** — точная копия структуры `requestPreviewMarkdown` (AppController.cpp:2398–2433): тот же generation-guard (`m_previewMarkdownGeneration`), тот же worker через `QPointer`; отличие: карта кропов и блок-карта. Кэш расширяется полями `m_previewCacheRefs` (те же ключи revisions+text): watcher-путь складывает `refs` в кэш и в member `m_editorCropRefs`; cache-hit путь берёт `m_editorCropRefs` из кэша. Блок-карта `const QVariantList map = m_inspector.blockBoxMap();` вычисляется синхронно в момент запроса и ловится в лямбду; оба пути эмитят `editorMarkdownReady(result, map)`.
- [ ] **Step 4: Реализовать `setCurrentPageTextFromEditor`**: строка проходит ревёрс-подмену — каждое известное `dataUrl` из `m_editorCropRefs` заменяется на исходный `![alt](image://ocr/crop/N)`; неизвестные data-URI проходят как есть (§3 спеки); затем вызов существующего `setCurrentPageText(decoded)`.
- [ ] **Step 5: Верификация** — `cmake --build build -j 8 && ctest --test-dir build` — все зелёные (тесты T1 проходят, регрессий нет).

### Task 7: Интеграция в `WorkPanel.qml`

**Files:**
- Modify: `src/qml/MainWindow/WorkPanel.qml` (свойства 14–16, таймер 20–27, Connections 29–34, Revert 79–89, SegmentedControl 111–119, StackLayout 127–130, Connections текстовой области 222–237, Loader 241–245, Connections выбранного бокса 249–259, компонент 261–269)
- Modify: `resources/i18n/llocr_ru.ts` (строки «Preview», ~4100–4105; новые строки редактора)

**Interfaces:**
- Consumes: всё из T5 (`MarkdownEditor`), `Controller.requestEditorMarkdown`/`editorMarkdownReady` (T6).
- Produces: работающая вкладка WYSIWYG.

- [ ] **Step 1: Свойства и переключатель**: `previewMarkdown` → `editorMarkdown`; + `property var editorBlockMap: []`; + `property bool pendingEditorReload: false`; `previewActive` → `wysiwygActive` (все 7 использований: строки 16, 24, 117–118, 130, 242, 253); опция `{ label: qsTr("Preview") }` → `{ label: qsTr("WYSIWYG") }`.
- [ ] **Step 2: Таймер и Connections**: `previewDebounce` → `editorDebounce`, `onTriggered: if (root.wysiwygActive) Controller.requestEditorMarkdown(textArea.text)`; `onPreviewMarkdownReady` → `onEditorMarkdownReady(markdown, blockMap)` → присвоить `editorMarkdown`/`editorBlockMap`.
- [ ] **Step 3: Loader и компонент** (241–245, 261–269): компонент `MarkdownEditor` — свойства из T5 (`markdown: root.editorMarkdown`, `blockMap: root.editorBlockMap`, `dark/bgColor/fgColor` из `Theme`, `hl: Theme.selected`); обработчики: `onEditorTextChanged: (md) => Controller.setCurrentPageTextFromEditor(md)` (ревёрс data-URI — T6, шаг 4); `onBlockClicked: (index) => { if (index >= 0 && index < root.editorBlockMap.length && root.editorBlockMap[index] >= 0) Controller.selectedBoxIndex = root.editorBlockMap[index] }` (id: `editor`).
- [ ] **Step 4: Отложенный refresh** — в Connections к `Controller` у текстовой области (222–237) добавить в `onResultChanged`/`onPageChanged` ветку: `if (root.wysiwygActive) { if (editor.editorFocused) root.pendingEditorReload = true; else editorDebounce.restart(); return }`; `Connections { target: editor; function onEditorFocusedChanged(f) { if (!f && root.pendingEditorReload) { root.pendingEditorReload = false; editorDebounce.restart() } } }`.
- [ ] **Step 5: Подсветка из канваса** — расширить `onSelectedBoxChanged` (249–259): при `root.wysiwygActive` → `editor.highlightBox(Controller.selectedBoxIndex)` и return (raw-путь остаётся для текстового режима).
- [ ] **Step 6: Revert** (79–89): при видимой вкладке WYSIWYG сбросить `pendingEditorReload` и перезапустить `editorDebounce` (правка уже записана в модель; редактор должен перечитать).
- [ ] **Step 7: i18n**: `llocr_ru.ts` — «Preview» → «WYSIWYG», новая строка ошибки загрузки из `MarkdownEditor.qml` («Не удалось загрузить страницу редактора (%1).»).
- [ ] **Step 8: Верификация** — `scripts/check.sh` зелёный; запуск приложения: вкладка WYSIWYG открывается, текст страницы виден, формулы рендерятся.

### Task 8: Гейт WASM + ручная приёмка + документация

**Files:**
- Modify: `docs/07-glossary.md` + `docs/07-glossary.ru.md` (новый ADR — проверить фактический следующий номер после ADR 293)

- [ ] **Step 1: Гейт WASM из qrc** (§2 спеки): запустить приложение, открыть страницу с формулами; если в консоли падают `lute`/`katex` (fetch/mime из qrc) — применить запасной вариант: раскладывать `resources/editor` рядом с бинарём, `editor.html` грузить через `file://`, `settings.localContentCanAccessFileUrls: true`, CSP не менять; зафиксировать выбор и причину в ADR.
- [ ] **Step 2: Ручная приёмка** (чек-лист для пользователя, каждая строка = ожидание):
  1. Страница с несколькими крупными кропами: ввод не фризит UI.
  2. Ввод в блоке рядом с `$$…$$` — формула не ломается; инлайн `$…$` рендерится.
  3. Таблицы (HTML при `output/tablesAsHtml` и текстовые OTSL) видны и редактируются.
  4. Кроп-картинка видна в WYSIWYG; после правки в WYSIWYG raw-вкладка показывает `image://ocr/crop/N`, а не base64 (ревёрс-маппинг).
  5. Клик по блоку в WYSIWYG → рамка выбирается; выбор рамки → подсветка+скролл в WYSIWYG.
  6. Правка в WYSIWYG при прилетевшем фоне — refresh после ухода фокуса, правка не потеряна.
  7. Быстрые переключения вкладок во время загрузки страницы — без устаревшего текста.
  8. Пустая страница — пустой редактор без краша.
  9. «Изменено» появляется только от фактической правки (открытие без правок не ставит флаг); «Отменить правки» возвращает текст; undo/redo внутри редактора.
  10. Тёмная тема — цвета редактора следуют `Theme`.
  11. Raw-вкладка «Текст (MD)» не изменила поведение (tap-to-block, подсветка, pendingReload).
- [ ] **Step 3: ADR** в `docs/07-glossary.md` + ru-зеркало: WYSIWYG-редактор на Vditor в WebEngine (замена вкладки «Предпросмотр» в UI; конвейер превью ADR 22/63 остаётся для экспорта), raw-вкладка сохранена, data-URI в обе стороны, эталон-гвард нормализации, принятие нормализации при правке + по-блочная запись как v2, линковка блоков через `blockBoxMap`.
- [ ] **Step 4: Финальный прогон** — `scripts/check.sh` зелёный; сводка для пользователя, коммит — за ним.
