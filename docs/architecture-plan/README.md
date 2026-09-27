# План устранения архитектурных проблем

Ревью: сентябрь 2026, HEAD `3f5e5c0`, ~19.7 k строк C++ + 49 файлов QML.
Область: `src/core`, `src/models`, `src/parsers`, `src/app`, `src/runtime`,
`resources/qml`, `tests/`, `src/CMakeLists.txt`, `docs/`.

Метод: сплошной разбор по слоям + перекрёстная проверка утверждений по коду.
Все находки ниже перепроверены в исходниках; ссылки `файл:строка` указывают на
состояние на дату ревью.

---

## 0. Резюме

Шесть системных проблем, из которых три — не «неаккуратно», а ломают
поведение пользователя или контракт безопасности:

| # | Проблема | Где | Эффект |
|---|----------|-----|--------|
| 1 | Удаление блока в UI меняет только view-model — документ, экспорт и очередь проверки сохраняют блок | `BoxListModel` / QML | тихая потеря правки, «призрак» блока |
| 2 | Роль запроса теряется при «слиянии» параллельных resolve → проверка текста уходит на OCR-модель | `RuntimeController` | неверный результат без ошибки |
| 3 | macOS: `owner.json` пишется и не читается; `ProcessGuard` на macOS — no-op | `LlamaServerProcess` / `ProcessGuard_mac` | осиротевший `llama-server` держит VRAM; обещанная в ADR 30/47 гарантия отсутствует |
| 4 | Синхронный image provider + блокирующий probe на GUI-потоке | `OcrImageProvider`, `RuntimeController::startServer` | фриз окна до 30 с (DjVu) / до 120 с (probe) |
| 5 | Один `qt_add_executable` на ~140 файлов, слои не разделены, `LLOCR_HAVE_DJVU` меняет layout класса | `src/CMakeLists.txt`, `tests/CMakeLists.txt` | слои не enforced; ODR-риск; 241 строка `.cpp` в тестах копируется руками |
| 6 | У документа три источника правды (текст страницы, `PageEditStore`, `page.boxes`/`blocks`) | `DetTokensParser`, `PageEditStore` | рассинхрон: правка «воскрешает» удалённый блок, dedup пишет не в тот индекс |

Плюс инфраструктурный долг: нет CI, `qmllint` структурно нерабочий (нет
`qt_add_qml_module`), 4 разных HTTP-идиомы, две независимые install-машины,
`modelRecipeId` одновременно является id адаптера и id профиля запроса.

Положительные стороны, которые надо сохранить: `ArchiveExtractor` и
`DownloadTask` (resume, ETag/Range, CRC) сделаны качественно; тесты не ходят в
интернет (только loopback); `BlockStyleMap`/`labels.json` — настоящая
data-driven точка расширения; `ProfileStorage` и `TestSettingsIsolation` — удачные
примитивы.

---

## Прогресс

| Этап | Статус |
|------|--------|
| 0. Страховка (CI, `qt_add_qml_module`, таймауты тестов, независимость app-теста от DjVu) | **✅ завершён (26.09.2026)**, кроме шага установки Qt в CI |
| 1. Корректность, без редизайна | **✅ завершён (26.09.2026)** |
| 2. Один источник правды для текста и блоков | **🟡 на 2/3**: парсер (2a) и текст страницы (2b) сделаны; рендер в воркере (2c) — нет |
| 3. Отзывчивость (проба/хеширование с GUI-потока) | **✅ завершён (26.09.2026)** |
| 4. Разделение на таргеты по слоям + разрыв цикла `app ↔ runtime` | **✅ завершён (26.09.2026)** |
| 5. Runtime: транзакции установки, «установленное» состояние, HTTP, осиротевший сервер | **✅ завершён**: D1, D7, D4/ADR 109, ADR 108 и D3/ADR 112 (публикация переименованием + блокировка у модели) |
| 6. Конфигурация и расширяемость моделей | **🟡 2/3**: раздельные id + список моделей по id (ADR 110) и общий обмен с сервером без копипаста (ADR 111) сделаны; `ProfileStore<T>` — нет (ADR 84 его отклонял) |
| 7. QML как слой представления | **✅ завершён (27.09.2026)**: правила гейтов и рестарта в C++ (ADR 113), живые переводы (ADR 114), list-модели вместо `QVariantMap`-ролей (ADR 115) |
| — | Вне этапов: примитив между тремя источниками правды (ADR 116) |

### Этап 6 — что сделано

| Пункт | Изменение | ADR | Проверка |
|-------|-----------|-----|----------|
| E1 | `model/recipeId` (адаптер) и новый `model/requestProfileId` (профиль выборки) разведены, миграция один раз в `applyStartupMigration()`; список моделей — настоящая модель `OcrModelListModel` с ролями `modelId`/`displayName`; `modelNames`/`modelIdToName`/`modelNameToId` удалены, `idForDisplayName` больше не подменяет модель по умолчанию | 110 | `TestRequestProfile::activeProfileIsIndependentOfTheModel`; попутно устранён висячий указатель в этом же файле тестов (`findParameter(store.activeProfile(), …)`) — из-за него изменение поведения проявлялось как зависание, а не как провал |
| E2 | `runChatExchange<Result>()` и `encodeImageDataUrl()` — общие; `GeneralPurposeModel` потерял копию механики (220 → 171 строка, `OcrModel` 232 → 175); data-URL строится в QByteArray; таймаут `ConnectionConfig` приведён к 120 с как в настройках | 111 | существующие тесты `test_ocr_models`; хуки намеренно оставлены `static` — виртуальные захватывали бы `this`, а future обязан завершаться и после уничтожения адаптера |

**Что осталось из этапа 6:** шаблон `ProfileStore<T>` (ADR 84 его явно отклонил в
пользу общих помощников `ProfileStorage` — это записанное решение, менять его
без отдельного обсуждения не буду). Открытыми остаются расхождения в логике
валидации параметров между `RequestProfile` и `LaunchProfile` (домены разные) и
приоритет примитива между настройками, `index.json` и обходом ФС (см. ADR 109).

### Этап 5 — продолжение

| Пункт | Изменение | ADR | Проверка |
|-------|-----------|-----|----------|
| ADR 108 | `HttpClient` — единая политика HTTP: таймаут, ручные редиректы с бюджетом переходов, https/loopback, `Authorization` не переносится на другой хост, системный прокси применяется из самого запроса. Мигрированы `ModelCatalog`, `ReleaseCatalog`, `DownloadTask`, `/v1/models` | 108 | `test_http_client` (двуххоповый сервер: токен есть на первом хопе, нет на втором; петля редиректов завершается ошибкой) + прежние тесты всех затронутых путей |
| D4 / ADR 109 | `InstalledState` — единственный ответ на «где рантайм и где lock»: пути выводятся из настроек при каждом обращении, lock пересоздаётся при переносе каталога, `pathsChanged` перескан��ивает список сборок. `main.cpp` создаёт один экземпляр и передаёт его в `RuntimeController`, `RuntimeInstaller`, `ModelInstaller`, `ModelInstallTransaction` | 109 | `movingTheRuntimeDirectoryMovesTheInstallLock`; 33/33 с DjVu и 32/32 без него |

### Этап 5 — D3: публикация переименованием (ADR 112)

