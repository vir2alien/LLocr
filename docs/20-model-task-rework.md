# Реворк: модели по задачам и конвейер «распознать → проверить → исправить»

> Многоэтапная программа работ (октябрь 2026). Составлена по постановке из
> `docs/todo.md` (блок «Нужно навести порядок в моделях и функционале
> программы»). Каждый этап самодостаточен и рассчитан на отдельную сессию
> агента. Изменения кода между этапами оставляют приложение собираемым и
> рабочим (возможны временные упрощения UI).

## Как работать с этим планом

1. Прочитай `AGENTS.md` (правила, сборка, слои), затем этот файл целиком
   (разделы «Целевая архитектура» и «Принятые решения» — обязательны), затем
   свой этап.
2. Перед началом этапа проверь его **предусловия** по коду, а не по отметкам:
   `git log --oneline` должен показывать коммиты предыдущих этапов.
3. Один этап = одна сессия = один логический кусок. Не делай работу чужих
   этапов «заодно». Коммитит пользователь — по окончании этапа покажи сводку
   изменений и предложи коммит.
4. Проверка после каждого этапа: `scripts/check.sh` (сборка + ctest +
   clang-format + qmllint). Известная особенность: падение ctest прерывает
   скрипт до format/lint — тогда запусти их вручную. Если падают тесты, не
   связанные с твоим этапом, проверь, что они падают и на `main` до твоих
   правок, и сообщи об этом.
5. Правила проекта обязательны: без комментариев в коде, кроме крайних
   случаев; UI-размеры через `Theme.*`; новые окна QML — через `bringToFront()`
   и ленивые `Loader { active: false }`; слои CMake соблюдать.

## Статус этапов

| # | Этап | Зависит от | Статус |
|---|------|-----------|--------|
| 0 | ADR и фиксация решений | — | ✅ |
| 1 | Задачи вместо ролей (профили, ConnectionRole, настройки) | 0 | ✅ |
| 2 | Машина состояний блока (двухслойная верификация) | 1 | ✅ |
| 3 | Конвейер fix: возврат протокола OK/FIX/REVIEW | 2 | ✅ |
| 4 | Статусы страниц | 2 | ✅ |
| 5 | UI: меню блока, тулбары, шапка, доступность | 3, 4 | ✅ |
| 6 | Окно настроек «Модели по задачам» | 1 | ✅ |
| 7 | Единое окно управления моделями, удаление старых окон | 6 | ✅ |
| 8 | Миграции, зачистка legacy, визард | 5, 7 | ✅ |
| 9 | Финал: полная проверка, E2E-чеклист, документация | 0–8 | ☐ |

Ветки после этапа 1 идут параллельно: A = 2 → 3 → 4 → 5 (ядро/конвейер/UI),
B = 6 → 7 (настройки). Файлы веток почти не пересекаются; общая точка —
`SettingsStore`/`RoleProfiles.qml` (правки согласовывать с этапом 1, а не
переписывать друг друга).

---

## Целевая архитектура

### 1. Шесть задач вместо четырёх ролей

Идентификаторы задач (строковые, единые в профилях моделей, `ConnectionRole`,
настройках и QML):

| id задачи | Название в UI | Что делает | Было |
|-----------|---------------|------------|------|
| `layout` | Разметка | Разметка страницы на блоки (без текста) | `layout` |
| `pageOcr` | Распознавание с разметкой | Полностраничное OCR, ответ с layout-токенами → блоки с текстом | `ocr` |
| `rawPageOcr` | Распознавание без разметки | Полностраничное OCR «сырым» текстом, без координат (парсер `raw`) | — (новая) |
| `blockRecognition` | Распознавание блока | OCR кропа одного блока | `blockRecognition` |
| `decision` | Проверка | Вердикт «текст соответствует изображению» (clef-flash, `/v1/systemone`) | `decision` |
| `fix` | Исправление блока | Универсальная модель: изображение + распознанный текст → OK / FIX+текст / REVIEW | — (новая) |

`ConnectionRole` расширяется до шести значений: `PageOcr`, `RawPageOcr`,
`BlockRecognition`, `Decision`, `Layout`, `Fix`. Механика одного сервера
llama.cpp с переключением модели при смене задачи (`RuntimeController::
serverRunsRole`) сохраняется без изменений.

### 2. Матрица «модель → задачи»

Правка девяти профилей в `resources/profiles/models/`:

| Профиль | Задачи | Изменения |
|---------|--------|-----------|
| `unlimited-ocr.json` | pageOcr | `roles.ocr` → `roles.pageOcr` |
| `lfm25-vl-3b.json` | pageOcr, blockRecognition | `roles.ocr` → `roles.pageOcr` |
| `glm-ocr.json` | rawPageOcr, blockRecognition | `roles.ocr` → `roles.rawPageOcr` |
| `teleocr.json` | layout, blockRecognition | роль `ocr` удаляется |
| `clef-flash.json` | decision | без изменений |
| `qwen3.5-4b.json` | fix | `blockRecognition` → `fix`, промпты верификатора из истории |
| `qwen3.5-9b.json` | fix | то же |
| `gemma-4-e4b.json` | fix | то же |
| `gemma-4-12b.json` | fix | то же |

Принцип: **OCR-модели — только OCR, decision — только проверка, универсальные
— только исправление.** Профиль не может совмещать несовместимые задачи.

### 3. Протокол исправления (роль `fix`) — возврат удалённого функционала

Запрос к универсальной модели: `system` = промпт верификатора профиля,
`user` = [промпт типа блока] + [OCR candidate: распознанный (действующий)
текст блока] + [изображение кропа блока]; порядок текст/картинка — по
`promptBeforeImage` роли. Ответ — ровно один из:

- `OK` — правка не требуется → `box.correctedText = box.text` (дубликат);
- `FIX` + текст (со следующей строки или после `:`) → `box.correctedText`
  = новый текст;
- `REVIEW` — изображение повреждено, распознавание невозможно → красный
  статус «повреждён» на слое коррекции, `correctedText` не создаётся;
- пустой `FIX` без текста деградирует до `REVIEW` (ADR 139);
- что-либо иное — ошибка запроса, статус блока не меняется.

Формулировки промптов восстановить из истории (см. «История git» ниже).

### 4. Поведение конвейера (полная спецификация)

