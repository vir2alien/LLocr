# 12. Аудит кода: оверинжиниринг, мёртвый код, дублирование

Проведён 02.10.2026, охват — весь продакшн-код (`src/`, `resources/qml/`, около
29 400 строк C++/QML). Метод: пять параллельных глубоких аудитов (runtime, app,
core/config/models/parsers, QML, кросс-слойный архитектурный) с перекрёстной
верификацией каждой находки grep-ом по `src/`, `resources/qml/`, `tests/`,
`main.cpp` — с учётом Q_PROPERTY/метаобъектных вызовов из QML и вызовов через
указатели. Статус: **открыт** — код на момент аудита не менялся; ниже план
чистки по раундам (§9).

Сводка: ~70 находок — мёртвый код 25, «используется только тестами» ~18,
дублирование 20, оверинжиниринг/переусложнение ~15.

## 1. Если делать только пять вещей

| # | Проблема | Объём |
| --- | --- | --- |
| 1.1 | Три копии стейт-машины установки (`state/busy/progress/statusMessage`) в `RuntimeInstaller` / `ModelInstaller` / `ModelInstallTransaction` + два разных паттерна для одной задачи | ~200 строк |
| 1.2 | Потоковый SHA-256 файла — три копии в слое runtime | ~50 строк |
| 1.3 | ~40% `Theme.qml` мёртво (~25 неиспользуемых шрифтов/цветов/алиасов) | ~40 строк |
| 1.4 | QML-копипаст «шаг визарда ↔ вкладка настроек»: `pickDialog`, редакторы параметров, Language/Theme, чекбоксы output, External-заглушка, блок установки runtime | ~500+ строк |
| 1.5 | Парсер параметров профиля — 4 копии, уже разошедшиеся в деталях | ~150 строк |

## 2. Полностью мёртвый код (потребителя нет нигде)

Проверено grep-ом по всему проекту; ни продакшн, ни тесты не используют.

### C++

- 🔴 `src/app/AppController.h:166` — сигнал `boxesChanged`. Эмитится в 8
  местах, подписчиков нет: QML обновляет боксы через
  `BoxListModel::dataChanged`.
- 🔴 `src/app/OcrImageProvider.cpp:24-37` — ветка `crop/` в `requestImage()`.
  Ни один QML `Image` не грузит `image://ocr/crop/...` (в QML только `current`
  и `page/N`); ссылки `crop/` перехватываются раньше —
  `resolveImagesForPreview()` и экспортёром. WebEngine не имеет доступа к QML
  image provider.
- 🔴 `src/app/VerificationPromptStore.cpp:195-201` — `blockTypes()`.
- 🔴 `src/core/OcrResult.h:25` — `BoundingBox::confidence`: поле не пишется ни
  одним парсером и не читается нигде; единственное упоминание — объявление.
- 🟡 `src/core/ModelProfiles.cpp:292-295` — `setInstance()`: тестового
  инъекционного сеттера нет ни в тестах, ни в продакшне (тесты используют
  `loadFrom`).
- 🟡 `src/core/RequestProfile.h:34` — `kBuiltInPath`: после ADR 123 built-in
  строится из `ModelProfiles`; константа не используется.
- 🟡 `src/config/ProfileStore.h:188` — `m_userFileName`; `:29` —
  `mutableBuiltIn()`; `:31` — `userProfiles()`.
- 🔴 `src/runtime/DownloadTask.h:36,146` + `cpp:426-431` — `State::Paused` и
  `m_pauseRequested`: метода `pause()` не существует, ветка в `onFinished()`
  недостижима.
- 🔴 `src/runtime/LlamaServerProcess.h:104,106` — `m_healthReached`,
  `m_autoRestartScheduled`: только записываются.
- 🟡 `src/runtime/LlamaServerProcess.h:28` — `Options::baseUrl`: не
  устанавливается ни продакшном, ни тестами; ветка в `spawn()` недостижима.
- 🟡 `src/runtime/RuntimeInstaller.cpp:515-518` — `normalizedPath()`;
  `cpp:390` — локальная `installDir`.
- 🟡 `src/runtime/ReleaseAsset.cpp:6-34` — `toJson()`/`fromJson()`:
  release-активы не сериализуются.
- 🔴 `src/runtime/ModelInstallTransaction.h:63,45,42-43` — сигнал
  `installFinished`, аксессоры `installed()`, `pendingTitle()`, `pendingRepo()`:
  данные идут через `installedListReplaced`.