| Изменение | Проверка |
|-----------|----------|
| `StagedInstall` — единый механизм публикации: staging → (move-aside → rename → drop backup), с откатом; деструктор убирает незакоммиченный staging. `InstallTransaction` переведён на него, `ModelInstallTransaction` теперь берёт **тот же** `.install.lock`, качает в `<staging>/model-<org>__<repo>` и коммитит атомарно; блокировка освобождается на успехе, ошибке, отмене и `shutdown()` | `test_staged_install` (5 кейсов) + `cancelledInstallLeavesNoModelDirectoryAndReleasesTheLock` |
| Имя staging детерминированное, а `cleanupStaging()` на старте сохраняет `model-…`: вне `<modelsDir>` скан его не видит, а докачка многогигабайтной модели после сбоя не теряется | — |

**Осознанно не сделано:** слияние двух транзакций в один параметризованный
движок. Остающееся дублирование — это *форма* (enum состояний, проброс
busy/progress/status), а не опасная часть; рискованная часть (запись в конечный
каталог, отсутствие блокировки, отсутствие отката) устранена. Слияние движков
осталось бы большой переработкой без выигрыша в безопасности — фиксирую как
незакрытое, а не как сделанное.

**Что осталось из этапа 5:**

* **D3 / ADR 96 — единый движок транзакций (форма, не безопасность).** `InstallTransaction` и
  `ModelInstallTransaction` (~1000 строк дублирования) должны стать одним
  параметризованным движком со staging-каталогом и атомарным swap; главное —
  модельная установка до сих пор **не берёт `.install.lock`**, не имеет staging и
  пишет прямо в конечный каталог, так что прерванная установка оставляет
  «пол-каталога», который следующий `scanModelsDir` примет за модель. Это
  переработка обеих транзакций с изменением порядка файловых операций и
  тестами на сбой в середине — отдельная итерация.
* **Вторая половина D4** — примитив между тремя источниками правды (настройки /
  `index.json` / обход ФС) с явным приоритетом. Сейчас `ModelRegistry::load()`
  уже сливает обход ФС с индексом; полноценная политика примитива — это
  проектное решение, а не исправление бага, поэтому в ADR 109 оно явно **не
  заявлено**.

### Этап 5 — что сделано

| Пункт | Изменение | ADR | Проверка |
|-------|-----------|-----|----------|
| D1 | `owner.json` читается: `ProcessGuard` получил кроссплатформенные `isProcessAlive` / `processImagePath` / `terminateProcess`, `ServerOwner` решает «осиротевший ли он» (pid жив + образ совпадает + не наш; если образ недоступен — умерший родитель), `RuntimeController` сканирует при старте и при смене каталога, футер предлагает «Stop it» | 107 | `orphanedServerIsDetectedAndCanBeTerminated`: мёртвый pid / наш собственный pid / живой чужой процесс, затем завершение |
| D7 | `DownloadGroup::progress()` считает по своим задачам, а не по агрегату менеджера | 107 | `groupProgressIsPerGroupNotTheManagerAggregate` (проверено: на старой реализации падает) |

Обещание ADR 30/47 («при следующем старте обнаружить осиротевший процесс и
предложить его завершить») теперь выполнено — на всех платформах, а не только
там, где `ProcessGuard` что-то умеет.

**Что осталось из этапа 5 и почему не в этот заход:**

* **D3 / ADR 96 — одна транзакция установки.** `InstallTransaction` и
  `ModelInstallTransaction` (~1000 строк дублирования) должны стать одним
  параметризованным движком со staging-каталогом и атомарным swap; главное —
  модельная установка сейчас не берёт `.install.lock`, не имеет staging и
  пишет прямо в конечный каталог, так что прерванная установка оставляет
  «пол-каталога», который следующий `scanModelsDir` примет за модель. Это
  переработка обеих транзакций, а не правка; она меняет порядок файловых
  операций, и нужна отдельная итерация с тестами на сбой в середине.
* **D4 / ADR 97 — единый «установленный».** `RuntimeInstaller` фиксирует пути в
  конструкторе, `ModelInstaller` и `RuntimeController` перечитывают их на каждый
  вызов, подписки на `runtimeRootDirChanged` нет ни у кого. Требует нового
  объекта-состояния и перепроверки всех мест чтения (модели, сборки, lock-пути).
* **C3 / ADR 98 — общий `HttpClient`.** Четыре идиомы HTTP, прокси включется
  как побочный эффект конструктора `DownloadManager`. Механический, но
  затрагивает четыре места и требует, чтобы таймаут/redirect/auth-политика
  совпали с самым строгим местом (`DownloadTask`).

### Этап 4 — что сделано

| Пункт | Изменение | ADR | Проверка |
|-------|-----------|-----|----------|
| 4.1 | `src/CMakeLists.txt` собирает слои как таргеты: `llocr_core` → `llocr_config` → {`llocr_models`, `llocr_parsers`} → `llocr_runtime` → `llocr_app` → `llocr`; ресурсы и QML-модуль остались на исполняемом файле | 106 | 32/32; правка заголовка в `core` пересобирает 2 TU вместо ~140 |
| 4.2 | 31 тест линкует те же таргеты; удалено 241 строк ручных `target_sources` и шесть копий триплета `ProcessGuard_*` | 106 | 32/32, сборка без DjVu — 31/31 |
| 4.3 | Цикл `app ↔ runtime` разорван: `llocr_config` (settings + profile stores + `RuntimePaths`) лежит *ниже* рантайма. `LaunchProfileStore` пришлось оставить в `llocr_runtime`: он черновит через `LaunchParametersModel` (QML-модель) и спрашивает `ReleaseCatalog::detectPlatform()` — обе зависимости раньше маскировались ручными списками тестов | 106 | конфигурация без DjVu собирается и проходит 31/31 |
| 4.4 | `LLOCR_HAVE_DJVU` стал `PUBLIC`-определением на `llocr_app`, а `app/DjVuDocument.cpp` — единственным условным исходником (заголовок включается безусловно, поэтому layout класса больше не зависит от опциональной зависимости) | 95, 106 | конфигурация без DjVu: 31/31 |

Побочные находки при разбиении: `Qt6::Qml` пришлось сделать `PUBLIC` у
`llocr_runtime`/`llocr_app` (заголовки `SelfTestController.h`,
`ModelInstaller.h`, `UiController.h` включают `<QQmlEngine>`), а
`test_export_renderer` линкует `Qt6::WebEngineQuick` сам, поскольку вызывает
`QtWebEngineQuick::initialize()`.

**Что осталось за рамками этапа 4:** каталоги по-прежнему не совпадают с таргетами
(`app/SettingsStore.*` лежит в каталоге `app/`, а компилируется в `llocr_config`),
и включение задаётся корнем `src/`, поэтому физически запретить `#include "app/…"`
из рантайма нельзя — запрещено *использование* (линковкой). Полное
переименование каталогов под таргеты — отдельная чистовая работа.

### Этап 3 — что сделано

| Пункт | Изменение | ADR | Проверка |
|-------|-----------|-----|----------|
| C2a | `RuntimeController::startServer()` и `probeRuntimePath()` зондируют бинарник на воркере; поколение отбрасывает отменённый/вытесненный probe; ошибка уходит в `failResolve()` только если resolve её ждал | 105 | `managedStartKeepsTheEventLoopRunning` — мок задерживает `--version` (`LLOCR_MOCK_VERSION_DELAY_MS`), тест меряет **максимальный разрыв** между тиками 5 мс и требует < половины задержки. Проверено в обе стороны: синхронный probe даёт разрыв 1767 мс и тест падает |
| C2b | Возобновляемая загрузка больше не перечитывает `.part` при старте: sha256 нельзя «подсеять» из дайджеста, поэтому готовый файл хешируется на воркере при верификации (`hashExistingPart()` удалён) | 105 | `resumeWithCorruptedPrefixFailsChecksum` (испорченный префикс всё равно ловится) + существующие 13 кейсов `test_download_manager` |
| C2c | `ModelInstallTransaction` решает «проектор уже на диске» (хеш многогигабайтного файла) на воркере, всё захватывается по значению | 105 | существующие `test_model_install_transaction` / `test_model_installer` / `test_install_lock` |