Терминология: у блока два текстовых слоя — **текст** (`box.text`, результат
OCR) и **коррекция** (`box.correctedText`, результат универсальной модели).
**Действующий текст** = коррекция, если она есть, иначе текст. У каждого слоя
своя проверка: `checkStatus` (+`confidence`) для текста,
`correctedCheckStatus` (+`correctedConfidence`) для коррекции.

1. Результат любой OCR-модели (страница, блок, повторное распознавание)
   всегда кладётся в `box.text`. Повторное распознавание блока **стирает**:
   `correctedText`, оба статуса проверки и обе confidence; статус блока
   возвращается к «распознано» (серый).
2. Результат универсальной модели кладётся в `box.correctedText`
   (см. протокол выше). Исправление сбрасывает проверку слоя коррекции.
   Статус проверки текста при этом **сохраняется** (он относится к тексту).
3. Проверка (decision) всегда судит **действующий** текст: коррекцию, если
   она есть, иначе текст. При старте проверки (и повторной) старый вердикт
   соответствующего слоя стирается; новый вердикт (Ok — зелёный,
   Mismatch — красный) пишется в этот слой.
4. Отмена исправления («Отменить исправление» в панели блока): удаляется
   `correctedText`, проверка коррекции и её результат; блок откатывается к
   статусу текста — «распознано» (серый) или «проверено» (зелёный/красный,
   если текст проверялся).
5. Автопроверка после распознавания (`check/autoCheck`) сохраняется.
   Автоисправление: если включено (`check/autoFix`, бывший `check/autoRecheck`),
   Mismatch-блоки после проверки автоматически идут в «Исправить»
   (универсальная модель).
6. Автоматического продвижения `correctedText` → `text` **нет**. Экспорт,
   пересборка текста страницы и Markdown-превью используют действующий текст.

### 5. Статусы блока (точка слева от рамки)

| Статус | Условие | Цвет точки |
|--------|---------|-----------|
| распознано | есть `box.text`, нет вердикта на действующем слое | серый |
| проверено | на действующем слое вердикт Ok / Mismatch | зелёный / красный |
| повреждён | вердикт Review | красный |
| исправлено | есть `box.correctedText`, вердикта коррекции нет | серый |

Точка всегда отражает **действующий слой**: если есть коррекция — статус
коррекции, иначе статус текста. Янтарный цвет (бывший `ReRecognized`)
убирается. Значение `ReRecognized = 2` из перечисления удаляется, числовая
дырка 2 сохраняется ради чтения старых проектов (см. этап 2). Бейдж
confidence (`check/showConfidence`) — на действующем слое.

### 6. Статусы страницы

Две независимые величины:

- **Режим распознавания** (хранится): нет / только разметка / распознано без
  разметки / распознано с разметкой. «Только разметка» → «с разметкой»
  автоматически, когда все нетекстовые-не-image блоки страницы получили текст.
- **Агрегат проверки** (вычисляется): не проверено / есть ошибки / нет
  ошибок. «Проверено» — только когда каждый блок страницы, подлежащий
  проверке (тип включён в настройках автопроверки **или** подозрение на
  дубль), имеет вердикт на действующем слое. `Review` считается ошибкой.
  Если подлежащих блоков нет — «не проверено».

Отображение (миниатюры, подпись страницы): если агрегат полон — он, иначе
режим распознавания. Существующие маркеры отредактировано/дубликат
сохраняются.

### 7. Интерфейс

**Всплывающее меню блока** (`BlockMiniToolbar`): Распознать / Проверить /
Исправить / Удалить. «Проверить» и «Исправить» недоступны, если у блока нет
распознанного текста (действующий текст пуст).

**Шапка** (книжные действия):

- «Распознать» — split-кнопка: основной клик = все страницы **с разметкой**
  (pageOcr); в меню — «Все страницы без разметки» (rawPageOcr).
- «Проверить» — проверка всех подлежащих блоков документа; при включённой
  `check/autoFix` Mismatch-блоки автоматически исправляются.
- «Исправить» — все блоки документа со статусами Mismatch и Review идут в
  конвейер `fix`.
- «Разметить», «Экспортировать», Stop, навигация — без изменений.

**Панель страницы** (2×2 сетка):

- «Распознать страницу» — split: основной клик = с разметкой; меню: «Без
  разметки», разделитель, «Проблемные блоки» (перезапустить распознавание
  Mismatch/Review блоков), «Все блоки» (все безтекстовые блоки разметки).
- «Проверить ▾» — основной клик = проверить и исправить (принудительно);
  меню — «Только проверить».
- «Разметить страницу», «Сохранить страницу» — без изменений.

**Доступность**: пункты меню и кнопки, требующие модель задачи, неактивны,
если модель задачи не выбрана/не скачана (managed: файла нет; external: имя
модели пустое).

### 8. Настройки

**Новое окно «Модели по задачам»**: шесть строк `Задача — Модель
(выпадающий список) — Квант (выпадающий список)`; под таблицей — вкладки
Launch/Request выбранной задачи (переиспользовать существующие
`LaunchTab`/`RequestTab`). При сохранении проверяется, что все выбранные
кванты скачаны; если нет — диалог со списком и предложением скачать; по
согласию строится последовательная очередь загрузки (по завершении каждой —
модель назначается на задачу). Для external-режима вместо квантов — поле
имени модели на задачу.

**Новое окно управления моделями**: единый список всех моделей (профилей)
приложения — скачанные и доступные к скачиванию, с действиями скачать /
удалить / открыть папку. Назначение задач здесь не выполняется (это в окне
задач). Четыре существующих окна «OCR model / Decision model / Block OCR
model / Layout model» и их пункты меню удаляются.

**Ключи настроек**: единый неймспейс `tasks/<task>/{modelPath, mmprojPath,
draftPath, requestProfileId, modelName}` с одноразовой миграцией из
`launch/*`, `check/*` (модельные ключи), `decision/*`, `layout/*`. Группа
`check/` остаётся для поведенческих настроек: `check/autoCheck`,
`check/autoFix` (быв. `autoRecheck`), `check/showConfidence`;
`decision/matchThreshold` остаётся.

---

## Принятые решения (зафиксировано; менять — только через правку этого файла и ADR)

1. **Launch/Request живут в окне «Модели по задачам»** (вкладки под
   выбранной задачей) — отдельных окон параметров не остаётся.
