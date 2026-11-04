# 03. Архитектура

> Английский оригинал: [docs/03-architecture.md](03-architecture.md)

## Слои
```
   llocr (executable: main.cpp + resources + the QML module)
     │
   llocr_app            AppController · Recognition/Export/Verification
     │                  controllers · DocumentModel · models (page/box)
     │                  ProfileStorage is below, in llocr_config
   llocr_runtime        RuntimeController · RuntimeInstaller · ModelInstaller
     │                  ModelInstallTransaction · LlamaServerProcess ·
     │                  DownloadTask/Manager/Group · ArchiveExtractor ·
     │                  InstallTransaction · ReleaseCatalog · ModelCatalog ·
     │                  ModelRegistry · ModelPresetCatalog · ProcessGuard ·
     │                  SingleInstanceGuard · LaunchProfileStore
     │           ┌──────┴───────┐
   llocr_models      llocr_parsers
   OcrModel adapters  IOutputParser · one parser per model · BlockStyle
     │           └──────┬───────┘
   llocr_config       SettingsStore · RequestProfileStore · ProfileStorage ·
     │                RequestParametersModel · RuntimePaths
   llocr_core         OcrResult/OcrRequest/ConnectionConfig · LlamaClient ·
                      RequestProfile · LaunchProfile · ValueParsing
```
Слои C++ — это **цели CMake** (ADR 106), а не соглашение: слой может
использовать только те, с которыми он линкуется. QML-интерфейс располагается
поверх исполняемого файла `llocr` и обращается к C++-сервисам через
синглтоны, регистрируемые в `main.cpp`:

```
┌──────────────────────────────────────────────────────────┐
│                     QML UI (View)                        │
│  Main.qml · Settings/*SettingsWindow.qml (5 windows) ·   │
│  ExportDialog · ServerLogWindow · SetupWizard + Setup/*   │
│  MainWindow/* (Header, ThumbPanel, ImagePanel,            │
│  WorkPanel, MarkdownPreview, Footer) · WindowSettings     │
└──────────────────────────────────────────────────────────┘
```

## Принципы
- **Интерфейс не работает напрямую** с сетью, файлами или разбором — только через бэкенд.
- Бэкенд оркестрирует: принимает запрос от интерфейса → определяет адаптер
  OCR-модели → отправляет запрос через транспорт → разбирает ответ →
  возвращает результат.
- Всё асинхронно (QFuture / QPromise / сигналы); интерфейс не блокируется.
  Навигация по страницам остаётся отзывчивой, **пока распознавание идёт**.

## Ключевая абстракция: OCR-модели (адаптеры)
Каждая поддерживаемая OCR-LLM получает собственный класс-адаптер; общая
механика живёт в абстрактной базе:

```cpp
class OcrModel {                      // src/models/OcrModel.h — the adapter
public:
    explicit OcrModel(QString modelId, QString role = {});
    static std::unique_ptr<OcrModel> create(const QString &modelId, const QString &role = {});
    static QString defaultId();                   // "mineru"

    QString id() const;                           // the profile id, e.g. "mineru"
    QString displayName() const;                  // from the profile
    QList<ModelProfiles::Prompt> promptVariants() const;
    QString defaultParserId() const;              // the profile's parser

    QFuture<OcrResult> recognize(const OcrRequest &request,
                                 const ConnectionConfig &config);
    void abort();
protected:
    static QByteArray buildRequestBody(const OcrRequest &, const QString &imageDataUrl, const QString &role = {});
    static OcrResult parseResponse(const QByteArray &responseData);
};
```

Конфигурация запроса и транспорта разделены:

```cpp
struct OcrRequest {                   // core/OcrRequest.h
    QImage image;
    QString prompt;
    QString modelId;
    QList<RequestParameter> parameters;   // ordered body parameters (request profile)
};

struct ConnectionConfig {             // core/ConnectionConfig.h — transport only
    QString baseUrl;      // e.g. http://localhost:8080
    QString apiKey;       // optional bearer token
    int     timeoutMs;    // per-request timeout
};
```

Задачи — один словарь, объявленный один раз: `core/ConnectionRole.h` хранит
enum `ConnectionRole`, список четырёх задач (`connectionRoles`,
`connectionRoleIds`) и отображение имя↔роль. Диспетчеризация настроек,
состояние рантайма по задачам и QML-таблица задач читают этот список
(ADR 215).