Правило, зафиксированное в ADR 105: **ничто, что читает файл целиком или ждёт
процесс, не выполняется на GUI-потоке**.

Не входит в этап 3 (остаётся C1 из этапа 2): рендер страницы внутри
синхронного `OcrImageProvider` — до 30 с декодирования DjVu на GUI-потоке.

### Этап 2 — что сделано и что осталось

| Пункт | Изменение | ADR | Проверка |
|-------|-----------|-----|----------|
| 2a (B3) | `DetTokensParser::parse()` строит только `page.boxes` и берёт текст из `rebuildText(page)`; исчезли параллельные `blocks` и `rawCoords`; дедупликация ищет по нормализованным прямоугольникам в `page.boxes` и пропускает неразмеченные фрагменты (новое `BoundingBox::positioned`) | 102 | 51/51 в `test_det_parser` (3 новых: drift+дубль, преамбула+дубль, инвариант `text == rebuildText` на 5 форматах) |
| 2b (B2) | Текст принадлежит странице: `setPageText()`/`pageText()` — единственные точки записи/чтения; `PageEditStore` теперь только baseline + флаг «правил» (+ общие хелперы ремапа в `PageIndex.h`); у `ExportController` исчезла зависимость от стора | 103 | новый `test_page_edit_store` (8 кейсов) + `typingTheVisibleTextBackKeepsStructuralEdits`; 32/32 всего |
| 2c (B4, частично) | Контракт потоков записан в `DocumentModel.h` и `AppController.h` — что GUI-resident и какие два пересечения намеренные | 104 | 32/32 |

**Что осталось из 2c (C1) и почему не сделано сейчас.** Рендер страницы
(для DjVu — до 30 с busy-wait в `DjVuDocument::waitForJob`) происходит внутри
синхронного `OcrImageProvider::requestImage`, то есть на GUI-потоке. Нужен
воркер-рендерер, а это меняет контракт с UI: `AppController::pageImage()`
должен вернуть пустой результат и позже «прислать» картинку (`imageRevision`),
а `RecognitionController` — получить её асинхронно, иначе он увидит пустое
изображение и откажется кодировать страницу. Без возможности интерактивно
проверить превью в этом окружении такая правка рискует превратить фриз на 30 с
в пустой превью. Отдельно уточнено (в ADR 104): пересечение потоков при экспорте —
это **не** гонка данных, а удержание write-lock на время рендера кропа.

### Что сделано в этапе 0

| Пункт | Изменение | ADR | Проверка |
|-------|-----------|-----|----------|
| 0.3 | `tests/CMakeLists.txt` одним циклом по всем `add_test` задаёт `TIMEOUT 300` и `QT_QPA_PLATFORM=offscreen`; `test_export_renderer` вместо offscreen получает `QTWEBENGINE_CHROMIUM_FLAGS`; явный таймаут теста не перетирается | 99 | 31/31, все тесты несут таймаут |
| 0.4 | `test_app_import` собирается **всегда**: фикстуры — растр + генерируемый 3-страничный PDF (`QPdfWriter`) + нечитаемый файл; добавлен `mixedImportCommitsFilesInOrder` (синхронный путь импорта), а три кейса асинхронного пути (worker-декодирование) остались под `#ifdef LLOCR_HAVE_DJVU` | 99 | сборка без DjVu: 30/30, `test_app_import` — 7 кейсов вместо 0 |
| 0.1 | `scripts/check.sh` (configure → build → ctest → clang-format → qmllint; отсутствующий инструмент = заметка, не «зелёный молча») + `.github/workflows/ci.yml` (macOS/Windows/Linux) с явным `TODO(provision-qt)` | 100 | скрипт зелёный; гейт qmllint проверен «на падение» и «на чистоту» |
| 0.2 | QML как настоящий модуль: `qt_add_qml_module(URI LLocr)`, глоб 49 файлов с `CONFIGURE_DEPENDS`, `qrc:/qt/qml/LLocr/…`, `QT_RESOURCE_ALIAS` + `QT_QML_SINGLETON_TYPE` для двух QML-синглтонов, `BlockNames.qml` перенесён в корень модуля, инертные `QML_ELEMENT`/`QML_SINGLETON` удалены, `.qmllint.ini` как гейт | 101 | приложение стартует без QML-ошибок; qmllint — 0 предупреждений |

Две находки, которые дал сам линт (исправлены в этом же этапе): `Theme`/`BlockNames`
без `QT_QML_SINGLETON_TYPE` превращались в обычные типы — каждое `Theme.accent`
читалось как `undefined` (1326 строк в логе запуска), а `qmllint` не мог найти
`BlockNames.displayName/hint/groupTitle`; и `SetupWizard.currentStep` был
типизирован `Item`, хотя шаги объявляют свой `complete` (теперь `var`).

Открытые пункты этапа 0:

* **Шаг установки Qt в CI** — нужен реальный артефакт Qt 6.10.3 для раннеров
  (официального инсталлятора достаточно не всегда); до его подключения workflow
  не включать на `push`/`pull_request`.
* **C++-синглтоны как типы модуля** — требуют `create()`-фабрик и решения по
  владению (объекты живут в `main()` и переживают движок). Это этап 4.
* **`clang-format` в локальном гейте** — не установлен на этой машине, поэтому
  шаг пропускается с заметкой; в CI он жёсткий, и первый прогон может показать
  расхождения форматирования.

### Что сделано в этапе 1

Все восемь пунктов выполнены, каждый — с регрессионным тестом; полный
`ctest` зелёный после каждого шага (31/31 в основном дереве, 29/29 в
конфигурации без DjVu).

| Пункт | Изменение | ADR | Тест |
|-------|-----------|-----|------|
| B1 | `AppController::removeBlock(index)` — единственная точка удаления блока; `BoxListModel::removeBox` больше не `Q_INVOKABLE` и не шлёт `boxRemoved`; три QML-сайта переведены на контроллер | 92 | `TestAppImport::blockRemovalUpdatesDocumentAndView`, `TestBoxModel::removeBoxIsNotQmlCallable` |
| D2 | resolve с ключом по роли: `m_deferredResolves` + `startResolveForRole(role)`; `cancelPendingStart()` чистит очередь; защита от пере-вложенного resolve | 93 | `TestEnsureConnection::crossRoleRequestDuringResolveIsNotAnsweredWithTheWrongRole` |
| C4 | дамп сырого ответа только при `LLOCR_RAW_DEBUG=1`; тело запроса дампится до POST; убран безусловный `qDebug()` тела запроса в `GeneralPurposeModel` | 94 | `TestOcrModels::rawDebugDumpIsOptIn` |
| A3 | `#ifdef` убран из `DocumentModel.h` (layout класса больше не зависит от define) | 95 | конфигурация `LLOCR_WITH_DJVU=OFF` собирается, линкуется и проходит 29/29 |
| D6 | `ReleaseCatalog::resetCache` удалён, откат на последний валидный кэш, таймер-сторож на вложенном цикле событий, rate-limit читается до `deleteLater()`, эндпоинт стал параметром | 98 | `TestReleaseCatalog::staleCacheSurvivesFailedFetch` (500 / 403 / мусор) |
| D5 | `ModelRegistry::update()` — read-modify-write под `.registry.lock`; два вызывающих переведены на него | 97 | существующие `test_model_registry` / `test_model_install_transaction` / `test_model_installer` |
| E4 | `ParserFactory::create("auto")` отклоняет значение; активация модели не переписывает выбор парсера; удалены мёртвые `kSchemaVersion`/`kSchemaKey` | 96 | `TestDetParser::autoIdIsSelectableButNeverCreatesAParser` |
| F5 | исправлены висящие ссылки на `docs/refactoring-plan/`, разведены дубли ADR 78/79/83 (вторые 78/79 → 90/91, дублирующая строка 83 удалена, ссылки в `04-components.md` обновлены), `docs/03-architecture.md` — реальные окна настроек вместо `SettingsDialog.qml`, `docs/TODO.md` добавлен в карту `AGENTS.md`, `CMakePresets.json` приведён к реальности (Unix Makefiles вместо Ninja+vcpkg), ADR 61 — два оставшихся `QObject::tr()` в `RuntimeController` переведены в контекст класса с переносом переводов в `llocr_ru.ts` | 94, 98 | — |