2. **Способ распознавания страницы выбирается split-кнопкой** «Распознать»
   (основное — с разметкой), без отдельной настройки.
3. **Автоэтап после проверки = автоисправление универсальной моделью**
   (`check/autoFix`), а не перераспознавание blockRecognition. «Проверить ▾»:
   основной клик — проверить и исправить, меню — только проверить.
4. **Книжное «Исправить» = все блоки Mismatch и Review** документа.
5. REVIEW от универсальной модели: красный статус на слое коррекции,
   `correctedText` при этом отсутствует (или очищается, если был).
6. `ReRecognized` удаляется из `BoxCheckStatus` **без перенумерации**
   остальных значений: старые проекты со значением 2 читаются как NotChecked
   (corrupted слой), `correctedText` из старых проектов сохраняется как слой
   коррекции.
7. Продвижения коррекции в текст нет; экспорт/пересборка текста — всегда
   действующий текст (аудит всех `IOutputParser::rebuildText`, не только
   `RawParser`).
8. Размеченная страница («только разметка») продолжает считаться
   `recognized = true` — пакетное распознавание пропускает её, как сейчас.
9. `decision/matchThreshold`, v5-схема запроса clef-flash, критерии
   верификации — без изменений (ADR 160/161).
10. Настройки визарда (первый запуск) переводятся на новые ключи механически;
    «автоматический подбор моделей в визарде» — вне скоупа этого реворка
    (отдельный пункт todo).

---

## Карта текущего кода (отправные точки)

| Что | Где |
|-----|-----|
| `ConnectionRole` | `src/core/ConnectionRole.h` |
| Профили моделей, схема `roles` | `src/core/ModelProfiles.{h,cpp}`, `resources/profiles/models/*.json` |
| Хранилище настроек, диспетчер роль→ключи | `src/config/SettingsStore.{h,cpp}` (`launchModelPathForRole`, `applyStartupMigration`) |
| Запуск сервера, смена модели между задачами | `src/runtime/RuntimeController.cpp` (`ensureConnectionReady`, `serverRunsRole`, `roleConfigError`), `src/runtime/ServerLaunchConfig.cpp` |
| Конфигурация запуска | `src/runtime/LaunchProfileStore.{h,cpp}` (`modelIdForRole`) |
| Профили запросов | `src/config/RequestProfileStore.{h,cpp}` (синглтоны в `src/main.cpp`) |
| Установщик моделей, кванты | `src/runtime/ModelInstaller.{h,cpp}`, `src/runtime/ModelQuantModel.cpp`, `src/runtime/ModelInstallTransaction.cpp`, `src/runtime/ModelRegistry.{h,cpp}` |
| Модель блока, статусы | `src/core/OcrResult.h` (`BoundingBox`, `BoxCheckStatus`), `src/core/CheckResult.h` |
| Применение результатов к блоку | `src/app/AppController.cpp`: `applyCheckResultToBox`, `revertBlockCorrection`, `applyRawResult`, `applyLayout` |
| Очередь проверки (decision → recheck) | `src/app/VerificationQueueController.{h,cpp}` |
| Decision-клиент (clef-flash) | `src/app/DecisionController.{h,cpp}`, `src/models/DecisionModel.cpp` |
| Block-OCR клиент | `src/app/CheckController.{h,cpp}`, `src/models/GeneralPurposeModel.cpp` |
| Layout | `src/app/LayoutController.{h,cpp}` |
| Список блоков для QML | `src/app/BoxListModel.{h,cpp}` |
| Модель страниц/миниатюр | `src/app/DocumentModel.{h,cpp}`, `src/app/PageListModel.{h,cpp}` |
| Проект `.llocr` | `src/app/ProjectStore.{h,cpp}` (kSchemaVersion) |
| QML шапка/тулбары/блок | `resources/qml/MainWindow/{Header,PageToolBar,BlockMiniToolbar,BlockEditPanel,ImagePreview,ThumbDelegate}.qml` |
| QML настройки | `resources/qml/Settings/ModelSettingsWindow.qml` (+`LocationTab/LaunchTab/RequestTab`), `Main.qml` (ленивые окна) |
| QML диспетчер ролей | `resources/qml/RoleProfiles.qml` |
| Настройки проверки (типы блоков) | `src/app/VerificationPromptStore.{h,cpp}`, `resources/qml/Settings/VerificationBlocksTab.qml` |

## История git (восстановление удалённого функционала fix)

Протокол OK/FIX/REVIEW существовал с 2026-09-21 и удалён 2026-10-09:

- `bdc62a5` — «add second llm support phase 1»: `CheckController`,
  `GeneralPurposeModel`, запрос image+text без протокола.
- `2c37a1a` — «phase 9»: полный протокол OK/FIX/REVIEW + общий системный
  промпт верификатора в `resources/profiles/verifyPrompts.json`.
- `6de1448` — «check promts moved to profiles»: **последняя полная версия**;
  промпты в профиле модели (`roles.check`), построитель запроса с
  OCR-candidate и парсер ответа в `GeneralPurposeModel`.
- `6513786` — «step 4 decision check»: парсер протокола удалён (замена на
  decision-конвейер, ADR 145).
- `5e0a8bb` — «block ocr updated»: `recognizedText` убран и из запроса.

Восстановление текстов: `git show 6de1448:src/models/GeneralPurposeModel.cpp`
и `git show 6de1448:resources/profiles/models/qwen3.5-4b.json` (роль
`check` того времени). Системный промпт протокола (из `2c37a1a`): «You are an
OCR post-processing assistant. You receive an image crop of a document block
and the OCR candidate that was recognized from it. Compare the candidate
against the visible text in the image and answer with exactly one of three
tokens: OK — ... FIX\n<corrected block> — ... REVIEW — the image is
unreadable...» — при переносе адаптировать под текущий стиль профилей
(списки правил, ADR 154), не копировать вслепую.

---

# Этап 0. ADR и фиксация решений

**Цель.** Записать архитектурные решения, чтобы дальнейшие этапы ссылались
на номера ADR, а не на этот файл.

**Предусловия.** Принят настоящий план.

**Файлы.** `docs/07-glossary.md`, `AGENTS.md` (карта документов: строка про
`docs/20-model-task-rework.md`; строка Current status — добавить абзац о
стартовавшем реворке с указанием на план).

**Шаги.**