Реализации:
- `OcrModel` (база) реализует `recognize()`/`abort()` как шаблонный метод над
  транспортом: кодирует изображение в base64 data URL, собирает тело запроса
  (`buildRequestBody`, virtual), отправляет POST через `LlamaClient` и
  разбирает ответ (`parseResponse`, virtual). Асинхронность через `QPromise`;
  таймаут на запрос; **`abort()`**, чтобы кнопка Stop в интерфейсе могла
  отменить уже выполняющийся запрос.
- Поведение конкретной модели — это **данные**: `OcrModel::create(modelId,
  role)` читает профиль модели (`resources/profiles/models/<id>.json` —
  промпты, парсер, параметры запроса/запуска), а подклассы по задачам
  (`BlockRecognitionModel`, `DecisionModel`, `LayoutModel`, `FixModel`) несут
  формы ответов на проводе. Адаптеры удалённых моделей ушли в архив вместе
  с ними (ADR 251). Добавить ещё одну LLM = один новый JSON профиля (плюс,
  только если форма ответа новая, один парсер). `defaultParserId()` адаптера —
  то, что Settings → Output использует в режиме «Automatic (model default)»
  (ADR 88).
- `LlamaClient` (`core/LlamaClient.h`) — тонкий транспорт: собирает URL
  `/v1/chat/completions`, отправляет POST с JSON с Bearer-аутентификацией/
  таймаутом/`abort()` и извлекает сообщение об ошибке сервера.

## Ключевая абстракция: разрешённое подключение
Контроллеры запросов по ролям (`ModelRequestController` и его подклассы
`CheckController`, `DecisionController`, `FixController`, `LayoutController`)
**никогда** не имеют дела с режимами, процессами или проверками живости.
Они обращаются к одному асинхронному фасаду и получают готовое
к использованию подключение:

```cpp
struct ResolvedConnection {   // src/runtime/ResolvedConnection.h
    QString baseUrl;    // http://127.0.0.1:<port> (Managed) or external URL
    QString apiKey;     // from settings (External) or empty (Managed)
    QString modelId;    // alias (Managed) or model/name (External)
    int     timeoutMs;
};

class RuntimeController : public QObject {   // src/runtime/RuntimeController.h
    // Callback-based resolve (review 3.3): External invokes synchronously;
    // Managed start → /health → /v1/models → alias, then onResolved() runs.
    // Concurrent callers register their callback and share one in-flight resolve.
    void ensureConnectionReady(std::function<void(const ResolvedConnection &)> onResolved);
    void cancelPendingStart();   // Stop during StartingRuntime
};
```

Кнопка «Check» мастера (самопроверка: запуск → health → `/v1/models` → один
OCR-запрос) выведена через отдельный синглтон `SelfTestController`
(`SelfTest`), который потребляет `ensureConnectionReady()`; окно живого
журнала сервера привязано к синглтону `RuntimeLog`.

Принцип координации ARM: *вся* асинхронная работа, решающая «готово ли
подключение и что это за подключение», принадлежит `RuntimeController`
(ADR 26/32/37). Поток распознавания тогда — чистый конвейер:

```
ensureConnectionReady() → ResolvedConnection → ConnectionConfig → OcrRequest → OcrModel (via LlamaClient)
```

## Модель конфигурации
Настройки модели и парсера живут в `SettingsStore` (сохраняются через
`QSettings`, редактируются в диалоге настроек). Когда начинается запрос
задачи, её подкласс `ModelRequestController` собирает `OcrRequest` (промпт
от активного адаптера OCR-модели + параметры тела запроса из профиля запроса
этой модели, ADR 59) и `ConnectionConfig` (транспорт подключения) из
разрешённого подключения и хранилища настроек.

- `SettingsStore` загружает/сохраняет настройки подключения/модели/парсера/
  интерфейса через `QSettings` (сгруппированные ключи: `provider/*`,
  `model/*`, `parser/*`, `ui/*`, `runtime/*`, `launch/*`, `hf/*`; префикс
  `provider/` — унаследованное хранилище, оставленное для совместимости
  профилей). `model/recipeId` выбирает адаптер OCR-модели (`OcrModel::create`,
  по умолчанию `mineru`). **Промпт** распознавания берётся из
  `promptVariants()` выбранного адаптера — власть переехала от
  `AppController`/пресетов к адаптеру модели (ADR 58).