Два отступления от исходного плана этапа 1, оба зафиксированы в ADR:

* **Пункт B1** выполнен не «подключением сигнала», а переносом мутации в
  контроллер: подключение сигнала оставило бы два пути изменения документа и
  сохранило бы `Q_INVOKABLE`-мутатор у view-модели.
* **ADR 88 (c)** («активация модели переписывает парсер — намеренно») было
  **изменено на противоположное** (ADR 96): аргумент «единственный способ
  заранее выбрать недефолтный парсер» гипотетический, а наблюдаемый вред —
  активация модели молча затирает явный выбор в Settings → Output. Если решение
  нужно вернуть, это одна строка в `ModelInstaller::setActiveModel` + правка
  ADR 96.

Не сделано сознательно: удаление пустых каталогов `cmake/` и `rag-service/`
(они не отслеживаются git и ни на что не влияют; `cmake/` понадобится на этапе 4).

---

## 1. Находки

### A. Сборка и разделение слоёв

**A1. Всё приложение — один target, `cmake/` пуст.**
`src/CMakeLists.txt:1` — `qt_add_executable(llocr ...)` на ~140 исходников из
всех пяти слоёв. Единственный include-root — `target_include_directories(... src)`
(`src/CMakeLists.txt:315`), поэтому `src/runtime/*.cpp` и `src/core/*.cpp`
одинаково резолвят `#include "app/…"`. Слои из `docs/03-architecture.md:3-38` —
соглашение, а не ограничение компилятора. *High / L.*

**A2. Тесты копируют production-исходники вручную.**
`tests/CMakeLists.txt` — 241 строка с `.cpp` на 31 target; `SettingsStore.cpp`
компилируется 12 раз, `RuntimePaths.cpp` — 11. Платформенная триплет
`ProcessGuard_{win,mac,linux}.cpp` повторён 6 раз. При этом `test_app_import`
(`tests/CMakeLists.txt:193-204`) — единственный, кто выводит список
автоматически из `get_target_property(llocr SOURCES)`. *High / M.*

**A3. `LLOCR_HAVE_DJVU` — PRIVATE-define, меняющий layout класса в заголовке.**
`src/CMakeLists.txt:342` объявляет его PRIVATE, но `src/app/DocumentModel.h:15,39,86`
под `#ifdef` прячет **члены** класса (`m_djvus`, `PreparedDjVu::document`).
Заголовок включается ещё в 4 TU (`AppController.h`, `PageEditStore.h`,
`VerificationQueueController.h`, `ExportController.h`). Любой новый target,
собирающий `DocumentModel.cpp` без define, получает другой layout того же класса —
не link error, а тихая порча памяти. Сейчас работает только ручная дисциплина
(`tests/CMakeLists.txt:158,183,219`). *High / S.*

**A4. `app ↔ runtime` — реальный цикл зависимостей.**
12 `#include "app/…"` в 7 файлах `runtime/` (`RuntimeController.cpp:12-13`,
`ServerLaunchConfig.cpp:4-5`, `RuntimeInstaller.cpp:11`, `ModelInstaller.cpp:10-11`,
`ModelInstallTransaction.cpp:14-15`, `RuntimeLog.cpp:7`, `SelfTestController.cpp:5-6`)
против 11 `#include "runtime/…"` в 8 файлах `app/`. Документ рисует runtime строго
*под* бэкендом. Следствие: слой нельзя вынести ни в сервис, ни в CLI, и он не
тестируется независимо. *High / L.*

**A5. Нет CI; `qmllint` не может работать в принципе.**
Каталога `.github/` нет; `docs/06-dev-setup.md:8` и `docs/05-roadmap.md:75` —
CI в статусе «todo». `qt_add_qml_module` не используется нигде: QML едет как
сырые `qrc:/qml/*` (`src/CMakeLists.txt:173-226`), 14 синглтонов регистрируются
руками в `src/main.cpp` (15 вызовов `qmlRegisterSingletonInstance`), а
`QML_ELEMENT`/`QML_SINGLETON` в `UiController.h`, `RuntimeInstaller.h`,
`ModelInstaller.h`, `RuntimeLog.h`, `SelfTestController.h` **инертны** без
`qmltyperegistrar`. Типовые ошибки QML ловятся только в рантайме. *High / M.*

### B. Целостность данных документа

**B1. «Удалить блок» правит только view-model.**
`BoxListModel::removeBox()` — `Q_INVOKABLE`, шлёт `boxRemoved`
(`BoxListModel.cpp:108-115`), но сигнал **никем не подключён**. Обработчик
`AppController::onBoxRemoved` (`AppController.cpp:598`, объявлен `Q_INVOKABLE`
в `AppController.h:171`) не имеет ни одной точки вызова — grep по `src/`,
`tests/`, `resources/` даёт только объявление и определение. Все три UI-сайта
идут в модель напрямую: `ImagePreview.qml:31`, `ImagePreview.qml:158`,
`BlockEditPanel.qml:129`.

Последствия: блок остаётся в `DocumentModel` → текст страницы не
перестраивается, `PageEditStore` не знает об удалении, `ExportController` экспортирует
«удалённую» картинку, `VerificationQueueController` всё ещё может её
проверять (и записать результат по сдвинувшемуся индексу), а ближайший
`updateBoxesForCurrent()` (смена страницы, повторное распознавание) возвращает
прямоугольник обратно. *High / S.*

**B2. Два источника правды для текста страницы; «оригинал» выбирается непоследовательно.**
`OcrResult::text` фиксируется на этапе парсинга (`AppController.cpp:511`) и
больше не обновляется, тогда как актуальный текст живёт в `PageEditStore`.
После структурной правки (удаление блока, применение исправления проверки) в
`m_editStore` попадает перестроенный текст (`AppController.cpp:612,799-800`),
а `setCurrentPageText` (`AppController.cpp:552`) сравнивает пользовательский ввод
с **исходным** `result.text`. `PageEditStore::setText` при `text == original`
удаляет запись и возвращает `NowClean` (`PageEditStore.cpp:22-26`) — то есть
ввод актуального текста **откатывает** структурные правки: удалённый блок
воскресает, исправление проверки теряется. `revertCurrentPageEdits` (`:571`) —
тот же дефект; корректен только `revertBlockCorrection` (`:711-718`). *High / M.*

**B3. `DetTokensParser` держит три несинхронизируемых пространства индексов.**
`page.boxes`, `blocks` (`QStringList`) и `rawCoords` расходятся дважды:
* `page.boxes.append(box)` (`:395`) происходит **до** `if (boxText.isEmpty()) continue;`
  (`:405-406`) → пустой блок добавляет бокс без записи в `blocks`;