1. Добавить в `docs/07-glossary.md` (после ADR 161):
   - **ADR 162** — система «модели по задачам»: шесть задач (`layout`,
     `pageOcr`, `rawPageOcr`, `blockRecognition`, `decision`, `fix`),
     профиль объявляет задачи, настройки `tasks/<task>/*`, split-кнопки
     распознавания. Заменяет часть ADR 149/150 в части «четвёртая роль» —
     теперь задач шесть.
   - **ADR 163** — возврат протокола OK/FIX/REVIEW для универсальных моделей
     (роль `fix`): запрос image + OCR candidate + промпт; суперседует
     recheck-часть ADR 145 и ADR 152 (повторное распознавание больше не
     шлёт прежний текст в blockRecognition — теперь это прерогатива fix).
     Ссылки на коммиты-источники.
   - **ADR 164** — двухслойная верификация блока: `checkStatus` для
     `box.text`, `correctedCheckStatus` для `box.correctedText`; OCR всегда
     пишет в `box.text` и стирает коррекцию; точка — действующий слой;
     значение `ReRecognized` удалено без перенумерации.
   - **ADR 165** — статусы страниц: режим распознавания + вычисляемый
     агрегат проверки (полный набор подлежащих блоков).
   - **ADR 166** — окна настроек: «Модели по задачам» (задача–модель–квант +
     Launch/Request) и единое окно управления моделями; четыре окна
     «модель по типу» удаляются.
2. Обновить карту документов и Current status в `AGENTS.md`.

**Критерии готовности.** ADR читаются связно, номера не конфликтуют;
`AGENTS.md` упоминает план.

---

# Этап 1. Задачи вместо ролей: профили, ConnectionRole, настройки

**Цель.** Ввести шесть задач на всех уровнях данных (профили, enum, ключи
настроек, хранилища) с миграцией; UI пока работает на новых ключах через
существующие окна (четыре окна продолжают показывать свои задачи).

**Предусловия.** Этап 0 закоммичен.

**Файлы.** `src/core/ConnectionRole.h/.cpp` (или инлайн), `src/core/ModelProfiles.{h,cpp}`,
девять JSON в `resources/profiles/models/`, `src/config/SettingsStore.{h,cpp}`,
`src/config/RequestProfileStore.cpp`, `src/main.cpp`, `src/runtime/RuntimeController.cpp`
(тексты `roleConfigError`), `src/runtime/LaunchProfileStore.cpp`,
`src/runtime/ServerLaunchConfig.cpp`, `src/runtime/ModelInstaller.{h,cpp}`,
`src/runtime/ModelQuantModel.cpp`, `resources/qml/RoleProfiles.qml`,
`resources/qml/Main.qml` (синглтоны), `resources/qml/Setup/*` (шаги Model/
Launch), `resources/qml/MainWindow/Header.qml` (заголовки окон — сверить),
тесты: `tests/test_model_preset_catalog.cpp`, `tests/test_ocr_models.cpp`,
`tests/test_settings_store.cpp`, `tests/test_launch_config.cpp`,
`tests/test_model_installer.cpp`.

**Шаги.**

1. **`ConnectionRole`**: значения `PageOcr, RawPageOcr, BlockRecognition,
   Decision, Layout, Fix`; `connectionRoleName()`/`connectionRoleFromName()`
   — новые строки; `"ocr"` из старых настроек маппится в `PageOcr`
   (неизвестное — как сейчас, `PageOcr`). Обойти все `switch` по роли
   (grep `ConnectionRole::`) и дописать ветки.
2. **`ModelProfiles`**: идентификаторы задач те же строки; `forRole`,
   `defaultIdForRole`, `idsForRole` работают с `pageOcr`/`rawPageOcr`/`fix`.
   Псевдонимы/дефолты: у `rawPageOcr` дефолт — `glm-ocr`, у `fix` —
   `qwen3.5-4b`, у `pageOcr` — как сейчас у `ocr` (`unlimited-ocr`).
3. **Профили JSON** — по матрице (§2 целевой архитектуры). Роль `fix` у
   универсальных моделей: перенести текущие `systemPrompt`, `blockPrompts`,
   `launch`, `request`, `promptBeforeImage` из `blockRecognition`, затем
   заменить формулировки на протокол OK/FIX/REVIEW (из `6de1448`,
   системный промпт из истории — см. выше; правила ADR 154: списки правил,
   без criteria, «exactly» сохранить). Удалить `recheckPrompt` из fix-ролей
   (не нужен: текст кандидата идёт отдельной частью запроса).
4. **`SettingsStore`**: ключи `tasks/<task>/modelPath|mmprojPath|draftPath|
   requestProfileId|modelName`; аксессоры `launchModelPathForRole(role)` и
   соседи переписать на новый неймспейс (switch по шести задачам).
   `applyStartupMigration()`: перенос `launch/*` → `tasks/pageOcr/*`,
   `check/modelPath|mmprojPath|draftPath|requestProfileId|modelName` →
   `tasks/blockRecognition/*`, `decision/*`-модельные → `tasks/decision/*`,
   `layout/*`-модельные → `tasks/layout/*`; `check/autoRecheck` →
   `check/autoFix` (значение сохранить); новые задачи `rawPageOcr`, `fix`
   получают дефолты из профилей; retired-ключи удалить (по образцу
   существующих записей в `defaultTable`/миграции).
5. **`RequestProfileStore`** ×6: имена файлов `requestPage.json`,
   `requestRawPage.json`, `requestBlock.json`, `requestDecision.json`,
   `requestLayout.json`, `requestFix.json`; при старте переименовать/прочитать
   старые `request.json`, `requestValidate.json` (миграция). Синглтоны QML в
   `main.cpp`: `RequestProfilesPageOcr/RawPageOcr/Block/Decision/Layout/Fix`
   (старые имена `RequestProfilesOcr/Validate` убрать вместе с упоминаниями
   в QML — grep).
6. **`RoleProfiles.qml`**: диспетчер задача→{title, quantModels,
   requestProfileId, setRequestProfileId} на шесть задач. Заголовки:
   «Разметка», «Распознавание с разметкой», «Распознавание без разметки»,
   «Распознавание блока», «Проверка», «Исправление блока» (qsTr + ru).