- `parserId` выбирает стратегию разбора ответа через `ParserFactory`
  (`auto` | `raw` | парсер layout-разметки `mineru-layout`; ключ
  `parser/id`; по умолчанию **`auto`**, что разрешается в
  `defaultParserId()` выбранной модели — ADR 88). Парсер настраивается через
  `ParserOptions` (`keepHeadersAndPageNumbers`, `tablesAsHtml`, `bboxRange`,
  `modelId`), а `IOutputParser::rebuildText()` заново собирает Markdown
  страницы после изменения её структурированных фрагментов.
- В режиме `Managed` *подключение* **вычисляется**, а не настраивается:
  `baseUrl` управляемого сервера (loopback + выбранный порт) и `modelId`
  (это `--alias`) приходят из `ensureConnectionReady()` (ADR 32).
  `model/name` не перезаписывается.

## Слой управляемого рантайма (этапы A–G)
Отдельный слой `src/runtime/` располагается между бэкендом и ОС:

- **RuntimeController** (фасад, экземпляр-синглтон создаётся в `main.cpp`,
  ADR 36) — владеет `ensureConnectionReady()` (External разрешается
  немедленно; Managed: запуск → `/health` → `/v1/models` → алиас),
  `cancelPendingStart()`, жизненным циклом сервера
  (запуск/остановка/перезапуск), `estimateModelMemory()` (H.2) и выставляет
  QML `state`/`busyState`/`statusMessage`/`loadProgressPercent`/`configValid`/
  `lockedOut`. Самопроверка (синглтон `SelfTest`) и живой просмотр журнала
  сервера (синглтон `RuntimeLog`) вынесены в собственные QML-синглтоны
  (review 3.4).
- **RuntimeLocator** — проба (`--version`/`--help`, устойчивый разбор версии),
  `probeCached()` (LRU, H.7). **ServerCapabilities** — белый список сборок +
  разбор `--help` (ADR 41). **ServerLaunchConfig** — сборщик argv +
  `toDisplayCommand()` (секреты никогда не показываются); учитывающая роль
  `fromSettings()` (ADR 72/74).
- **LlamaServerProcess** — `QProcess` только через argv, кольцевой буфер
  журнала (2000 строк) + ротируемый файловый журнал (5 МБ × 3), опрос
  живости, падение → автоперезапуск ≤3 раз за 5 мин, остановка через
  terminate→kill. **ProcessGuard** — платформенная привязка против
  процессов-сирот (Job Object / `PDEATHSIG` / best-effort на macOS +
  `owner.json`, ADR 30).
- **RuntimeInstaller** (этап D) — `ReleaseCatalog` (GitHub Releases +
  `sha256` из тела, TTL 6 ч), `detectPlatform()`/рекомендация бэкенда,
  укреплённый `ArchiveExtractor` (ZIP + `.tar.gz` через zlib; защита от
  zip-bomb/zip-slip, ADR 34), транзакционный `InstallTransaction` (staging →
  проверка → проба → атомарное переименование → фиксация, ADR 39).
  QML-синглтон `RuntimeInstaller`.
- **ModelInstaller** (этап E) — `ModelCatalog` (дерево HF с пагинацией,
  закрепление commit-`sha`, mmproj/многотомность, ADR 29),
  `ModelPresetCatalog` (встроенный `:/models/default-presets.json` +
  пользовательский `models/catalog.json`, ADR 42), `ModelRegistry`
  (index.json, managed против external, защита от удаления). QML-синглтон
  `ModelInstaller`, у которого единственная списковая модель —
  `ModelQuantModel`: строка — модель, её квантизации выбираются на самой
  строке (ADR 122).
- **DownloadTask/DownloadManager** — докачиваемые загрузки
  (`.part`+`.part.meta`, `Range`/`If-Range`, потоковый `sha256`), ≤2
  параллельно, `.part` лежит рядом с целью (ADR 40); редиректы только https,
  с отброшенным `Authorization` при смене хоста (ADR 45).
- **SingleInstanceGuard** — `.instance.lock` (владелец рантайма); когда его
  держит другой экземпляр, операции с управляемым сервером блокируются,
  External продолжает работать (ADR 26). Эксклюзивность установки
  охраняется отдельно файлом `.install.lock` внутри конвейера
  `RuntimeInstaller`, а записи индекса моделей — `.registry.lock` (ADR 46) —
  поэтому второй экземпляр может устанавливать, пока первый работает через
  External.