* `dupIndex` — индекс в `rawCoords`, куда попадают только токены с bbox
  (`:396-397`), но по нему пишутся **и** `page.boxes[dupIndex]` (`:383`), **и**
  `blocks[dupIndex]` (`:390`), и он же становится индексом картинки
  `style.imageIndex = dupIndex` (`:388`).

Как только перед дубликатом встретится токен без bbox или пустой блок,
«замена» перезапишет не тот бокс, не тот Markdown-блок и не тот
`image://ocr/crop/<n>`. `rebuildText` (`:241`) обходит `page.boxes`
независимо, поэтому может разойтись с `page.text` сразу после парсинга. *High / M.*

**B4. `QReadWriteLock` декоративна: 12 блокировок из 63 обращений.**
`m_documentLock` берётся в 12 местах (`AppController.cpp:41,194,201,228,234,240,344,357,413,457,695,770`),
а `m_document` встречается 63 раза; не заперты, в частности, все записи в
`applyRawResult` (`:510-527`), `onBoxRectChanged` (`:583-589`), `onBoxRemoved`
(`:602-609`), `setCurrentPageText`, `setSelectedBoxIndex`. `ExportController` и
`VerificationQueueController` читают `DocumentModel` через внедрённые ссылки
вообще без блокировки. При этом фактически ничего не пересекает потоки: воркеры
получают копии (`QImage`, `OcrRequest`). То есть блокировка сегодня не защищает
ничего, но **заявляет**, что `DocumentModel` безопасен из воркеров, — ровно то
убеждение, которое сломается при переносе рендера в поток (см. C1). *Med-High / M.*

### C. Потоки и отзывчивость UI

**C1. `OcrImageProvider` синхронный, полная страница рендерится на GUI-потоке.**
`OcrImageProvider.cpp:10` — `QQuickImageProvider(QQuickImageProvider::Image)` без
флага `Asynchronous`, значит `requestImage` выполняется в GUI-потоке и зовёт
`AppController::pageImage()`, который берёт **write-lock** и уходит в
`DocumentModel::fullImage()` (`AppController.cpp:226-230`). Для DjVu это
busy-wait цикл до 30 с (`DjVuDocument.cpp:12` `kDecodeTimeoutMs = 30000`,
`:50-70` `QThread::msleep(5)`); для PDF — рендер 300 dpi. `ImagePreview.qml:13`
перезапрашивает полное изображение на каждый `imageRevision`, т.е. на каждую
смену страницы. *High / M.*

**C2. Блокирующая работа на GUI-потоке: probe до 120 с, sha256 гигабайтных файлов.**
`RuntimeController.cpp:29` — `kProbeTimeoutMs = 120000`; `startServer` вызывает
`RuntimeLocator::probeCached` **синхронно** (`:535-536`), а тот — `waitForStarted`
+ `waitForFinished(timeoutMs)` (`RuntimeLocator.cpp:26-35`). Тот же путь доступен
из QML как `Q_INVOKABLE` (`RuntimeController.cpp:649`). Сюда же: перехеширование
`.part` в `DownloadTask::hashExistingPart` (`DownloadTask.cpp:283-295`) на GUI-потоке
и sha256 целого GGUF в `ModelInstallTransaction::mmprojAlreadyOnDisk` (`:394-425`).
*High / M.*

**C3. Четыре HTTP-идиомы, нет общего клиента.**
Асинхронный reply (`DownloadTask.cpp:239-266`), вложенный `QEventLoop` с таймером
(`ModelCatalog.cpp:55-127`), вложенный `QEventLoop` без таймера
(`ReleaseCatalog.cpp:221-224`), поллинг по таймеру
(`LlamaServerProcess.cpp:157-183`) + свой `QNetworkAccessManager` в
`RuntimeController.cpp:405-415`. Прокси включён как побочный эффект конструктора
`DownloadManager` (`DownloadManager.cpp:21`), поэтому остальные сайты зависят от
того, что этот объект был создан. Политика redirect/timeout/auth реализована
4 раза; «закалён» только `DownloadTask.cpp:480-514`. Побочно: `ReleaseCatalog`
сбрасывает офлайн-кэш на **любой** сбой транспорта (`:229,250,257,265`) и читает
`X-RateLimit-Reset` уже после `reply->deleteLater()` (`:237,240`). *Med / M.*

**C4. Debug-дамп сырого ответа включён по умолчанию в production.**
`OcrModel.cpp:29-38` — `rawDebugEnabled()` возвращает `true`, если имя
приложения не начинается с `test`; в релизной сборке дамп пишется всегда, а
`dumpRawRequest` (`:49-68`) сохраняет **тело запроса целиком, включая
base64-изображение страницы**, в `<AppData>/raw-debug/`. Дамп запроса вызывается
из обработчика ответа (`:203`), поэтому прерванные запросы не попадают в лог
вообще. *High / S.*

### D. Runtime: незакрытые контракты и две install-машины

**D1. `owner.json` пишется и никогда не читается.**
`writeOwnerJson` / `clearOwnerJson` (`LlamaServerProcess.cpp:519-542`), путь
задаётся в `RuntimeController.cpp:560`. Поиск по `src/`, `tests/`, `resources/`
находит только запись, удаление и комментарии. При этом:
* `ProcessGuard_mac.cpp:8-12` — и `install()`, и `attachParent()` являются no-op
  и обосновывают это ровно отсутствующим механизмом;
* `docs/09-local-runtime-plan.md` §5.4 обещает: «при следующем старте LLocr
  обнаруживает осиротевший процесс **и предлагает пользователю** его завершить»;
* ADR 30/47 фиксируют «best-effort на macOS (`owner.json` + next-start detection)».

Итог: после жёсткого kill GUI на dev-платформе `llama-server` остаётся жить,
занимая VRAM; следующий старт выбирает новый свободный порт
(`LlamaServerProcess.cpp:106-107`) и ничего не замечает. *High / S-M.*

**D2. Роль запроса теряется при слиянии параллельных resolve.**
`RuntimeController.cpp:242-245`: если resolve уже идёт, вызывающий просто
добавляется в `m_resolveCallbacks` и **возвращается, не запомнив свою роль**;
`m_resolveRole` (`:277`) остаётся от первого, а `completeResolve` (`:317-330`)
отдаёт всем один и тот же `ResolvedConnection`. Вызывающие: `CheckController.cpp:66`
(Check) и `RecognitionController.cpp:76` (Ocr) — очередь проверки может
встать в ходе распознавания. Результат: проверка уходит на OCR-модель
(неверный `modelId`/`mmproj`), `beginRoleSwitch` не вызывался, ошибки нет. *High / M.*

**D3. Две install-машины; у моделей нет ни блокировки, ни транзакции.**
`InstallTransaction` (`.h:35-45`, `.cpp:74-201`: стадия → распаковка → probe →
rename-aside → атомарный rename → rollback) и `ModelInstallTransaction`
(`.h:26-55`, `.cpp:217-548`) повторяют одну и ту же машину (enum состояний,
busy/progress/status, prepare/begin/enqueue/maybeFinish/complete) — ~1000 строк;
`ModelInstaller` затем превращается в проброс сигналов
(`ModelInstaller.cpp:28-40`) — третья копия `setState/setBusy/setProgress/setStatusMessage`.
Расхождение важнее дублирования: установка runtime берёт `.install.lock`
(`RuntimeInstaller.cpp:391-402`), а **установка моделей не берёт блокировку
вообще** и пишет прямо в конечный каталог (`ModelInstallTransaction.cpp:327`) —
без staging, без rename, без rollback. Прерванная установка оставляет
полузаполненный `<modelsDir>/<org>__<repo>`, который следующий `scanModelsDir`
примет за валидную модель. *Med-High / L.*