7. **`ModelInstaller`/`ModelQuantModel`**: списки моделей строятся по задаче
   (`matchesRole` — тег `entry.roles`/профиль); старые четыре свойства
   (`quantModels`, `checkQuantModels`, `decisionQuantModels`,
   `layoutQuantModels`) переносятся на `pageOcr`, `blockRecognition`,
   `decision`, `layout` (окна до этапа 7 показывают эти четыре; `rawPageOcr`
   и `fix` списки добавляются как свойства для будущего окна задач).
8. **Визард**: шаги Model/Launch пишут в `tasks/pageOcr/*` (ключи меняются
   под капотом — QML-свойства `Settings.*` сохраняют имена, проверить
   биндинги).
9. **`RuntimeController`**: `roleConfigError` — тексты на шесть задач
   («pick a model in Settings → Models for the <task> task» — формулировки
   согласовать с новым окном задач; до этапа 7 допустима ссылка на
   существующие окна). `configValid` не менять (гейт старта — основная
   задача, ADR 61).

**Критерии готовности.** `scripts/check.sh` зелёный (обновлены тесты:
пресет-каталог ожидает новую матрицу ролей; `test_ocr_models::
everyModelDeclaresARegisteredParser` проходит; тест настроек покрывает
миграцию старых ключей). Приложение запускается: существующий выбор моделей
сохраняется после миграции (проверить вручную: выбрать модели, перезапустить,
убедиться что настройки не потерялись), визард проходит.

---

# Этап 2. Машина состояний блока: двухслойная верификация

**Цель.** Развести проверку текста и коррекции; OCR-результаты всегда в
`box.text`; новая семантика стирания состояний; схема проекта v2.

**Предусловия.** Этап 1 закоммичен.

**Файлы.** `src/core/OcrResult.h`, новый `src/core/BoxTransitions.{h,cpp}`
(чистые переходы без Qt-зависимостей сверх QString), `src/core/CheckResult.h`,
`src/app/AppController.{h,cpp}` (`applyCheckResultToBox`,
`revertBlockCorrection`, `recognizeSelectedBlock`-пути), `src/app/BoxListModel.{h,cpp}`,
`src/app/ProjectStore.{h,cpp}`, `src/parsers/*` (`rebuildText` — аудит),
`resources/qml/MainWindow/ImagePreview.qml`, `BlockEditPanel.qml`,
тесты: новый `tests/test_box_transitions.cpp`, `tests/test_project_store.cpp`,
`tests/test_pagemodel.cpp`, `tests/CMakeLists.txt`.

**Шаги.**

1. **`OcrResult.h`**:
   - `BoxCheckStatus`: удалить `ReRecognized`; значения остаются
     `NotChecked=0, Ok=1, (2 свободно), Review=3, Mismatch=4`; комментарий
     про зарезервированную двойку (старые проекты).
   - `BoundingBox`: добавить `BoxCheckStatus correctedCheckStatus =
     BoxCheckStatus::NotChecked;` и `double correctedConfidence = -1.0;`.
     Комментарии полей обновить (`correctedText` — слой исправления
     универсальной модели).
2. **`CheckResult.h`**: транспортный `CheckStatus` отвязать от
   `BoxCheckStatus`: `Failed=0, Ok, Recognized, Fixed, Review, Mismatch`
   (не персистится). Ожидается из: blockRecognition → `Recognized` (+текст),
   fix → `Ok`/`Fixed`(+текст)/`Review`, decision → `Ok`/`Mismatch`
   (+вероятность).
3. **`BoxTransitions`** (чистые функции над `BoundingBox&`):
   - `ocrResultApplied(box, text)` — текст в `box.text`; `correctedText`,
     оба статуса, обе confidence стираются.
   - `verificationStarted(box)` — вердикт действующего слоя стирается
     (статус → NotChecked, confidence → −1).
   - `verdictApplied(box, result)` — вердикт (+confidence) пишется в
     действующий слой.
   - `fixApplied(box, text)` — `correctedText = text`, проверка коррекции
     стирается.
   - `fixConfirmedOriginal(box)` — `correctedText = box.text` (дубликат),
     проверка коррекции стирается.
   - `fixMarkedUnreadable(box)` — `correctedText` очищается,
     `correctedCheckStatus = Review`.
   - `correctionReverted(box)` — `correctedText` очищается, проверка
     коррекции стирается.
   - Хелперы: `effectiveText(box)`, `effectiveCheckStatus(box)`,
     `effectiveConfidence(box)`.
4. **`AppController`**: переписать применение результатов на переходы.
   Развести сигналы `VerificationQueueController` по источникам
   (`blockRecognized`, `blockVerified`, `blockFixed` — вместо одного
   `blockChecked`) — очередь этапа 3 дорисует fix, сейчас появляется
   `blockRecognized` для blockRecognition. Повторное распознавание блока =
   `ocrResultApplied` (текст перезаписывается, а не дублируется в
   `correctedText`, как сейчас). После каждого перехода — существующие
   процедуры: `rebuildPageText`, rebase `m_editStore`, `markPageEdited`,
   `m_boxModel.updateBoxCheck`.
5. **`BoxListModel`**: роли `boxCheckStatus`, `boxCorrectedText`,
   `boxCorrectedCheckStatus`, `boxConfidence`, `boxCorrectedConfidence`;
   `updateBoxCheck` обновляет оба слоя. Зеркальные свойства выбранного блока
   в `AppController` (`selectedBlockCorrected` и т.п.) дополнить статусом
   коррекции.
6. **QML**: `ImagePreview.qml` — точка по действующему слою (QML-функция или
   роль-обёртка): `boxCorrectedText.length > 0 ? boxCorrectedCheckStatus :
   boxCheckStatus`; янтарный убрать; бейдж confidence — по действующему слою.
   `BlockEditPanel.qml` — секция коррекции: точка статуса коррекции, кнопка
   «Отменить исправление» видна при наличии `correctedText` (семантика —
   `correctionReverted`).
7. **`rebuildText`-аудит**: все парсеры (`DetTokenParserBase`, `Lfm25VlParser`,
   `UnlimitedOcrParser`, `TeleOcrLayoutParser`, `RawParser`) собирают текст
   страницы из действующего текста блока (RawParser уже так — распространить).
8. **`ProjectStore`**: `kSchemaVersion = 2`; `boxToJson` пишет
   `correctedCheckStatus`; чтение v1: `checkStatus == 2` → оба слоя
   NotChecked (corruptedText сохраняется как коррекция), прочие значения
   `checkStatus` переносятся в текст-слой как есть; v2 читает оба поля.
   Запись режима распознавания страницы — этап 4.