- 🟡 `src/runtime/StagedInstall.h:19` — `finalPath()`.
- 🟡 `src/runtime/DownloadManager.h:43-46` — `totalBytes()`, `receivedBytes()`,
  `speedBytesPerSec()`, `etaSec()`.
- 🟡 `src/runtime/DownloadTask.h:58` — `partPath()`.

### QML

- 🔴 `Theme.qml` — шрифты `footnote…display` (кроме `caption`), размеры
  `subtitleSize/h2Size/h1Size/displaySize`, цвета
  `captionColor…displayColor`, `disabledTextColor`, алиасы
  `overlayOuter/overlayInner`.
- 🟡 `Setup/StepModel.qml:13` — `preparedForCheck`: пишется, не читается.
- 🟢 `ThumbDelegate.qml:22` — `pageIdx`; `Main.qml:24-26` — пустой
  `ButtonGroup themeGroup`; `VerificationPromptsTab.qml:22-26` — `userModified`/
  `draftDirty`; `RuntimeTabInternal.qml:54` — пустой `onInstalledChanged`;
  ~13 неиспользуемых `id` (`portField`, `autoStartBox`, `installStatusLabel`,
  `installProgress`, `splitPagesCheck` в StepOutput и др.).

## 3. API, живущий только на тестах

Часть — осознанные test seams (`setAllowLoopbackHttp`, `setFreeBytesQuery`,
2-арный `probeCached`): пометить или сузить до тестовых сборок. Остальное —
кандидаты на удаление вместе с тестами.