**D4. Три источника правды об «установленном», без инвалидации.**
Ключи настроек (`SettingsStore.cpp:491-515`), `modelsDir/index.json`
(`ModelRegistry.cpp:149-152`) и обход ФС (`ModelRegistry.cpp:257-325`,
`InstallTransaction.cpp:203-245`). Сверки в рантайме нет. Хуже: `RuntimeInstaller`
фиксирует `m_paths` **и путь блокировки** в конструкторе
(`RuntimeInstaller.cpp:64-65`), тогда как соседи перечитывают настройки на каждый
вызов (`ModelInstaller.cpp:143-145`, `RuntimeController.cpp:534`);
`RuntimeController` подписан только на `serverPath`/`connectionMode`/`launchModelPath`
(`:47-52`), но не на `runtimeRootDirChanged`; `rescanInstalledBuilds` (`:556-557`)
строит свежие пути, а блокировку использует старую. *Med / M.*

**D5. `index.json`: read-modify-write не покрыт блокировкой.**
`ModelRegistry::save` берёт `.registry.lock` (`.cpp:228-233`), но все вызывающие
передают список, собранный из **незапертого** снимка в памяти
(`ModelInstallTransaction.cpp:511-516`, `ModelInstaller.cpp:348-351`).
Блокируется запись, а не чтение-изменение-запись → второй инстанс молча
теряет запись первого. *Med / S.*

### E. Конфигурация и модели

**E1. `modelRecipeId` — одновременно id адаптера и id активного профиля запроса.**
`Settings.modelRecipeId` выбирает адаптер (`RecognitionController.cpp:59`,
`AppController.cpp:123,128`) и одновременно определяет активный request-профиль
(`RequestProfileStore.cpp:135-137`), причём `RequestProfileStore.cpp:49,95`
подставляет `SettingsStore::kDefaultModelRecipeId = "unlimited-ocr"` как id
профиля. UI выбирает модель по **отображаемым именам** (`AppController::modelNames`,
`RequestTab.qml:56-66`) с синхронизацией через `indexOf(modelIdToName(...))`,
а `OcrModelFactory::idForDisplayName` для неизвестного имени молча возвращает
id по умолчанию (`OcrModelFactory.cpp:42-50`) — рассинхрон показывает одну
модель, а использует другую. *Med-High / M.*

**E2. `OcrModel` не расширяем — абстракция даёт только промпты.**
`OcrModel.h:35` — `recognize()` **не virtual**; `buildRequestBody` (`:40`) и
`parseResponse` (`:42`) объявлены `static`. Переопределить можно только
`id/displayName/promptVariants/defaultParserId`. Модель с иной формой ответа
требует правки самого `OcrModel.cpp`, и `GeneralPurposeModel` в ответ
**скопировал класс целиком**: `encodeImageDataUrl` совпадает побайтово
(`OcrModel.cpp:101-118` vs `GeneralPurposeModel.cpp:38-55`), так же как
encode→post→parse и цикл сортировки параметров. Транспорт (`LlamaClient`) —
конкретный класс с приватным `QNetworkAccessManager` и статическим
`endpointUrl()`, зашивающим `/v1/chat/completions` (`LlamaClient.h:15-21,37`);
в тестах из-за этого приходится поднимать сырой `QTcpServer`
(`tests/test_ocr_models.cpp:45-99`). *High / L.*

**E3. Четыре хранилища профилей, три политики схемы, дублированные валидаторы.**
* Валидация параметров написана дважды: `RequestProfile::fromJson`
  (`core/RequestProfile.cpp:152-211`) и `LaunchProfile::parseParameter/parseFile`
  (`core/LaunchProfile.cpp:71-175`); конвертация значения существует в трёх видах.
* `schemaVersion` проверяется в `VerificationPromptStore.cpp:318-325` и
  `ModelRegistry.cpp:191`, пишется-но-не-читается в `RequestProfileStore.cpp:111`
  и `LaunchProfileStore.cpp:109`, а в `core/RequestProfile.cpp:18-19` и
  `core/LaunchProfile.cpp:15-16` остались **мёртвые** `kSchemaVersion`/`kSchemaKey`.
* Путь хранилища пересчитывается из изменяемых настроек трижды
  (`RequestProfileStore.cpp:60-63`, `LaunchProfileStore.cpp:54-57`,
  `VerificationPromptStore.cpp:171-174`): правка `runtimeRootDir` молча
  перенаправляет все файлы профилей.
* `launch/presetId` (`SettingsStore.h:334`) — мёртвый дубль `launch/profileId`.
* `ConnectionConfig::timeoutMs = 240000` (`core/ConnectionConfig.h:10`) против
  `kDefaultTimeoutMs = 120000` (`SettingsStore.h:266`). *High / L.*

**E4. История «auto»-парсера имеет три источника.**
`effectiveParserId()` (`AppController.cpp:117-124`) корректно резолвит `auto`
через `OcrModel::defaultParserId()`, но: (1) `ModelInstaller.cpp:261-262`
перезаписывает `Settings.parserId` из `ModelPreset::parser`
(`resources/profiles/defaultLlmPresetsOcr.json:12`), молча отменяя `auto` при
активации пресета; (2) `ParserFactory.h:16-19` утверждает, что `auto` «никогда не
доходит до `create()`», но `create("auto")` возвращает `RawParser`
(`ParserFactory.cpp:43-46`), и это закреплено тестом
(`tests/test_det_parser.cpp:974`) — инвариант держится на соглашении;
(3) `Lfm25VlDrift.cpp` применяется ко **всем** ответам без обёртки
(`DetTokensParser.cpp:280`) вне data-driven механизма `labels.json`.
Дополнительно: `setActiveModel` пишет `launch/modelPath`, но не `model/recipeId`,
так что активированный GGUF может разойтись с адаптером. *Med / S.*

### F. Гигиена и инфраструктура

* **F1.** Нет `qt_add_qml_module` → `qmllint` даёт «Failed to import LLocr» на всех
  49 файлах; список QML-ресурсов ведётся руками (`src/CMakeLists.txt:173-226`).
* **F2.** Из 31 `add_test` только `test_app_import` имеет `set_tests_properties`
  (таймаут 90 с + offscreen) — 30 тестов без таймаута, включая спавнящие реальные
  процессы; `test_export_renderer` создаёт `QGuiApplication` без offscreen-платформы.
* **F3.** Единственный интеграционный тест слоя view-model — `test_app_import` —
  собирается **только при наличии DjVu** (`tests/CMakeLists.txt:191`), то есть на
  дефолтной конфигурации `AppController`, `RecognitionController`,
  `ExportController`, `VerificationQueueController`, `CheckController`,
  `PageEditStore` не тестируются вообще.
* **F4.** `AppController`, `ExportController`, `RecognitionController`,
  `VerificationQueueController` не подключены к `retranslate()`
  (`src/main.cpp:168-176` — только `engine` + 4 runtime-класса);
  `exportNameFilters` помечен `CONSTANT` (`AppController.h:64`), поэтому фильтры
  диалога экспорта не переводятся.