**Критерии готовности.** Новый `test_box_transitions` покрывает все переходы
и правила §4–5 (включая: OCR стирает коррекцию; отмена коррекции возвращает
вердикт текста; проверка стирает старый вердикт слоя). `test_project_store`:
roundtrip v2 + загрузка v1-фикстуры со статусом 2. `test_pagemodel` — новые
роли. Вручную: повторное распознавание блока перезаписывает текст и
обнуляет статусы; отмена исправления откатывает точку.

---

# Этап 3. Конвейер fix: возврат протокола OK/FIX/REVIEW

**Цель.** Универсальные модели исправляют блоки; автоисправление после
проверки; книжное и блочное «Исправить».

**Предусловия.** Этап 2 закоммичен (переходы `fixApplied`/
`fixConfirmedOriginal`/`fixMarkedUnreadable` существуют).

**Файлы.** `src/app/FixController.{h,cpp}` (новый, по образцу
`CheckController`), `src/app/VerificationQueueController.{h,cpp}`,
`src/app/AppController.{h,cpp}`, `src/models/GeneralPurposeModel.{h,cpp}`,
`src/core/CheckRequest.h` (вернуть `recognizedText`),
`resources/qml/MainWindow/{BlockMiniToolbar,Header,PageToolBar,Footer}.qml`,
`resources/qml/Settings/VerificationBlocksTab.qml` (autoFix),
`resources/i18n/llocr_ru.ts`, `tests/` (новый `test_fix_protocol` или
расширение ближайшего слоя `llocr_models`), `tests/CMakeLists.txt`.

**Шаги.**

1. **`GeneralPurposeModel`**: режим fix в `buildRequestBody` — восстановить
   по `git show 6de1448:src/models/GeneralPurposeModel.cpp`:
   OCR-candidate оборачивается в `<ocr_candidate>…</ocr_candidate>`; порядок
   частей по `promptBeforeImage` роли fix; system = `systemPromptFor(modelId,
   "fix")`. `parseResponse`: OK → `CheckStatus::Ok`; REVIEW → `Review`; FIX →
   текст (снять ведущий `:`/перенос); пустой FIX → `Review`; иначе — ошибка
   «expected OK, FIX or REVIEW». Срезание thinking/code-fence — как сейчас.
   blockRecognition-режим остаётся «ответ = текст блока» (→ `Recognized`).
2. **`FixController`**: `fixBlock(image, recognizedText, systemPrompt,
   typePrompt)` → `ModelRequestController::beginRequest(ConnectionRole::Fix…)`
   с request-профилем задачи fix.
3. **`VerificationQueueController`**: фаза `Recheck` → `Fix`: Mismatch-блоки
   идут в `FixController` (входной текст — действующий текст блока);
   `RecheckMode` сохраняет смысл (skip/force/fromSetting), настройка теперь
   `check/autoFix`. Промпт: `ModelProfiles::blockPromptFor(modelId, "fix",
   label)` + system; кандидат передаётся структурно (см. п.1), не вставкой
   в user-промпт.
4. **Точки входа**: `fixSelectedBlock()` (мини-тулбар блока), книжное
   «Исправить» — очередь по всем блокам Mismatch/Review документа;
   «Проверить ▾» — основной клик = decision + fix принудительно, меню =
   только проверить. Автопроверка `check/autoCheck` после распознавания
   сохраняется; при `check/autoFix` Mismatch автоматически уходит в fix.
5. **Футер**: статус «Исправление %1 / %2» для фазы fix (по образцу
   существующих строк фаз).
6. **UI-строки**: «Исправить», «Исправление блока», «Только проверить»,
   «Проверить и исправить», «Автоматическое исправление после проверки» —
   qsTr + русский перевод в `llocr_ru.ts`.

**Критерии готовности.** Юнит-тест парсера: OK / FIX-с-двоеточием /
FIX-с-переносом / пустой FIX→Review / REVIEW / мусор→ошибка / code-fence
вокруг FIX. Тест или ручная проверка через `mock_llama_server`: mismatch →
автоfix → correctedText = новый текст, точка серая; OK → correctedText =
дубликат; REVIEW → красный без correctedText. Книжное «Исправить» обходит
все Mismatch/Review. `scripts/check.sh` зелёный.

---

# Этап 4. Статусы страниц

**Цель.** Режим распознавания + агрегат проверки; отображение в
миниатюрах и подписи страницы.

**Предусловия.** Этап 2 закоммичен (этап 3 не обязателен, но агрегат
осмысленен после него — если ветки идут параллельно, агрегат можно писать
по `checkStatus`-полям этапа 2).

**Файлы.** `src/core/OcrResult.h` или `src/app/DocumentModel.h` (enum
`PageRecognitionMode`), `src/app/DocumentModel.{h,cpp}`, `src/app/PageListModel.{h,cpp}`,
`src/app/AppController.cpp` (`applyRawResult`, `applyLayout`, применение
block-результата), `src/app/ProjectStore.{h,cpp}`, `resources/qml/MainWindow/ThumbDelegate.qml`,
`resources/qml/MainWindow/WorkPanel.qml` (подпись страницы), `resources/i18n/llocr_ru.ts`,
тесты: `tests/test_pagemodel.cpp` (или новый `test_page_status`).

**Шаги.**

1. `enum class PageRecognitionMode { None=0, MarkupOnly=1, RawOcr=2,
   MarkupOcr=3 }`; поле в `DocumentPage` (persist). `bool recognized`
   сохраняется (совместимость, скип батча).
2. Установление режима: `applyRawResult` — знает задачу
   (RawPageOcr → RawOcr, PageOcr → MarkupOcr); `applyLayout` → MarkupOnly
   (существующее поведение recognized=true сохраняется); переход
   MarkupOnly → MarkupOcr — после каждого blockRecognition-результата:
   все блоки, кроме image-плейсхолдеров (`BlockStyle::ImagePlaceholder`),
   имеют непустой `box.text`.
3. Агрегат (чистая функция, например `PageStatusCalc` в DocumentModel или
   core): вход — страница + `isTypeEnabled` + duplicateSuspect; выход
   `Incomplete | HasErrors | Clean`. Подлежащие = включённые типы ∪
   подозрительные; «проверен» = вердикт (не NotChecked) на действующем слое;
   Review/Mismatch → HasErrors; пустой список подлежащих → Incomplete.
   Пересчёт: на каждый `updateBoxCheck`/изменение набора блоков.