- 🟡 `SettingsStore`: `defaults()`, `defaultsCount()`, `contains()`,
  `resetToDefaults()` (полный сброс не вызывается из QML — окна используют
  точечные reset'ы).
- 🟡 `RequestProfileStore::resetToDefaults()`, `hasUserProfile()`;
  `LaunchProfileStore::hasUserProfile()`, `resetToDefaults()`.
- 🔴 `OcrModelFactory::displayNameForId` / `idForDisplayName` /
  `registeredIds` — хвост ADR 110/129 (продакшн живёт по id). Сама «фабрика»
  без полиморфизма: `create()` = `make_unique<OcrModel>(id)`.
- 🟡 `LlamaServerProcess::startCount()`, `restartCount()`, `isRunning()`;
  параметр `QString *error` у `pickFreePort` (всегда `nullptr`).
- 🟡 `DownloadManager::cancel(int,bool)`; перегрузки `ensureConnectionReady`
  без `context`; `InstallTransaction` — параметр `CommitFn commit` и
  `keepModelStaging` (продакшн всегда передаёт дефолты).
- 🟡 `ReleaseInfo::pickAsset` — параллельная реализация к продакшн-версии
  `RuntimeInstaller::pickAsset`, живёт только в `test_release_catalog`.
- 🟡 `AppController::currentImage()`; сигнал `pageImageReady` (слушатель —
  только `QSignalSpy`); `DocumentModel::loadImage()`;
  `PageListModel::setPageCount()`; `BoxListModel::updateBoxText()`;
  `BlockStyleMap::knowsLabel()`.
- 🟡 `DocumentModel::appendDjVu()` + djvu-ветка `appendFile()` — в продакшне
  недостижимы (`AppController::importNextFile` диспетчеризует `.djvu` раньше
  по асинхронному пути); диспетчеризация по суффиксу продублирована в двух
  местах.
- 🟢 `RequestParametersModel::KindRole/OrderRole` — роли не читаются ни QML,
  ни тестами.
- 🟡 `ServerCapabilities` — пять полей (`supportsFlashAttn`,
  `supportsFlashAttnValue`, `supportsJinja`, `supportsCacheTypeK/V`)
  детектируются из `--help` и сериализуются в дисковый кэш, но продакшн читает
  только `supportsAlias` (`ServerLaunchConfig.cpp:85`).

## 4. Дублирующийся функционал

### 4.1. Кросс-модульное (архитектурно значимое)

1. 🔴 **Три копии поверхности «установка»** — `RuntimeInstaller.h:28-50,142-149`,
   `ModelInstaller.h:28-31,109-114` (+ `cpp:31-34,74-95` — переприём сигналов
   транзакции), `ModelInstallTransaction.h:30-41,104-107`. Плюс идентичные
   строки `"Download failed — check your connection…"`, идентичные `shutdown()`,
   идентичная обвязка `DownloadGroup` и install-lock
   (`RuntimeInstaller.cpp:360-379` ↔ `ModelInstallTransaction.cpp:315-327,497-503`;
   ручной `releaseInstallLock()` на каждом раннем return — источник риска
   утечки лока, напрашивается RAII). Просится общий базовый класс `InstallFlow`.
2. 🟡 **Парсинг `/v1/models` дважды** — `LlamaServerProcess.cpp:199-218`
   (`tryModelsFallback`) и `RuntimeController.cpp:553-595`
   (`fetchManagedModels`, второй собственный NAM). Повторный запрос частично
   оправдан (ADR 61), но парсинг и второй NAM — дублирование;
   `LlamaServerProcess` мог бы отдавать уже полученный modelId.
3. 🔴 **Конвертация `ResolvedConnection` → `ConnectionConfig` — три копии**:
   `RecognitionController.cpp:119-126`, `CheckController.cpp:41-48`,
   `SelfTestController.cpp:59-62`. Просится
   `ResolvedConnection::toConnectionConfig()`.
4. 🟡 **Политика HTTP-редиректов/URL дважды**: `DownloadTask.cpp:447-480` ↔
   `HttpClient.cpp:48-63` — одна и та же политика «https-only + loopback +
   5 hops»; смена политики требует правки двух мест.
5. 🟡 **Идиома `RuntimePaths(settings.runtimeRootDir(),
   settings.runtimeModelsDir())`** собрана вручную минимум в 6 файлах
   (`main.cpp:128`, `InstalledState.cpp:21`, `RuntimeController.cpp:131`,
   `RequestProfileStore.cpp:31`, `LaunchProfileStore.cpp:24`,
   `VerificationPromptStore.cpp:168`, `RuntimeLog.cpp:53`). Статический
   `RuntimePaths::fromSettings()` в config-слое закрыл бы все места.

### 4.2. Внутри слоёв (C++)

6. 🔴 **SHA-256 файла ×3**: `DownloadTask.cpp:483`, `InstallTransaction.cpp:25`,
   `ModelInstallTransaction.cpp:~483`.
7. 🟡 **`normalizedPath` ×2 живых + 1 мёртвая**: `InstalledBuildsModel.cpp:85`
   и `InstalledReconcile.cpp:13` — с разошедшейся семантикой (одна резолвит в
   абсолютный путь, другая нет).
8. 🔴 **Парсер параметров профиля ×4**: `LaunchProfile.cpp:87-163`,
   `ModelProfiles.cpp:18-70` (`readLaunchParameters`), `ModelProfiles.cpp:72-110`
   (`readRequestParameters`), `RequestProfile.cpp:159-203` — один алгоритм
   (name → order → kind → description → dup-check), уже разошедшийся в деталях.
9. 🟡 **Транспортный каркас ×2**: сортировка+вставка параметров и разбор
   `choices[0].message.content` — `OcrModel.cpp:109-147` ↔
   `GeneralPurposeModel.cpp:40-85`.
10. 🟡 **GFM pipe-таблица ×2**: `DetTokenParserBase.cpp:129-150` ↔
    `OtslTable.cpp:174-192` — побайтово совпадающий рендер.
11. 🟡 **`AppController`**: `notifyImportFinished()` ≡ `notifyPageChanged()`;
    LRU-кэш картинок ×2 (thumbnail ↔ preview); паттерн «пометить страницу
    отредактированной» ×4; `recognizeCurrent/recognizeAll` — дословные guard'ы.
12. 🟡 **`DocumentModel::renderFull` ↔ `renderDetached`** (247-288 ↔ 314-363) —
    ~60 строк дублирующейся логики рендера.
13. 🟡 **`VerificationPromptStore`**: `save()` повторно парсит built-in JSON
    (дублирует `loadBuiltIn`); четыре линейных поиска по одному списку
    (`indexOfType/findBlock/promptForType/isTypeEnabled`).
14. 🟢 **`ModelRegistry::load()` ↔ `readIndex()`** — разбор index.json написан
    дважды в одном файле.
15. 🟡 **`BlockGroupFilterModel.cpp:8`** — `kGroupRole = Qt::UserRole + 3`:
    магическое число, дублирующее `VerificationBlocksModel::GroupRole` из
    другого файла; перестановка ролей молча ломает фильтр.
16. 🟢 **`VerificationQueueController::startNextVerify`** — трёхкратный повтор
    skip-блока.

### 4.3. QML

17. 🔴 **`LaunchTab.qml:69-205` ↔ `RequestTab.qml:37-155`** — два почти
    одинаковых редактора таблицы параметров (~120 строк) → кандидат
    `Common/ParamsTableEditor.qml`.
18. 🔴 **`pickDialog` ~60 строк дословно**: `LocationTab.qml:162-221` ↔
    `StepModel.qml:138-196`.
19. 🟡 **`CustomTabButton` + кастомный TabBar** — побайтово:
    `ModelSettingsWindow.qml:131-157` ↔ `VerificationSettingsWindow.qml:151-177`.
20. 🟡 **Хром окон настроек** — 7-8 копий `background` + футера (~30 строк
    каждая).
21. 🟡 **`StepIntro` ↔ `UITab`** (Language/Theme), **`StepOutput` ↔
    `OutputTab`** (чекбоксы + PDF-настройки, help-тексты дословно),
    **`StepRuntime` ↔ `RuntimeTabInternal`** (блок установки llama.cpp;
    ProgressBar дублирует существующий `Common/InstallerProgressRow`).
22. 🟡 **External-заглушка**: `LocationTab.qml:45-95` ↔
    `LaunchTab.qml:216-238` — текст по-разному разбит на `qsTr`-строки
    (расхождение для переводников); предупреждение «The managed runtime cannot
    run this model» встречается 4 раза.
23. 🟢 **`VerificationBlocksTab.qml:227-294`** — три копипастных колонки,
    заголовки продублированы с `BlockNames.groupTitle()`.
24. 🟢 **`Footer.qml:372-406`** — два почти идентичных confirm-диалога.

## 5. Оверинжиниринг

- 🔴 **`QwenGeneralModel` → `GeneralPurposeModel`** — иерархия из одного
  класса, чьи виртуальные `id()/displayName()` мертвы; всё содержимое
  наследника — две константные строки. `OcrModel` уже ушёл к data-driven
  (ADR 88), verifier-адаптер остался в старом стиле. Свёртка тривиальна.
- 🟡 **`DownloadManager` как `QAbstractListModel`** — 8 ролей,
  `begin/endInsertRows`, вытеснение завершённых задач — и ни одного
  QML/C++ читателя `data()`/`roleNames()` в продакшне (прогресс идёт через
  `DownloadGroup`). Упрощение до QObject-очереди убрало бы ~70 строк.
- 🟡 **`RuntimeController::connectEveryChangeSignal`** (`cpp:44-72`) —
  рефлексивный коннект «все `*Changed`-сигналы → слот по имени»: опечатка
  ловится только qWarning'ом в рантайме; явные connect'ы проверялись бы
  компилятором.
- 🟢 **Мелочь**: `exportViaPandoc(extraArgs)` — оба вызова передают `{}`;
  `ResolvedImages` — структура из одного поля; `encodeImageDataUrl(format,
  quality)` — параметры никогда не передаются; `ParserOptions::bboxRange` —
  задаёт только тест (ADR 88 сознательно оставил точкой расширения);
  Q_PROPERTY `activeProfileId/draftProfileId` у `LaunchProfileStore` не
  читаются из QML; `Layout.maximumHeight: Number.POSITIVE_INFINITY` (no-op);
  `textScale` ≡ `platformTextScale`; `rowHeight` у `ModelDownloadList` никогда
  не переопределяется.

## 6. Переусложнённый код

- 🟡 **`ProfileStore`**: конструктор с пустым `builtInPath` всё равно зовёт
  `loadBuiltIn()` → `QFile("").open()` падает → qWarning на каждом старте,
  после чего `setModelProfiles()` перезаписывает список; `putUserProfile` —
  вывернутое условие no-op (`value(id)==profile && contains(id)`).
- 🟢 **`RuntimeController::stopServer`** — `setBusyState(StoppingRuntime)→…→
  setBusyState(Idle)` в одном кадре: промежуточное состояние наружу не видно,
  `Idle` при ещё живом сервере преждевременен.
- 🟡 **`ModelProfiles::promptsFor(profiles,…)`** игнорирует параметр
  `profiles` и читает глобальный синглтон — латентная несогласованность.
- 🟢 **Мелочь**: `Main.qml:97` — identity-копия `drop.urls.map(u => u)`;
  `StepLaunch` — `gib()` ≡ `giText()`; `ServerLogWindow` — разрыв binding'а +
  восстановление через `Qt.binding()` ×2; `InstalledReconcile::selectsPath` —
  неиспользуемый параметр `input`.

## 7. Что проверено и чисто

- `HttpClient` используется везде для GET-логики, `LlamaClient` — отдельная
  роль (async POST), дублирования нет.
- `StagedInstall` действительно общий для обеих транзакций;
  `ProfileStorage`/`ProfileStore<T>` — общий код, сторы — тонкие фасады.
- `RuntimePaths`/`RuntimeLocator`/`InstalledState` не пересекаются по
  ответственности; лишних слоёв среди посредников нет.
- `parsers` — самый качественный слой (настоящий полиморфизм,
  `ValueParsing`/`BlockStyleMap` переиспользуются).
- `RequestParametersModel` ↔ `LaunchParametersModel` — параллельные структуры,
  но осознанный компромисс слой-изоляции (config vs runtime), не «плохой»
  копипаст.

## 8. Общий вывод

Архитектура дисциплинированная (слои соблюдаются, транзакционность/локи/кэши
продуманы, тесты покрывают контракт). Долг скапливается систематически в двух
местах: **(а) стык «runtime-установка ↔ QML-синглтон»** — поверхность
state/progress/lock копируется на каждую сущность (три копии), и **(б) стык
«шаг визарда ↔ вкладка настроек»** в QML — пары файлов копируют друг друга
(~500+ строк). Плюс хвосты рефакторингов ADR 88/110/129 (осиротевшие API
модельного слоя, мёртвые виртуалки, test-only инвокаблы).

## 9. План чистки по раундам

Каждый раунд закрывается прогоном `scripts/check.sh` (build + ctest +
clang-format + qmllint). Правило приоритетов: сначала 🔴, затем 🟡; 🟢 — по
мере касания файлов. Тесты, защищающие удаляемый test-only API, удаляются или
переписываются вместе с ним; осознанные test seams (`setAllowLoopbackHttp`,
`setFreeBytesQuery`, 2-арный `probeCached`) — помечаются и остаются.

### Раунд 1 — безопасное удаление мёртвого кода

Всё из §2; изменения не затрагивают поведение. Затрагиваемые тесты:
`test_server_process` (если удаляются `startCount`/`restartCount` — нет, они
test-only, остаются до раунда 4), `test_archive_extractor` не затронут.
Проверка: зелёный `scripts/check.sh` без правки тестов (кроме случаев из §2,
где тестов-потребителей нет по определению).

### Раунд 2 — внутрислоевые дедупликации C++

- Общий хелпер SHA-256 (§4.2.6) в runtime-слое.
- Единый `normalizedPath` (§4.2.7).
- Общий парсер параметров профиля (§4.2.8) — самый тонкий шаг: копии уже
  разошлись, сначала зафиксировать текущее поведение тестами.
- `ResolvedConnection::toConnectionConfig()` (§4.1.3) — тривиально.
- `AppController`: `notifyImportFinished` → `notifyPageChanged`, метод
  `markPageEdited(int)`, общая LRU-утилита (§4.2.11).
- `VerificationPromptStore`: `save()` через `loadBuiltIn`-переиспользование,
  `promptForType`/`isTypeEnabled` через `findBlock` (§4.2.13).
- `kGroupRole` → публичная константа роли из `VerificationBlocksModel`
  (§4.2.15).

### Раунд 3 — QML-компоненты

- `Common/ParamsTableEditor.qml` (§4.3.17).
- Общий `pickDialog` (§4.3.18) — заодно убрать мёртвый `preparedForCheck`.
- `Common/CustomTabButton.qml` + общий хром окна настроек (§4.3.19-20).
- Пары «визард ↔ настройки»: Language/Theme, output-чекбоксы, external-заглушка
  (§4.3.21-22) — вынести блоки в переиспользуемые компоненты; qsTr-строки
  объединить.
- Чистка `Theme.qml` (§2 QML) — если не сделана в раунде 1.

### Раунд 4 — test-only API и оверинжиниринг

- Решение по каждому пункту §3: удалить (с тестом) или пометить как test seam.
- Свёртка `QwenGeneralModel` в конкретный `GeneralPurposeModel` (§5).
- Упрощение `DownloadManager` до QObject-очереди (§5) — модельный контракт
  `test_download_manager` переписать под новую поверхность.
- `OcrModelFactory` → простая функция/`defaultId()` (§3).
- Явные connect'ы вместо `connectEveryChangeSignal` (§5).

### Раунд 5 — архитектурные дедупликации

- Базовый класс `InstallFlow` для `RuntimeInstaller`/`ModelInstaller`/
  `ModelInstallTransaction` + RAII lock-guard (§4.1.1, §4.1.8-обвязка).
- Общий проб `/v1/models` / передача modelId из `LlamaServerProcess` (§4.1.2).
- Единая политика редиректов в `HttpClient` (§4.1.4).
- `RuntimePaths::fromSettings()` (§4.1.5).
- `ProfileStore`: убрать пустой `loadBuiltIn()` в конструкторе (qWarning на
  старте, §6).

Раунды 2-5 фиксировать ADR-ами по мере выполнения (по образцу раундов 1-5
предыдущего плана, ADR 82-88).