* **F5.** Дрейф документации: `docs/refactoring-plan/README.md` не существует, но
  упомянут 4 раза (`AGENTS.md`, ADR 82/85/86 в `docs/07-glossary.md`);
  дубли номеров ADR **78, 79, 83**; `docs/03-architecture.md:7` упоминает
  несуществующий `SettingsDialog.qml`; `docs/TODO.md` отсутствует в карте
  документации `AGENTS.md:28-38`; ADR 61 фактически неверен (в
  `RuntimeController.cpp:599,626` остался `QObject::tr()`); пустые `cmake/`
  и `rag-service/`; `CMakePresets.json` описывает неиспользуемый Ninja-пресет.
* **F6.** Fallback экспорта выполняется на GUI-потоке: `ExportController.cpp:136-145`
  → `writePdfFallback` / сборка HTML целиком; строки статуса формируются в воркере,
  а переводятся в GUI-потоке — разный `tr()`-контекст для одной строки.

---

## 2. План работ

Порядок выбран так, чтобы каждый этань был самодостаточным и мержился
отдельно. Оценки: **S** ≈ 0.5–1 день, **M** ≈ 2–4 дня, **L** ≈ 1–2 недели.
Этапы 0–1 настоятельно рекомендуются до любых рефакторингов: без страховки
остальные этани рискованнее, чем кажутся.

### Этап 0. Страховка (M)

| # | Задача | Что даёт | Оценка |
|---|--------|----------|--------|
| 0.1 | `scripts/check.sh`: configure + build + `ctest` + `clang-format --dry-run` + `qmllint` (после 0.2). CI на GitHub Actions (macOS/Windows/Linux) — если репозиторий публичный; иначе достаточно скрипта | воспроизводимая проверка перед рефакторингом | M |
| 0.2 | `qt_add_qml_module(LLocr …)` вместо сырых `qrc:/qml/*`; связать с ручной регистрацией в `main.cpp`, чтобы не сломать QML API | работающий `qmllint`, `.qmltypes`, инертные `QML_ELEMENT` начинают работать | M |
| 0.3 | Общий таймаут + `QT_QPA_PLATFORM=offscreen` для всех тестов (хелпер в `tests/CMakeLists.txt` вместо 30 копий) | зависший `waitForStarted` больше не вешает `ctest` навсегда | S |
| 0.4 | Сделать `test_app_import` (или его эквивалент) независимым от DjVu: фикстуры — изображения/PDF, DjVu — дополнительный кейс | view-model-слой покрыт тестами на дефолтной конфигурации | S |

Итог этапа: зелёная проверка одной командой + первый тест на слое view-model.

### Этап 1. Корректность, без редизайна (M)

Каждый пункт — отдельный коммит с регрессионным тестом.

1. **B1** — единственная точка мутации: подключить
   `BoxListModel::boxRemoved` → `AppController::onBoxRemoved` в конструкторе
   (`AppController.cpp:60-104`), снять `Q_INVOKABLE` с `removeBox`, поправить три
   QML-сайта (`ImagePreview.qml:31,158`, `BlockEditPanel.qml:129` — там же
   добавить `selectedBoxIndex = -1`). Тест: удаление блока меняет текст страницы,
   экспорт и не оставляет бокс в `DocumentModel`. *S.*
2. **D2** — resolve с ключом по роли: хранить ожидающие вызовы в
   `QHash<ConnectionRole, callbacks>` (или отклонять смешанную роль, если
   сервер один). Тест в `test_ensure_connection`: Check-запрос во время
   Ocr-resolve получает check-соединение либо явную ошибку. *M.*
3. **C4** — дамп сырого ответа только по явному opt-in (настройка
   `runtime/rawDebug` или переменная окружения), убрать проверку
   `applicationName()` из логики; писать тело запроса на старте запроса, а не в
   обработчике ответа. Тест: `rawDebugEnabled()` выключен по умолчанию. *S.*
4. **A3** — убрать `#ifdef` из `DocumentModel.h` (безусловный член через
   type-erased `std::shared_ptr<void>` с удалителем либо сборка
   `DjVuDocument.cpp` со стаб-dlopen), `LLOCR_HAVE_DJVU` оставить только в `.cpp`.
   Проверка: новый target, собирающий `DocumentModel.cpp` без define. *S.*
5. **D6** (из C3) — `ReleaseCatalog`: не сбрасывать офлайн-кэш на транспортных
   сбоях, добавить таймер к `QEventLoop`, читать rate-limit заголовок до
   `deleteLater()`. *S.*
6. **D5** — `ModelRegistry::update(modelsDir, fn)`, удерживающий блокировку на
   всём цикле чтение-изменение-запись. *S.*
7. **E4 (часть)** — `ParserFactory::create()` начинает отклонять `auto` (ADR 88),
   убрать перезапись `Settings.parserId` из `ModelPreset::parser`, убрать мёртвые
   `kSchemaVersion`/`kSchemaKey`. *S.*
8. **F5** — починить ссылки на `docs/refactoring-plan/`, развести дубли ADR
   78/79/83, исправить ADR 61 и `docs/03-architecture.md:7`, удалить пустые
   `cmake/`/`rag-service/`, привести `CMakePresets.json` в соответствие с
   реальным генератором. *S.*

### Этап 2. Один источник правды для текста и блоков (M)

Цель: убрать B2/B3/B4 как класс, а не как симптом.

* **ADR 90 — «Текст страницы принадлежит странице».** `DocumentPage` хранит
  авторитетный текст; `rebuildText()` пишет его обратно в страницу;
  `PageEditStore` понижается до отметки «правил» (dirty) + снимок оригинала
  для Revert. Следствие: B2 исчезает по построению — сравнение всегда с
  актуальным текстом.
* **ADR 91 — «Один индекс в парсере».** Убрать `QStringList blocks` из
  `DetTokensParser`; `page.text` — чистая функция от `page.boxes` (один общий
  `renderBox(box, index)` для `parse` и `rebuildText`); dedup — по самому списку
  боксов. Тесты: существующие кейсы `test_det_parser` + новые на пустой блок и на
  токен без bbox перед дубликатом.
* **ADR 92 — «`DocumentModel` — GUI-thread-only; рендеринг вынесен в
  worker-очередь».** Из `DocumentModel` уходят блокировки и тяжёлый рендер;
  `DjVuDocument`/PDF-рендер живут за одним worker'ом с очередью задач и
  возвращают `QImage` по значению. Это закрывает сразу B4 и C1: снимается
  write-lock из `pageImage()`, исчезает 30-секундный busy-wait в GUI-потоке.
  Изменение поведения: `currentImage()` становится асинхронным (сигнал
  `pageImageReady`), QML показывает превью, пока страница рендерится.
* Следствие этапа: `BoxListModel` становится чистым отражением `DocumentModel`
  (без собственного состояния), что заодно закрывает половину B1 «по-хорошему».

### Этап 3. Отзывчивость (M)

* **C2** — probe (`RuntimeLocator::probeCached`) и проверка целостности
  (`.part`, GGUF/mmproj) уходят в `QtConcurrent::run` с доставкой через
  `QFuture::then(this, …)`; состояние `StartingRuntime` уже есть — остаётся
  показать прогресс. Плюс ADR 93: «никаких блокирующих вызовов >200 мс на
  GUI-потоке» как проверяемое правило (см. 0.1).
* Проверка в тестах: `test_runtime_locator`/`test_ensure_connection` не должны
  требовать GUI-цикла; добавить тест, что resolve при холодном кэше не блокирует
  event loop.
* Побочно снимается риск, который иначе всплыл бы на macOS при первом же
  запуске после очистки кэша.

### Этап 4. Сборка: разделить слои (L)

Порядок обязателен: сначала таргеты, потом разрыв цикла.