4. `PageListModel`: роли `pageRecognitionMode`, `pageVerification`.
   `ThumbDelegate`: отображение по §6 (агрегат полон → зелёный/красный
   маркер + подпись «Проверено (есть ошибки / без ошибок)»; иначе режим:
   «Только разметка» / «Распознано (без разметки)» / «Распознано»).
   Существующие маркеры edited/дубликат не трогать.
5. `ProjectStore`: v2-поле `recognitionMode` (для v1-проектов: `recognized &&
   boxes непустые → MarkupOcr`, `recognized → RawOcr`, иначе None).
6. Пересчёт при загрузке проекта.

**Критерии готовности.** Тесты агрегата: полная проверка → Clean; один
Mismatch → HasErrors; неотмеченный тип не блокирует; duplicateSuspect
обязателен; коррекция с вердиктом считается. Ручная проверка: разметить →
«Только разметка»; заполнить блоки → «Распознано»; проверить всё →
«Проверено». `scripts/check.sh` зелёный.

---

# Этап 5. UI блоков, тулбары, шапка, доступность

**Цель.** Привести управляющие элементы к целевому интерфейсу (§7) и
завязать доступность на наличие моделей задач.

**Предусловия.** Этапы 3 и 4 закоммичены.

**Файлы.** `resources/qml/MainWindow/{BlockMiniToolbar,BlockEditPanel,
PageToolBar,Header,ImagePreview}.qml`, `src/app/AppController.{h,cpp}`
(availability), `src/runtime/RuntimeController.{h,cpp}` (наличие модели
задачи), `resources/i18n/llocr_ru.ts`; иконки при необходимости —
`resources/icons/ui/` (+ ресурс `ui_icons` в CMake).

**Шаги.**

1. **Мини-тулбар блока**: Распознать (scan-text) / Проверить (circle-check)
   / Исправить (подобрать Lucide-иконку, напр. `wand-sparkles` или
   `pencil-line`; добавить SVG в `resources/icons/ui/`) / Удалить (trash-2).
   Включённость: «Проверить» и «Исправить» — `effectiveText.length > 0` и
   соответствующий availability-флаг; «Распознать» — как сейчас для
   текстовых блоков.
2. **`BlockEditPanel`**: статусы по слоям (точка текста, точка коррекции при
   наличии), подписи «Распознано / Проверено / Исправлено / Повреждено»;
   «Отменить исправление» — по спецификации отката (этап 2).
3. **`PageToolBar`**: «Распознать страницу» — split (основной = с разметкой;
   меню: «Без разметки», разделитель, «Проблемные блоки», «Все блоки»);
   «Проверить ▾» — основной = проверить и исправить, меню = «Только
   проверить»; rest без изменений. Методы `AppController`:
   `recognizeCurrentPage(ConnectionRole)` / `recognizeAllPages(role)` —
   параметризовать задачей (сейчас `recognizeCurrent`/`recognizeAll`).
4. **`Header`**: «Распознать» split (все с разметкой / все без разметки);
   «Исправить» → fix-очередь (этап 3); «Проверить» — как есть.
5. **Доступность**: расширить `AvailabilityFlags`/`updateAvailability()`
   флагами наличия модели на задачу: managed — файл
   `tasks/<task>/modelPath` существует на диске; external —
   `tasks/<task>/modelName` непусто. Источники истины — `RuntimeController`
   (по образцу `layoutAvailable`, но для шести задач; сигнал при смене
   настроек/реестра). Привязать: Распознать(с/без) — pageOcr/rawPageOcr;
   Разметить — layout; Проверить — decision; Исправить — fix; блочные
   кнопки — blockRecognition/decision/fix соответственно. `configValid`
   (гейт старта сервера) не трогать.
6. Переводы всех новых строк.

**Критерии готовности.** Пункты гаснут при отсутствии модели (проверить:
стереть путь модели задачи в настройках → кнопка недоступна; вернуть →
доступна). Split-кнопки работают. Мини-тулбар: «Проверить»/«Исправить»
недоступны для блока без текста. `scripts/check.sh` зелёный (qmllint).

---

# Этап 6. Окно настроек «Модели по задачам»

**Цель.** Выбор модель+квант для шести задач, параметры Launch/Request,
валидация скачанности и очередь загрузки.

**Предусловия.** Этап 1 закоммичен (задачи, `tasks/*` ключи, списки
`ModelInstaller` по задачам).

**Файлы.** новый `resources/qml/Settings/TaskModelsWindow.qml`,
`resources/qml/Main.qml` (ленивый Loader + пункт меню),
`resources/qml/MainWindow/Header.qml` (пункт меню Настройки),
`src/runtime/ModelInstaller.{h,cpp}` (очередь установки),
`src/runtime/ModelInstallTransaction.cpp` (если нужна последовательная
очередь), `resources/i18n/llocr_ru.ts`, тесты `tests/test_model_installer.cpp`.

**Шаги.**

1. **Окно**: таблица 6 строк: Задача (текст) — Модель (ComboBox: профили,
   объявляющие задачу; показывать title + признак установленности) — Квант
   (ComboBox квантов выбранного профиля: id, размер, установлен).
   Предвыбор — текущие `tasks/<task>/*`. Под таблицей — вкладки
   Launch/Request выбранной строки (существующие компоненты с
   `role: <task>`).
2. **Сохранение**: записать выбор (`setLaunchModelPathForRole`,
   `selectModelProfile`, `models/quant/<id>`); затем проверка: для каждой
   задачи выбранный квант физически есть (`ModelRegistry` + файл). Если нет —
   немодальный диалог: список недостающих (задача, модель, квант, размер),
   «Скачать» / «Отмена». По согласию — последовательная очередь: установка
   по одной (существующий `ModelInstallTransaction`), прогресс в футере
   окна, по завершении каждой установки — модель назначается на задачу
   (как это уже делает установка из списка). Отмена очереди — стоп.
3. **External-режим**: вместо квантов — поле «Имя модели» на задачу
   (`tasks/<task>/modelName`), по образцу внешней вкладки LocationTab.