- **ModelMemoryEstimator** — оценка GGUF-размера + KV-кэша (H.2),
  используется шагом Launch мастера для предупреждения о RAM.

Состояния: `AppBusyState` (`Idle`/`StartingRuntime`/`Recognizing`/
`StoppingRuntime`/`Downloading`/`Installing`) и `RuntimeState`
(`NotConfigured → Stopped → Starting → Ready → Stopping → Stopped`, плюс
`Failed`) выставлены в QML; `canRecognize` следует за состоянием рантайма
(компоненты управляемого рантайма живут в `docs/04-components.md`, решения —
в виде ADR в `docs/07-glossary.md`).

## Строительные блоки бэкенда (реализованы)
- **AppController** — ViewModel и **фасад**, к которому привязывается QML
  (ADR 213). Выставляет QML `busy`, `resultText`, `statusMessage`, `hasImage`,
  `pageCount`, `currentPage`, `hasResult`, `imageRevision`, `docRevision`,
  `currentPageEditable`, `currentPageEdited`, `exportNameFilters`,
  `canRecognize`, `parserNames`, `prompt` и списковые модели `pageModel` /
  `boxModel`. Владеет жизненным циклом страниц
  (открытие/добавление/удаление/перестановка), правками страниц и
  редактированием блоков-изображений. Домены вокруг него живут в собственных
  классах — `BlockInspector`, `ProjectController`, `AvailabilityPresenter`,
  `VerificationQueueController` (с его подклассами `ModelRequestController`),
  `LayoutController`, `ExportController` — и фасад передаёт их состояние
  и сигналы, не меняя QML API. Применяет результаты проверки к документу
  (`applyCheckResultToBox` под блокировкой документа). Все настройки
  подключения/модели/парсера живут в `SettingsStore` (выставлена в QML как
  синглтон `Settings`), а не на контроллере.
- **BlockInspector** — выбранный блок: два слоя текста с их вердиктами,
  различия между ними (одно сравнение на пару текстов, ADR 208), место блока
  в тексте страницы и подсветка выбранного диапазона. Выбор и заметки — его
  собственные; документ принадлежит контроллеру, и встречаются они через
  внедрённые провайдеры.
- **ProjectController** — файл `.llocr`: чтение, запись, флаг занятости,
  путь и восстановление прочитанного документа (ADR 131, ADR 213).
- **AvailabilityPresenter** — матрица доступности: двадцать флагов,
  отпечаток, охраняющий пересчёт (ADR 208), и область действия панели
  инструментов. Флаги меняются только когда один из них переключился, чтобы
  болтовня очереди не заставляла QML пересматривать документ.
- **VerificationQueueController** — владеет последовательной очередью
  проверки (LLM-проверки): список задач (`{page, box}`), прогресс, остановка
  и собственный `CheckController`; блоки-кандидаты собираются из включённых
  типов блоков в `VerificationPromptStore`. Доступ к документу только на
  чтение через внедрённые ссылки; мутации по результатам остаются в
  `AppController` (сигнал `blockChecked`). Прогресс/ошибки передаются
  в свойства `check*` класса `AppController` (ADR 86).
- **ExportController** — владеет конвейером экспорта: сбор страниц, пути
  pandoc/встроенного писателя и поток отрендеренного HTML/PDF с резервными
  писателями; владеет `Exporter` и `ExportRenderer`. Кроп-изображения и флаг
  импорта приходят внедрёнными провайдерами; `exportingChanged`/статус
  передаются через `AppController` (ADR 86).
- **ModelRequestController** — общий каркас контроллеров запросов по задачам
  (`CheckController`, `DecisionController`, `FixController`,
  `LayoutController`): флаг занятости, флаг **остановки**, учитываемый в обеих
  фазах — и разрешения подключения, и выполняющегося запроса, — и обработка
  ошибок. Каждый подкласс владеет своим экземпляром `OcrModel` (создаётся из
  профиля модели задачи) и своим наблюдателем результата; каждый запрос
  получает подключение **только** через
  `RuntimeController::ensureConnectionReady()` (ADR 37), собирает запрос +
  `ConnectionConfig` из разрешённого подключения и передаёт итог.
  Распознавание блоков (роль blockRecognition) идёт через очередь
  перепроверки `VerificationQueueController` — однопроходный цикл по
  страницам удалён (ADR 252).