1. **ADR 94 — «Слои как таргеты».** Разбить на
   `llocr_core` → `llocr_parsers` / `llocr_models` → `llocr_runtime` / `llocr_app`,
   `target_include_directories(... PUBLIC src)` сужается по слоям. Закрывает A1.
2. Перевести 31 тест на `target_link_libraries(test_x PRIVATE llocr_runtime …)`
   и удалить ручные `target_sources` (241 строка) — закрывает A2. До полного
   перевода оставить `test_app_import` как есть.
3. **ADR 95 — «Разрыв цикла `app ↔ runtime`».** `runtime/` определяется против
   интерфейса настроек (`RuntimeConfigPort`/`SettingsSink`), а не против
   `SettingsStore`; `SettingsStore` опускается на уровень core-порта. Проверка
   сборки: `llocr_runtime` не содержит ни одного `#include "app/…"`.
4. Перенести выбор `ProcessGuard_*` в `cmake/LlocrPlatform.cmake` (сейчас таргет
   один, поэтому это не выражалось).

### Этап 5. Runtime: одна install-машина и один «установленный» (L)

* **ADR 96 — «Один транзакционный движок установки».** `InstallTransaction`
  становится параметризованным артефактом (бинарник/модель): staging-каталог,
  probe/verify, атомарный swap, rollback, **обязательная** `.install.lock` для
  обоих путей. `ModelInstallTransaction` перестаёт писать в конечный каталог
  напрямую; прерванная установка больше не оставляет «пол-каталога», который
  примет следующий скан. Закрывает D3. Тест: инъекция сбоя в середине установки
  модели → состояние «как было».
* **ADR 97 — «Один читатель установленного состояния».** `InstalledState`
  (на базе существующего `RuntimePaths`) — единственный источник для
  `RuntimeInstaller`, `ModelInstaller`, `RuntimeController`; пути пересчитываются
  по сигналу `runtimeRootDirChanged`, блокировка всегда соответствует текущим
  путям; сверка «settings vs index.json vs ФС» в одном месте с политикой
  приоритета. Закрывает D4, попутно убирает замороженные в конструкторе пути.
* **ADR 98 — «Общий `HttpClient`».** Единые timeout/прокси/redirect/auth-policy;
  `DownloadTask` становится самым строгим существующим поведением, остальные
  четыре идиомы — его клиентами. Закрывает C3.
* **D1** — прочитать `owner.json` при старте (рядом с `SingleInstanceGuard`),
  проверить pid + образ + порт, предложить завершить осиротевший процесс —
  обещанное поведение из §5.4 и ADR 30/47. Тест на синтетическом `owner.json`.
* **D7 (побочно)** — прогресс группы считать по собственным задачам группы, а
  не по агрегату менеджера (`DownloadGroup.cpp:34-39`,
  `DownloadManager.cpp:174-192`): сейчас вторая установка размывается первой.

### Этап 6. Конфигурация и расширяемость моделей (L)

* **ADR 99 — «Раздельные пространства id».** `model/recipeId` (адаптер) и
  `model/requestProfileId` (профиль запроса) — разные ключи; значения по
  умолчанию задаются профилями, а не моделью (`RequestProfileStore.cpp:49,95`).
  Закрывает E1.
* Выбор модели — полноценный `QAbstractListModel` (id + display-роль) вместо
  `QStringList` отображаемых имён и round-trip через
  `modelIdToName/indexOf`; `OcrModelFactory::idForDisplayName` перестаёт молча
  возвращать значение по умолчанию (бросает/возвращает optional).
* **ADR 100 — «Транспорт — инъецируемая зависимость».** `IChatTransport`
  (post/encode/parse конверта) вместо приватного `QNetworkAccessManager` и
  статического `endpointUrl()`; `buildRequestBody`/`parseResponse` становятся
  **virtual** (или `protected`, но не `static`), `recognize()` — шаблонный метод;
  `GeneralPurposeModel` теряет скопированный класс и становится ~40 строками.
  Случай `native /completion` и ретраи перестают требовать правки `OcrModel`.
  Закрывает E2. Base64 строить в `QByteArray` (сейчас 3–4 полные копии
  многомегабайтной строки на страницу).
* **ADR 101 — «Один шаблон хранилища профилей».** `ProfileStore<T>` (load/merge/
  draft/save/reset + единая политика `schemaVersion` и миграций); валидатор
  параметров один; путь хранилища — один источник; удаляются `launch/presetId`
  и расхождение таймаутов 240000/120000. Закрывает E3.
* **ADR 102 — «Политика `tr()`».** `core/`, `models/`, `parsers/` не формируют
  пользовательские строки (возвращают коды/структуры), либо используют
  `QCoreApplication::translate` последовательно; устраняется `QObject::tr()`-мусор
  в 58 строках `llocr_ru.ts`; `ExportController` формирует строку статуса в GUI-части
  по enum-статусу (заодно снимает F6).

### Этап 7. QML как слой представления (M)

* **ADR 103 — «Мутации только через контроллер».** Ни один QML не мутирует модель
  напрямую (`Controller.boxModel.removeBox` и подобные сайты исчезают); QML —
  представление + вызовы методов.
* Дублирующиеся правила переезжают в C++: гейт «Launch settings changed —
  restart» (сейчас ручной список из 11 сигналов в `Footer.qml:90-113`),
  гейты шагов мастера (`StepLaunch.qml:13-16`, `StepRuntime.qml:15`,
  `StepBinary.qml:16` — проверяется только непустота пути, тогда как
  `RuntimeController::recomputeConfigValid` проверяет существование файла), порог
  предупреждения о памяти (`StepLaunch.qml:41`).
  Следствие: добавление настройки в C++ больше не может «забыть» поднять баннер.
* `AppController`/`ExportController`/`RecognitionController`/`VerificationQueueController`
  подключаются к `retranslate()`, `exportNameFilters` теряет `CONSTANT` (F4).
* Строково-типизированные `QVariantMap`-роли (`ModelInstaller::roleInstalledInfo`,
  `RuntimeInstaller::installedBuildInfo`) заменяются настоящими list-моделями с
  ролями — по образцу уже успешного `BlockGroupFilterModel`.

---

## 3. Границы плана

* **Не меняем** стек (Qt6/C++/QML/CMake), не трогаем RAG-сервис, не расширяем
  функциональность продукта.
* **H.5 (keychain)** остаётся отложенным, но этап 6 фиксирует точку входа:
  `apiKey` должен приходить из порта настроек, а не из `QSettings` напрямую
  (`SettingsStore.cpp:165-175`).
* **H.4 (watchdog)** остаётся закрытым; `owner.json` из D1 — это дешёвая
  альтернатива помощнику-сторожу, а не его реинкарнация.
* ADR 89 (разбиение stage-4 пайплайна) остаётся отложенным, но блокируется
  этапом 2: после перехода на «текст = функция от боксов» разделение пайплайна
  становится тривиальным.
* `docs/09-local-runtime-plan.md` считается источником истины по функциональности
  managed-runtime; этот план не переписывает его, а фиксирует найденные долги.

## 4. Что имеет смысл делать первым

Если нужен быстрый результат — **этап 1 целиком** (M, ~1 неделя): он закрывает
четыре пользовательски заметных дефекта (удаление блока, проверка не на той
модели, фриз окна на дампе/отладке, осиротевший сервер) почти без
рефакторинга. **Этап 4** даёт наибольшую долгосрочную отдачу, но требует
этапов 0–1 как подушки. Этапы 5 и 6 — самые дорогие и должны идти после 4,
иначе консолидация снова размажется по новым таргетам.