4. **Регистрация**: `Main.qml` — `Loader { active: false }` + `bringToFront()`;
   пункт меню Настройки → «Модели по задачам». Старые четыре пункта меню и
   окна пока остаются (удаляются на этапе 7).
5. Переводы.

**Критерии готовности.** Окно открывается с текущим состоянием; смена
модели сохраняется и переживает рестарт; при выборе нескачанного кванта и
согласии — скачивается и назначается (проверить на маленькой модели или
mock); отмена сохранения не меняет состояние. `scripts/check.sh` зелёный.

---

# Этап 7. Единое окно управления моделями, удаление старых окон

**Цель.** Один список всех моделей приложения; удалить четыре окна
«модель по типу».

**Предусловия.** Этап 6 закоммичен.

**Файлы.** новый `resources/qml/Settings/ModelsManagerWindow.qml` (или
переименование существующего), `resources/qml/Main.qml`, `resources/qml/MainWindow/Header.qml`,
удаление/упрощение: `resources/qml/Settings/ModelSettingsWindow.qml`,
`LocationTab.qml`, `Common/ModelDownloadList.qml`, `src/runtime/ModelInstaller.{h,cpp}`
(свойства под новый список), `src/runtime/ModelQuantModel.cpp`,
`resources/qml/Setup/StepModel.qml` (если ссылался на удаляемое),
`resources/i18n/llocr_ru.ts`, тесты `tests/test_model_installer.cpp`.

**Шаги.**

1. **Единый список**: одна модель строк «профиль × квант» по всем профилям
   (`ModelProfiles` ∪ `ModelRegistry`), группировка по профилю; действия:
   Скачать (квант), Удалить квант, Удалить модель, Открыть папку. Кнопки
   Use/Active убираются — назначение задач живёт в окне задач. Защита от
   удаления активного файла любой задачи сохраняется (сейчас: проверка по
   всем четырём ролям — расширить до шести задач).
2. **Удаление старого**: четыре экземпляра `ModelSettingsWindow` из
   `Main.qml`, пункты меню «OCR model / Decision model / Block OCR model /
   Layout model» из `Header.qml`, `LocationTab.qml`, per-role использования
   `ModelDownloadList`. Launch/Request-обёртки (`LaunchTab`/`RequestTab`)
   сохраняются — их использует окно задач. Порог decision остаётся в
   RequestTab задачи «Проверка».
3. `VerificationSettingsWindow` не трогать (autoCheck/autoFix/showConfidence,
   типы блоков).
4. Мёртвые свойства/сигналы `ModelInstaller` удалить; grep по QML на
   старые имена (`quantModels`, `checkQuantModels`, …).
5. Переводы; чистка `llocr_ru.ts` от удалённых строк.

**Критерии готовности.** Меню настроек: «Модели по задачам», «Модели»
(управление), Runtime, Проверка, Вывод, Интерфейс — без дублей. Единый
список показывает все профили; скачивание/удаление работают; попытка
удалить модель, назначенную на задачу, отказывается с понятным текстом.
`scripts/check.sh` зелёный.

---

# Этап 8. Миграции, зачистка legacy, визард

**Цель.** Убрать переходный код; убедиться, что fresh-профиль и старый
профиль обоих поколений приезжают в новое состояние.

**Предусловия.** Этапы 5 и 7 закоммичены.

**Файлы.** `src/config/SettingsStore.cpp` (миграция/ретайред ключи),
`src/config/RequestProfileStore.cpp` (старые имена файлов),
`src/app/VerificationQueueController.h` (переименования Recheck→Fix, если
отложено), `resources/qml/Setup/*`, `src/app/SelfTest*` / `runSelfTestQml`,
прочее по grep: `RecheckMode`, `check/`, `RequestProfilesValidate`,
`blockChecked`, `ocr"` в ролях.

**Шаги.**

1. Прогнать grep-лист и удалить остатки: старые ключи/аксессоры, старые
   имена синглтонов, неиспользуемые сигналы,.dead QML.
2. Визард: пройти fresh-профиль от нуля до конца (runtime → модель →
   self-test); убедиться, что пишет в `tasks/pageOcr/*` и стартует сервер.
3. Self-test (`runSelfTestQml`) работает с новыми ключами.
4. Проверить баг из todo: «сменилась модель → предупреждение о рестарте →
   после рестарта стартует не та модель» — воспроизвести/исправить в
   рамках нового переключения задач.
5. Старый профиль с моделями четырёх ролей: первая загрузка нового билда
   переносит всё в `tasks/*` (проверить миграцию на копии профиля).

**Критерии готовности.** grep-лист пуст; fresh-визард и мигрированный
профиль работают; `scripts/check.sh` зелёный.

---

# Этап 9. Финал: проверка, E2E, документация

**Цель.** Полная зелёная прогонка и фиксация документации.

**Предусловия.** Этапы 0–8 закоммичены.

**Шаги.**

1. `scripts/check.sh --configure` с нуля (чистое дерево) — всё зелёное;
   вручную clang-format/qmllint, если ctest прервал.
2. E2E-чеклист (руками, на реальных моделях):
   - настроить задачи в новом окне (включая скачивание очередью);
   - разметить страницу → статус «Только разметка»;
   - «Все блоки» → тексты в `box.text`, статус «Распознано»;
   - «Проверить» → зелёные/красные точки, бейджи confidence;
   - «Исправить» (блок и книга) → correctedText, серые точки, статусы
     проверки сброшены; повторная проверка судит коррекцию;
   - «Отменить исправление» → откат к статусу текста;
   - повторное распознавание блока → текст перезаписан, всё стёрто;
   - raw-распознавание страницы/книги → «Распознано (без разметки)»;
   - экспорт использует действующий текст;
   - save/open `.llocr` — статусы обоих слоёв и режим страниц сохраняются;
   - отсутствие модели задачи гасит пункты; external-режим живёт.
3. Документация: `AGENTS.md` (Current status — итог реворка, карта
   документов), `docs/07-glossary.md` (дополнить ADR по фактическим
   реализациям, если расходится), `docs/todo.md` — вычеркнуть выполненные
   пункты (блок «Нужно навести порядок…» целиком + строки 2–4, если
   закрыты раннее коммитом), модельные мануалы (13/15/16/18/19) — сверить
   разделы о ролях с новой матрицей.

**Критерии готовности.** Все этапы закрыты, проверка из п.1 зелёная,
E2E-чеклист пройден, документация сходится с кодом.