- **DocumentModel** — хранит загруженные страницы (`DocumentPage`:
  изображение + постраничный `OcrResult` + флаг `recognized`); загружает
  одно/несколько изображений (`QImage`), PDF (`QPdfDocument`) и DjVu
  (`DjVuDocument` / DjVuLibre), поддерживает добавление/удаление/
  перестановку. `DocumentSource` и `sourcePageIndex` идентифицируют источник
  независимо от текущего порядка отображения. Ошибки DjVu распространяются
  через `appendFile()` / `fullImage()`; конвейер OCR остаётся независимым
  от формата. Импорт DjVu разделён на статическую `prepareDjVu()` в воркере
  и `appendPreparedDjVu()` в GUI-потоке под блокировкой документа.
  `PreparedDjVu` владеет страницами и общим декодером, не являющимся
  QObject, поэтому его время жизни не зависит от контроллера.
  `AppController.importing` отделён от `busy` распознавания; обработка
  файлов в очереди сохраняет порядок.
- **PageListModel** — питает ленту миниатюр слева: индекс страницы, флаг
  распознанности, флаг изменённости, флаг дубликата, подсветка текущей
  страницы. Специально не несёт **никаких** рамок.
- **BoxListModel** — нормализованные прямоугольники bbox для оверлея текущей
  страницы; выставляет `updateBoxRect` / `isImageBox` для редактирования
  блоков-изображений. Удаление строк управляется
  `AppController::removeBlock` (ADR 92), а не QML.
- **PageEditStore** — пользовательские правки по страницам, ключ — индекс
  страницы; вычисляет «эффективный» текст (правка перекрывает
  распознавание), обрабатывает откат и перемаппинг после удаления/
  перестановки страниц.
- **OcrImageProvider** — `QQuickImageProvider`, отдающий полную текущую
  страницу (`image://ocr/current`), миниатюры страниц (`image://ocr/page/N`)
  и кропы блоков-изображений (`image://ocr/crop/N`).
- **SettingsStore** — сохраняет настройки (подключение/модель, включая
  параметры DRY, парсер — через `QSettings`, сгруппированные ключи
  `provider/*`, `model/*`, `output/*`, `runtime/*`, `launch/*`, `hf/*`) плюс
  состояние интерфейса (`ui/*`: режим темы, язык, геометрия окна). Выставлена
  в QML как синглтон `Settings`; также читается напрямую `AppController`,
  подклассами `ModelRequestController`, `RuntimeController` и
  `UiController`.
- **UiController** — обработка тем System / Light / Dark (`QML_ELEMENT`).
- **I18n** — переключение языка на лету через Qt Linguist (`qsTr`/`tr` +
  `.ts`); устанавливает переводчики, испускает `languageApplied` для
  `engine.retranslate()`.
- **Exporter** — превращает страницы в TXT / MD напрямую, собирает автономный
  HTML-файл из отрендеренных секций, хранит резервные писатели DOCX (Pandoc)
  и PDF (`QPdfWriter`) и разрешает ссылки `image://ocr/crop/*` (файлы для
  прямых писателей, `data:` URL для пути рендеринга).
- **ExportRenderer** — headless `QWebEnginePage`, исполняющий бандл
  предпросмотра (`qrc:/preview/export.html`: marked + DOMPurify + KaTeX),
  чтобы получить форматированные HTML-секции для экспорта и напечатать PDF
  через `printToPdf`; только UI-поток, на колбэках (ADR 63).
- **WindowSettings** (QML) — сохраняет положение/размер/видимость окна.

## Поток данных (OCR)
```
UI (file selection)
  → AppController (stores pages in DocumentModel)
    → per-task controller (a ModelRequestController subclass) or the
      VerificationQueueController's re-check queue (block recognition)
        → RuntimeController::ensureConnectionReady()   [External: immediate;
            Managed: start → /health → /v1/models → alias]
        → OcrModel::create(modelId, role) from the task's model profile
        → ResolvedConnection → ConnectionConfig + OcrRequest
      → DocumentModel (decode image / render PDF page → QImage)
      → OcrModel.recognize() → LlamaClient.postJson()  [async, cancellable]
      → OutputParser (from settings: auto / raw / the model's own)
    → OcrResult (text + optional normalized boxes + parse notes)
  → AppController (applies the result to the box/page → per-page OcrResult
    + PageEditStore)
→ UI: text panel + bbox overlay + thumbnail "recognized" marker
     + Exporter (on request)
```
