# 09. Local Runtime: автозапуск llama.cpp и управление моделями (v2)

План работ для агентов. Статус: **A выполнена** — каркас, режимы, resolver, SettingsStore-группы/миграция, RuntimePaths, ServerLaunchConfig, сакелет RuntimeController (External), RecognitionController через `ensureConnectionReady()`, SingleInstanceGuard. Далее по порядку — **B**.

---

## 0. Цель

Убрать требование «пользователь сам поднял сервер». Приложение должно уметь:

1. Найти/выбрать бинарник `llama-server` вручную.
2. Скачать сборку llama.cpp с GitHub Releases под текущую ОС/архитектуру/бэкенд.
3. Скачать GGUF-модель (+ `mmproj` для мультимодалок) с Hugging Face.
4. Собрать командную строку запуска из редактируемых параметров и поднять сервер как дочерний процесс.
5. Провести пользователя через это в **мастере первого запуска**.
6. Сохранить всё в `SettingsStore` и дать вкладки в Settings.

**Инварианты, которые нельзя нарушать:**

- Существующий сценарий «внешний сервер / хостед API» продолжает работать без изменений.
- `OpenAiProvider` остаётся transport-only и ничего не знает про `QProcess`.
- Приложение **никогда** не меняет `provider/mode` автоматически.
- Managed-сервер слушает **только loopback**.

### 0.1. Границы (что НЕ входит)

- ⛔ Автоматический attach к чужому процессу на занятом порту (вырезано, см. ADR 33).
- ⛔ Поддержка `.tar.gz` (llama.cpp публикует `.zip` для всех платформ; см. ADR 34).
- ⛔ Автоматическое снятие macOS quarantine через `xattr` (см. ADR 35).
- ⛔ Управление Ollama / LM Studio — они остаются в `External`.
- ⛔ Хранение секретов в системном кейчейне — отдельная задача для всего приложения.

---

## 1. Архитектура

### 1.1. Режим подключения

`ConnectionMode`:

| Режим | Поведение |
| --- | --- |
| `External` (**дефолт**, в т.ч. для всех существующих профилей) | `ProviderConfig.baseUrl` указывает на чужой сервер. Приложение ничего не запускает. |
| `Managed` | Приложение стартует `llama-server` на loopback, ждёт готовности, само формирует `baseUrl` и `modelId`, гасит процесс при выходе. |

Переключатель — **Settings → Connection**. Всё ниже активно только в `Managed`.

### 1.2. Владелец и lifetime

`RuntimeController` создаётся **один раз в `main.cpp`**, до `QQmlApplicationEngine`,
и живёт до конца работы приложения. QML **не может** его инстанцировать.

```
main.cpp
 └── RuntimeController  (единственный экземпляр, owner)
       ├── RuntimePaths
       ├── RuntimeLocator
       ├── LlamaServerProcess
       ├── DownloadManager
       ├── ReleaseCatalog
       ├── ModelCatalog
       ├── ModelRegistry
       └── ArchiveExtractor

     AppController(&runtimeController)
       └── RecognitionController(&runtimeController)   // только ensureConnectionReady()
```

Регистрация в QML:

```cpp
// RuntimeController.h
class RuntimeController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON            // экземпляр задаётся извне
    // ...
public:
    static RuntimeController* create(QQmlEngine*, QJSEngine*);  // возвращает already-set instance
};

// main.cpp
RuntimeController runtime(&settingsStore);
qmlRegisterSingletonInstance("LLocr", 1, 0, "Runtime", &runtime);
```

> Требование: `qmlRegisterSingletonInstance` вызывается **до** `engine.load()`.
> Запрещено `new RuntimeController` где-либо, кроме `main.cpp`.
> Тест `test_runtime_lifetime` проверяет, что QML-обращение к `Runtime` возвращает
> тот же указатель, что передан в `AppController`.

### 1.3. Единая точка готовности подключения

`RecognitionController` **не** знает про режимы, процессы и health-check.
Он вызывает один асинхронный метод и получает готовую тройку:

```cpp
struct ResolvedConnection {
    QString baseUrl;   // http://127.0.0.1:<actualPort> или внешний URL
    QString apiKey;    // из настроек (External) или пусто (Managed)
    QString modelId;   // alias (Managed) или model/name (External)
    int     timeoutMs;
};

class RuntimeController {
public:
    // External:  немедленно резолвится из SettingsStore.
    // Managed:   при необходимости стартует процесс, ждёт /health, резолвится.
    //            Повторные вызовы во время старта возвращают тот же future.
    QFuture<ResolvedConnection> ensureConnectionReady();

    void cancelPendingStart();   // для кнопки Stop во время StartingRuntime
};
```

`RecognitionController::run()`:

```
ensureConnectionReady()
  → ResolvedConnection
  → ProviderConfig{baseUrl, apiKey, timeoutMs}
  → OcrRequest{... modelId ...}
  → OpenAiProvider::recognize()
```

Ошибка резолва → тот же путь обработки, что сетевая ошибка провайдера,
с человекочитаемым сообщением из §7.5.

### 1.4. Состояния

Одного `busy` недостаточно. Вводим `AppBusyState` (эксклюзивные):

```
Idle
StartingRuntime     // сервер поднимается / грузит модель
Recognizing
StoppingRuntime
Downloading         // модель или runtime
Installing          // распаковка/верификация
```

И отдельно `RuntimeState` (состояние процесса):

```
NotConfigured → Stopped → Starting → Ready → Stopping → Stopped
                              ↓                  ↓
                           Failed ←──────────────┘
```

`canRecognize`:

```
canRecognize =
    documentLoaded
 && busyState == Idle
 && (
      mode == External
      || runtime.state == Ready
      || (runtime.state == Stopped && runtime.configValid
          && (autoStart || startOnDemand))
    )
```

`runtime.configValid` = бинарник валиден (probe прошёл) **и** файл модели существует.

---

## 2. Файлы к созданию

```
src/runtime/
├── ConnectionMode.h            # enum class: External | Managed
├── ResolvedConnection.h        # baseUrl + apiKey + modelId + timeoutMs
├── RuntimeState.h              # RuntimeState, AppBusyState
├── RuntimeController.h/.cpp    # фасад, ensureConnectionReady(), runSelfTest()
├── RuntimePaths.h/.cpp         # каталоги данных, install/staging
├── RuntimeLocator.h/.cpp       # поиск/валидация/probe llama-server
├── ServerCapabilities.h/.cpp   # какие флаги поддерживает данный бинарник
├── ServerLaunchConfig.h/.cpp   # параметры + argv (с учётом capabilities)
├── LlamaServerProcess.h/.cpp   # QProcess + health + лог + shutdown
├── ProcessGuard.h              # платформенная привязка к жизни родителя
├── ProcessGuard_win.cpp        # Job Object
├── ProcessGuard_linux.cpp      # PR_SET_PDEATHSIG
├── ProcessGuard_mac.cpp        # best-effort (см. §5.4)
├── ReleaseCatalog.h/.cpp       # GitHub Releases + sha256 из тела релиза
├── ReleaseAsset.h
├── ModelCatalog.h/.cpp         # Hugging Face API, pagination, revision pinning
├── ModelPreset.h               # точная пара model/mmproj/parser/prompt
├── ModelRegistry.h/.cpp        # локальный реестр, managed vs external
├── DownloadManager.h/.cpp      # очередь, прогресс
├── DownloadTask.h/.cpp         # resume, ETag/If-Range, sha256
├── ArchiveExtractor.h/.cpp     # ZIP only, hardened
├── InstallTransaction.h/.cpp   # staging → verify → atomic rename
└── SingleInstanceGuard.h/.cpp  # QLockFile

third_party/miniz/              # vendored, MIT

resources/models/
└── default-presets.json        # встроенный каталог (read-only)

resources/qml/
├── SetupWizard.qml
├── Setup/
│   ├── StepWelcome.qml
│   ├── StepRuntime.qml
│   ├── StepModel.qml
│   ├── StepLaunch.qml
│   └── StepDone.qml
├── Settings/
│   ├── RuntimeTab.qml
│   ├── ModelsTab.qml
│   └── LaunchTab.qml
└── ServerLogWindow.qml
```

Обновить: `SettingsDialog.qml`, `MainWindow/Footer.qml`, `main.cpp`,
`SettingsStore.*`, `RecognitionController.*`, `AppController.*`,
`resources/i18n/llocr_ru.ts`, `CMakeLists.txt`, `tests/CMakeLists.txt`.

---

## 3. Каталоги данных

```
<AppData>/LLocr/
├── runtime/
│   ├── .install.lock                       # QLockFile
│   ├── staging/<uuid>/                     # временная распаковка
│   └── llama.cpp-<build>-<backend>-<os>-<arch>/
│       └── llama-server[.exe]
├── models/
│   ├── .registry.lock
│   ├── index.json                          # ModelRegistry (QSaveFile)
│   ├── catalog.json                        # пользовательский каталог пресетов
│   └── <org>__<repo>/
│       ├── model-Q4_K_M.gguf
│       ├── model-Q4_K_M.gguf.part          # ⚠ .part рядом с целью, не в downloads/
│       └── mmproj-F16.gguf
├── cache/
│   └── releases.json                       # TTL 6 ч
└── logs/
    └── llama-server.log                    # ротация, 5 МБ × 3
```

**Правило §3.1:** файл `.part` **всегда** создаётся в целевом каталоге, а не в
общем `downloads/`. Иначе финальный `rename` пересекает границу файловой
системы (модели часто держат на внешнем диске) и перестаёт быть атомарным.
Перед стартом загрузки — `QStorageInfo` целевого тома.

`runtime/rootDir` и `runtime/modelsDir` переопределяемы. При смене пути файлы
**не переносятся**; UI показывает предупреждение и предлагает пересканировать
реестр.

---

## 4. Настройки (`SettingsStore`)

### 4.1. Ключи

| Ключ | Тип | Дефолт | Комментарий |
| --- | --- | --- | --- |
| `provider/mode` | string | `external` | `external` \| `managed` |
| `provider/lastExternalBaseUrl` | string | «» | сохраняется при уходе в Managed |
| `runtime/setupVersion` | int | `0` | версия пройденного мастера; текущая — `1` |
| `runtime/setupDismissed` | bool | `false` | пользователь закрыл мастер |
| `runtime/serverPath` | string | «» | абсолютный путь к бинарнику |
| `runtime/serverPathIsManaged` | bool | `false` | скачан нами или указан вручную |
| `runtime/rootDir` | string | AppData | |
| `runtime/modelsDir` | string | AppData/models | |
| `runtime/backend` | string | «» | `cpu`/`cuda`/`vulkan`/`metal`/`hip`/`sycl` |
| `runtime/installedBuild` | string | «» | напр. `b10594` |
| `runtime/autoStart` | bool | **`false`** | ⚠ дефолт изменён (см. §4.3) |
| `runtime/startOnDemand` | bool | `true` | |
| `runtime/stopOnExit` | bool | `true` | |
| `runtime/autoRestart` | bool | `true` | ≤3 попыток / 5 мин |
| `runtime/startupTimeoutMs` | int | `180000` | |
| `runtime/checkUpdates` | bool | `false` | opt-in |
| `runtime/allowNonLoopback` | bool | `false` | advanced, см. §7.4 |
| `launch/presetId` | string | «» | id пресета из каталога |
| `launch/modelPath` | string | «» | |
| `launch/mmprojPath` | string | «» | |
| `launch/modelAlias` | string | `llocr-local` | → `--alias`, см. §4.2 |
| `launch/host` | string | `127.0.0.1` | |
| `launch/port` | int | `0` | `0` = автоподбор |
| `launch/ctxSize` | int | `8192` | |
| `launch/gpuLayers` | int | `-1` | |
| `launch/threads` | int | `0` | `0` ⇒ не передавать |
| `launch/batchSize` | int | `0` | |
| `launch/parallel` | int | `1` | |
| `launch/flashAttn` | string | `off` | |
| `launch/cacheTypeK` / `cacheTypeV` | string | «» | |
| `launch/noMmap` | bool | `false` | |
| `launch/jinja` | bool | `false` | |
| `launch/extraArgs` | string | «» | shell-подобный парсинг с кавычками |
| `hf/token` | string | «» | ⚠ plaintext, см. §7.6 |

### 4.2. Связь `modelPath` ↔ OpenAI-поле `model`

Сервер всегда стартует с `--alias <launch/modelAlias>` (дефолт `llocr-local`).
После `Ready` контроллер запрашивает `GET /v1/models` и проверяет, что alias
присутствует в ответе.

- `ResolvedConnection.modelId` в `Managed` = `launch/modelAlias`.
- Поле **Settings → Model → model name** в `Managed` становится **read-only**
  и показывает alias с подписью «определяется запущенным сервером».
  Значение `model/name` при этом **не перезаписывается** — оно сохраняется
  для возврата в `External`.
- Если `--alias` не поддерживается бинарником (см. §5.3), фолбэк:
  взять первый `id` из `/v1/models`.

### 4.3. Семантика автозапуска

| `autoStart` | `startOnDemand` | Поведение |
| --- | --- | --- |
| `false` | `true` | **дефолт.** Старт при первом «Распознать» |
| `true` | — | Старт после инициализации приложения |
| `false` | `false` | Только ручной Start в Settings |

Дополнительные правила:

- Автозапуск **не выполняется**, если `runtime.configValid == false`.
- Автозапуск **не выполняется**, если `setupVersion == 0`.
- `autoStart` предлагается включить только в мастере, явным чекбоксом
  с указанием размера модели.

> Обоснование дефолта `false`: 8-гигабайтная модель, загруженная в VRAM при
> каждом открытии GUI, — неприемлемое поведение для приложения, где OCR
> запускается эпизодически.

### 4.4. Миграция существующих профилей

```
if (!settings.contains("runtime/setupVersion")) {
    bool looksConfigured = settings.contains("provider/baseUrl")
                        && !settings.value("provider/baseUrl").toString().isEmpty();
    settings.setValue("provider/mode", "external");
    settings.setValue("runtime/setupVersion", looksConfigured ? 1 : 0);
}
```

**Правила показа мастера:**

- Показывать автоматически ⟺ `setupVersion == 0 && !setupDismissed`.
- ⛔ **Никаких сетевых проб при старте** для принятия этого решения.
  Внешний endpoint может быть за VPN, выключен или требовать ключ — это не
  повод считать профиль ненастроенным.
- Закрытие крестиком → `setupDismissed = true`, `setupVersion` не меняется.
  Мастер доступен из `Settings → Runtime → «Запустить мастер»`.
- Успешное прохождение → `setupVersion = 1`, `setupDismissed = false`.
- Будущие версии мастера поднимают константу и могут показать только
  недостающие шаги.

---

## 5. Этапы работ

Порядок: **A → B → C → (D ∥ E) → G-core → F → G-UI → H**

Мастер (F) переставлен **после** ядра интеграции (G-core), потому что шаг
«Проверить» требует рабочего пути «старт → health → пробный запрос».

---

### Stage A — Каркас, режим, resolver ✅ (выполнен)

**Задачи**

- [x] 1. `ConnectionMode`, `RuntimeState`, `AppBusyState`, `ResolvedConnection`.
- [x] 2. `SettingsStore`: группы `runtime/*`, `launch/*`, `hf/*`, Q_PROPERTY, сигналы,
   **миграция §4.4**.
- [x] 3. `RuntimePaths`: создание каталогов, `stagingDir()`, `installDir(tag)`,
   `modelDir(repo)`.
- [x] 4. `ServerLaunchConfig` + `QStringList toArguments(const ServerCapabilities&)`
   + `QString toDisplayCommand()` (для превью в UI, с экранированием).
   (Минимальный `ServerCapabilities` введён здесь как value-тип; полный probe — Stage B.)
- [x] 5. `RuntimeController` — скелет: свойства `state`, `busyState`, `statusMessage`,
   `configValid`; `ensureConnectionReady()` реализован **только для `External`**
   (возвращает готовый future из `SettingsStore`).
- [x] 6. `RecognitionController` переводится на `ensureConnectionReady()` —
   поведение в `External` не меняется.
- [x] 7. `SingleInstanceGuard` (`QLockFile` в `<AppData>/LLocr/.instance.lock`):
   при занятом локе — предупреждение и запрет `Managed`-операций
   (загрузки/установка/старт), `External` работает как обычно.

**Приёмка**

- [x] `External` неотличим от текущего поведения (регресс-прогон всех тестов).
- [x] Профиль со старыми ключами не показывает мастер (миграция даёт `setupVersion == 1`).
- [x] Чистый профиль даёт `setupVersion == 0`.

**Тесты**

- [x] `test_launch_config` — argv для разных конфигов, пропуск дефолтных значений,
  парсинг `extraArgs` с кавычками и пробелами, пути с пробелами/кириллицей.
- [x] `test_settings_store` (расширен) — миграция, дефолты, отсутствие
  автопереключения режима.
- [x] `test_runtime_lifetime` — единственность экземпляра.

---

### Stage B — Бинарник и процесс

**Задачи**

1. **`RuntimeLocator`**
   - `probe(path)` → `ProbeResult{ok, version, build, capabilities, error}`.
     Запуск `--version`, при неудаче — `--help`; таймаут 10 с.
     Парсер версии **терпимый**: несколько регулярок + фолбэк «версия неизвестна,
     но бинарник отвечает».
   - ⛔ **Не** проверять, что имя файла содержит `llama-server` — это ломает
     переименованные и обёрнутые бинарники. Критерий валидности — успешный probe.
   - `autoDiscover()` — `QStandardPaths::findExecutable`, Homebrew, `/usr/local/bin`,
     `%LOCALAPPDATA%`.
   - `chmod +x` — **только** для файлов внутри нашего `runtime/`; для файла,
     выбранного пользователем вручную, — запрос подтверждения.

2. **`ServerCapabilities`** (§5.3 ниже) — определение поддерживаемых флагов.

3. **`LlamaServerProcess`**
   - `QProcess`, только `setProgram` + `setArguments`, никогда не shell.
   - `setWorkingDirectory` = каталог бинарника (нужно для соседних DLL/dylib).
   - Windows: `CREATE_NO_WINDOW` через `setCreateProcessArgumentsModifier`.
   - stdout/stderr → кольцевой буфер (2000 строк) + файл лога с ротацией.
   - Health: `GET {baseUrl}/health` каждые 500 мс до `startupTimeoutMs`;
     фолбэк `/v1/models`. Прогресс загрузки модели парсится из stderr.
   - `stop()`: `terminate()` → 5 с → `kill()`.
   - `crashed` → автоперезапуск ≤3 раз / 5 мин, затем `Failed`.

4. **Порт**
   - `launch/port == 0` → `QTcpServer::listen(QHostAddress::LocalHost, 0)`,
     забрать порт, закрыть, передать в argv. Ретрай ×3 при гонке.
   - Фиксированный порт занят → **ошибка** с предложением сменить порт или
     переключиться в `External`.
   - ⛔ Никакого автоматического attach (ADR 33).

5. **`ProcessGuard`** — §5.4.

6. **Shutdown** — §5.5.

**UI:** `Settings → Runtime` — путь + «Обзор» + «Определить автоматически»,
статус probe, Start/Stop/Restart, «Показать лог».

**Приёмка**

- Ручной бинарник + локальный GGUF ⇒ сквозное распознавание.
- Нормальное закрытие приложения не оставляет процесс на всех 3 ОС.
- `kill -9` GUI: Windows/Linux — процесс умирает; macOS — задокументированное
  best-effort (§5.4).

**Тесты**

- `test_server_process` — мок-бинарь (тестовый таргет, поднимает `QTcpServer`
  с `/health`, печатает строки, умеет падать по команде): переходы состояний,
  таймаут, стоп, рестарт, кольцевой лог.
- `test_runtime_locator` — probe на моке, терпимый парсинг версии,
  бинарник с нестандартным именем принимается.

#### 5.3. Capability detection

CLI llama.cpp меняется между сборками (например, `--flash-attn` перешёл
от boolean к `on|off|auto`). Слепая сборка argv ломает совместимость.

Схема (гибридная, не только `--help`):

1. **Минимальная поддерживаемая версия: `b4000`.** Ниже — отказ с явным
   сообщением. Константа `kMinimumSupportedBuild` фиксируется в коде и в этом
   документе.
2. Из probe извлекается build number → таблица известных capability-профилей
   (`allowlist` по диапазонам build).
3. Дополнительно парсится вывод `--help` — как уточнение, не как единственный
   источник.
4. Итог кэшируется в `<cache>/capabilities-<sha1(path+mtime)>.json`.
5. Неподдерживаемые флаги **не передаются**; соответствующие поля в UI
   становятся disabled с подсказкой «не поддерживается вашей сборкой».
6. Если при старте процесс завершился с `error: invalid argument` /
   `unknown argument`, флаг помечается неподдерживаемым и делается **один**
   автоматический ретрай без него, с записью в лог.

**Тест:** `test_capabilities` — фикстуры вывода `--help` нескольких сборок,
проверка, что для старой сборки `--flash-attn on` не генерируется.

#### 5.4. Гарантии по процессам-сиротам (честная формулировка)

| ОС | Механизм | Гарантия |
| --- | --- | --- |
| Windows | Job Object + `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` | **Сильная** — включая аварийное завершение GUI |
| Linux | `prctl(PR_SET_PDEATHSIG, SIGTERM)` в `setChildProcessModifier` | **Сильная** |
| macOS | нет аналога `PDEATHSIG` | **Best-effort** — см. ниже |

**macOS, принятое решение для MVP:**

- Штатный выход и `SIGTERM`/`SIGINT` обрабатываются → сервер гасится.
- При `SIGKILL` GUI сервер **может пережить родителя**.
- Митигация без helper-процесса:
  - PID и порт записываются в `<AppData>/LLocr/runtime/owner.json`
    (`QSaveFile`);
  - при следующем старте LLocr обнаруживает «осиротевший» процесс, проверяет
    PID + имя образа + порт и **предлагает пользователю** его завершить
    (не делает это молча);
  - в лог и в UI выводится предупреждение.
- Полноценный watchdog-helper (следит за родителем через `kqueue`/`NOTE_EXIT`)
  вынесен в **Stage H** как опциональное улучшение.

> В документации и в UI формулировка: «корректное завершение гарантировано;
> при аварийном завершении на macOS возможен остаточный процесс — LLocr
> обнаружит его при следующем запуске».

Дополнительно: `QProcess::terminate()` гасит только непосредственный процесс.
`llama-server` потомков не порождает, но `ProcessGuard` на Windows всё равно
покрывает всё дерево через Job Object.

#### 5.5. Порядок завершения

`aboutToQuit` выполняется уже при остановленном event loop, поэтому
асинхронное ожидание там недопустимо.

```
QCoreApplication::aboutToQuit
  → RuntimeController::shutdownSync(timeoutMs = 8000)
       process.terminate();
       process.waitForFinished(5000);      // блокирующе — здесь это корректно
       if (still running) process.kill();
       process.waitForFinished(2000);
       owner.json удалить
```

- Отменить все активные загрузки (`.part` сохраняются).
- Флаг `stopOnExit == false` → процесс намеренно оставляется, `owner.json`
  сохраняется, при следующем старте предлагается переиспользовать или убить.

---

### Stage C — Загрузчик

**Задачи**

1. **`DownloadTask`**
   - Целевой файл: `<targetDir>/<name>.part` → `<targetDir>/<name>` (§3.1).
   - Проверка свободного места на **целевом** томе до старта.
   - Потоковая запись, `flush` + `fsync` перед финальным `rename`.
   - **Resume (строгий протокол):**
     - сохраняются `ETag` и/или `Last-Modified` в `<name>.part.meta` (JSON, `QSaveFile`);
     - при возобновлении: `Range: bytes=<size>-` **и** `If-Range: <etag|date>`;
     - ответ `206` → проверить `Content-Range`: единица `bytes`, начало
       диапазона **равно** размеру `.part`, полный размер совпадает с ожидаемым;
     - ответ `200` (сервер проигнорировал Range или файл изменился) →
       начать с нуля, `.part` обнулить;
     - несовпадение хоть чего-то → полная перезакачка, без «доверия по размеру».
   - **SHA-256:** `QCryptographicHash` не сериализуется между запусками,
     поэтому при возобновлении хэш считается **перечитыванием `.part` с диска**
     чанками, затем поток продолжается. Вычисление — потоковое, в рабочем
     потоке загрузки (формулировка «параллельная sha256» из v1 некорректна).
   - Санитизация имени файла: запрет разделителей пути, `..`, control-символов,
     зарезервированных Windows-имён (`CON`, `NUL`, …); коллизии разрешаются
     суффиксом.

2. **Семантика управления (было противоречиво в v1):**

| Событие | `.part` | `.part.meta` |
| --- | --- | --- |
| Pause (пользователь) | сохраняется | сохраняется |
| Сетевой сбой / выход из приложения | сохраняется | сохраняется |
| Cancel (пользователь) | диалог: «удалить частичную загрузку?» | по тому же выбору |
| Провал sha256 | удаляется всегда | удаляется |

3. **`DownloadManager`** — очередь, ≤2 параллельно, агрегированный прогресс,
   `QAbstractListModel` (name, targetDir, size, received, speed, eta, state, error).

4. **Редиректы (§7.3)** — политика не слабее `NoLessSafeRedirectPolicy`
   (дефолт Qt6), плюс **собственная проверка**: только `https`, ≤5 переходов,
   и **снятие `Authorization` при смене host** — иначе HF-токен утечёт на CDN.

5. Прокси: `QNetworkProxyFactory::useSystemConfiguration()`.

**Приёмка**

- Обрыв на 4 ГБ → продолжение с места разрыва, итоговый sha256 верен.
- Подмена файла на сервере (другой ETag) → полная перезакачка, не склейка.
- Bad sha256 → файл удалён, внятная ошибка.

**Тест:** `test_download_manager` — локальный `QTcpServer`: `Range`/`If-Range`,
некорректный `Content-Range`, смена ETag, отмена, hash mismatch, нехватка места.
Реальная сеть не используется.

---

### Stage D — Установка llama.cpp

**Задачи**

1. **`ReleaseCatalog`**
   - `GET https://api.github.com/repos/ggml-org/llama.cpp/releases?per_page=10`,
     заголовки `Accept: application/vnd.github+json`, `User-Agent: LLocr/<ver>`.
   - Rate limit 60 req/ч для анонимов → кэш `<cache>/releases.json`, TTL 6 ч,
     внятное сообщение при 403 с временем сброса из `X-RateLimit-Reset`.
   - **Целостность:** llama.cpp публикует `sha256:` для ассетов в теле релиза —
     парсить их и сверять. Размер — только предварительная проверка, **не**
     доказательство целостности. Если хэша нет — предупредить пользователя.
   - Разбор имён (`llama-b<build>-bin-<os>-<backend>-<arch>.zip`) регуляркой +
     таблица известных шаблонов. Неизвестные ассеты — только в advanced-режиме.
   - Тестируется на **сохранённых metadata нескольких реальных релизов**.

2. **`detectPlatform()` / рекомендация backend**
   - `QSysInfo::kernelType()`, `currentCpuArchitecture()`.
   - Рекомендация — **только подсветка**, выбор всегда за пользователем.
   - Учитывать: версия NVIDIA-драйвера (не просто наличие `nvidia-smi`),
     наличие Vulkan loader, glibc на Linux, версия macOS, разрядность процесса.
   - **Финальная проверка — фактическая:** после установки выполняется probe;
     ошибки вида «не найден `cudart64_*.dll`» / «libvulkan.so.1: cannot open
     shared object» распознаются и переводятся в понятную рекомендацию сменить
     backend.

3. **CUDA:** к основному ассету нужен `cudart-llama-bin-win-cuda-*.zip`.
   Оба распаковываются в один каталог; правила коллизий: файл из cudart
   не перезаписывает одноимённый файл основного архива, конфликт логируется.

4. **`ArchiveExtractor` — только ZIP** (vendored `miniz`, MIT).
   `.tar.gz` не поддерживается: llama.cpp публикует `.zip` для всех платформ,
   а корректный TAR-парсер — лишний код и лишняя поверхность атаки (ADR 34).
   ⛔ Не использовать приватные Qt-заголовки (`QZipReader`) и системный `tar`.

   **Обязательный hardening:**
   - нормализация путей; отклонение `..`, абсолютных путей, `C:\`, UNC (`\\`),
     смешанных `/` и `\`, NUL и control-символов в именах;
   - отклонение symlink- и hardlink-записей, ведущих за пределы каталога
     (для MVP — отклонять symlink-записи целиком);
   - лимиты anti-bomb: суммарный размер (≤4 ГБ), количество файлов (≤10 000),
     коэффициент сжатия (≤200:1);
   - запрет перезаписи файла, уже созданного в этой же распаковке
     (дубликаты нормализованных путей);
   - `chmod 0755` только для исполняемых файлов, внутри нашего каталога.

5. **`InstallTransaction`** — установка транзакционна:

```
1. download        → <models|runtime>/... .part   (нужный том)
2. verify size + sha256
3. extract         → runtime/staging/<uuid>/
4. validate        состав архива, отсутствие escape-путей
5. locate          llama-server внутри staging
6. probe           --version / --help → ServerCapabilities
7. atomic rename   staging/<uuid> → runtime/<tag>
8. commit          запись в SettingsStore (последним шагом!)
```

Любой сбой → `staging/<uuid>` удаляется целиком, настройки не тронуты.
Осиротевшие `staging/*` чистятся при старте приложения.

6. Кнопка «Очистить неиспользуемые сборки» (не трогает активную).

**UI:** `Settings → Runtime` — релиз, backend, размер, «Скачать и установить»,
прогресс, «Установлено: b10594 (CUDA 12.4)», «Проверить обновления».

**Тесты**

- `test_release_catalog` — JSON-фикстуры ≥3 реальных релизов, извлечение
  sha256 из тела, выбор ассета для win/x64/cuda, macos/arm64, linux/x64/vulkan,
  устойчивость к неизвестному имени.
- `test_archive_extractor` — zip-slip, абсолютный путь, UNC, symlink, bomb,
  дубликаты, права на файлы.
- `test_install_transaction` — сбой на каждом шаге не оставляет мусора
  и не меняет настройки.

---

### Stage E — Модели с Hugging Face

**Задачи**

1. **`ModelCatalog`**
   - Дерево: `GET https://huggingface.co/api/models/{repo}/tree/{revision}?recursive=true`
     с **pagination** (`Link: rel="next"`), URL-кодированием `repo`, `revision`
     и каждого сегмента `path`.
   - Поля: `path`, `size`, `lfs.oid` (sha256). Обрабатывать отсутствие `size`
     и не-LFS файлы (у них `lfs` нет — тогда sha256 недоступен, предупредить).
   - **Пиннинг ревизии:** сначала `GET /api/models/{repo}` → `sha` коммита;
     всё дерево и все загрузки идут через **этот commit SHA**, не через `main`.
     Иначе между показом каталога и скачиванием файл может измениться.
   - Скачивание: `https://huggingface.co/{repo}/resolve/{commitSha}/{path}`.
   - Поиск: `GET /api/models?search={q}&filter=gguf&limit=30&sort=downloads`.
   - `Authorization: Bearer <hf/token>` — только на host `huggingface.co`
     (снимается при редиректе, §7.3).
   - `401/403` → сообщение «репозиторий gated», **ссылка на страницу лицензии**
     и инструкция принять условия (юридическое требование, §7.7).

2. **Multi-part GGUF** (`model-00001-of-00005.gguf`) — распознавать паттерн,
   качать все части, в `--model` передавать первую, в UI показывать как один
   элемент с суммарным размером.

3. **`ModelPreset` — точные совместимые связки.**
   Это критично именно для LLocr: `det_tokens` и текущий prompt заточены под
   конкретную модель; произвольная vision-GGUF выдаст мусор.

```json
{
  "id": "unlimited-ocr-q4km",
  "title": "Unlimited-OCR (Q4_K_M)",
  "repo": "org/repo",
  "revision": "<commit-sha>",
  "model": "model-Q4_K_M.gguf",
  "mmproj": "mmproj-F16.gguf",
  "parser": "det_tokens",
  "prompt": "document parsing.",
  "ctxSize": 8192,
  "minBuild": "b4000",
  "approxVramGb": 6.5,
  "license": "https://huggingface.co/org/repo",
  "sha256": { "model-Q4_K_M.gguf": "...", "mmproj-F16.gguf": "..." }
}
```

   При выборе пресета применяются `parser`, `ctxSize` и (если пользователь
   согласен) `prompt`. Ручной выбор произвольной пары остаётся возможным,
   но помечается как «непроверенная комбинация» с предупреждением о том,
   что формат вывода может не соответствовать парсеру.

4. **Два каталога пресетов** (исправление ошибки v1: `:/...` read-only):
   - встроенный `:/models/default-presets.json` — только чтение;
   - пользовательский `<AppData>/LLocr/models/catalog.json` — read/write;
   - слияние по `id`, пользовательский переопределяет встроенный;
   - кнопки «Импорт», «Экспорт», «Восстановить умолчания».

5. **Валидация загруженного:** sha256 (из `lfs.oid` или пресета) + магия
   `GGUF` в первых 4 байтах.

6. **`ModelRegistry`** (`<modelsDir>/index.json`)
   - запись через `QSaveFile`, поле `schemaVersion`;
   - при повреждении/отсутствии — **пересканировать `modelsDir`** и
     восстановить реестр;
   - запись помечается `managed` (внутри `modelsDir`) или `external`
     (пользовательский GGUF где-то ещё);
   - удаление разрешено **только** для `managed` и **только** внутри
     `modelsDir` (проверка canonical path);
   - ⛔ приложение никогда не предлагает удалить `external`-файл — только
     «убрать из списка»;
   - запрет удаления активной модели при `RuntimeState == Ready`;
   - лок `<modelsDir>/.registry.lock` на время записи.

**UI:** `Settings → Models` — таблица (активная выделена), поиск по HF,
выбор файлов, прогресс, предупреждение о VRAM, ссылка на лицензию.

**Тесты**

- `test_model_catalog` — фикстуры tree API с pagination, определение mmproj,
  multi-part, извлечение квантизации, кодирование путей с не-ASCII.
- `test_model_registry` — восстановление после повреждения, отказ удалить
  external, отказ удалить активную, атомарность записи.

---

### Stage G-core — Готовность подключения

Выполняется **до** мастера, потому что мастеру нужен рабочий self-test.

**Задачи**

1. Полная реализация `RuntimeController::ensureConnectionReady()` для `Managed`:
   `Stopped` + (`startOnDemand`|`autoStart`) → старт → health → `/v1/models` →
   проверка alias → `ResolvedConnection`.
2. Дедупликация: параллельные вызовы получают один общий future.
3. `cancelPendingStart()` — кнопка Stop прерывает ожидание старта, а не только
   сетевой запрос.
4. `AppBusyState` проброшен в QML; `canRecognize` по формуле §1.4.
5. `runSelfTest()` — независимый от recognition-flow метод:
   старт → health → `/v1/models` → один запрос с встроенной тестовой картинкой
   → вернуть текст/ошибку. Используется мастером и кнопкой «Проверить».
6. Матрица ошибок §7.5.

**Приёмка:** в `Managed` нажатие «Распознать» при остановленном сервере
корректно проходит через `StartingRuntime` и завершается результатом;
Stop во время старта прерывает его.

**Тест:** `test_ensure_connection` — на мок-сервере: External-путь,
Managed-старт, дедупликация параллельных вызовов, отмена, таймаут.

---

### Stage F — Мастер первого запуска

**Задачи**

1. Триггер строго по §4.4 (`setupVersion == 0 && !setupDismissed`),
   **без сетевых проверок**.
2. Шаги:
   - **Welcome** — «Локальный сервер (рекомендуется)» / «У меня уже есть сервер
     или API» (второй ведёт в Connection и ставит `setupVersion = 1`).
   - **Runtime** — «Скачать llama.cpp» (автовыбор сборки + ручной выбор
     backend) / «Указать существующий бинарник». Прогресс, ошибки, «Повторить».
     Предупреждение про Windows SmartScreen / антивирус (§7.8).
   - **Model** — пресет из каталога / поиск на HF / локальный GGUF.
     Показ размера, ориентировочной VRAM, ссылки на лицензию.
   - **Launch** — порт, ctx-size, n-gpu-layers, чекбокс `autoStart`
     (по умолчанию **выключен**), превью команды,
     кнопка «Проверить» → `runSelfTest()`.
   - **Done** — сводка, `setupVersion = 1`.
3. Навигация: Назад / Далее / Пропустить; шаг не покидается вперёд, пока его
   условие не выполнено. Крестик → `setupDismissed = true`.
4. Все строки через `qsTr`, добавить в `llocr_ru.ts`.

**Приёмка:** на чистом профиле пользователь доходит от старта до распознанной
страницы, не открывая терминал.

---

### Stage G-UI — Интеграция в основной интерфейс

**Задачи**

1. Индикатор в `Footer.qml`: точка (серый `Stopped` / жёлтый `Starting` /
   зелёный `Ready` / красный `Failed`) + текст + клик → `ServerLogWindow`.
2. Баннер «Параметры запуска изменены — требуется перезапуск сервера»
   при правке `launch/*` во время `Ready`, с кнопкой «Перезапустить».
3. Прогресс `StartingRuntime` с текстом из stderr («loading model … 43%»).
4. Ошибки — по матрице §7.5.

---

### Stage H — Полировка

- Ротация лога, «Скопировать лог», «Открыть каталог».
- Оценка памяти: размер GGUF + KV-cache от `ctxSize` → предупреждение при
  превышении доступной RAM/VRAM.
- Проверка обновлений llama.cpp (opt-in).
- **Опционально:** watchdog-helper для macOS (§5.4) — полная гарантия no-orphan.
- **Опционально:** хранение `hf/token` и `provider/apiKey` в системном
  кейчейне (общая задача для всего приложения, §7.6).
- Расширенный мульти-инстанс: раздельные локи install/registry/runtime-owner
  вместо одного глобального.
- Документация: обновить `01`, `02`, `03`, `04`, `05`, `06`, `07`; `AGENTS.md`.

---

## 6. Изменения в существующих файлах

| Файл | Что делать |
| --- | --- |
| `src/main.cpp` | создать `RuntimeController`, `qmlRegisterSingletonInstance`, `SingleInstanceGuard`, `aboutToQuit → shutdownSync()`, показ мастера по `setupVersion` |
| `src/app/SettingsStore.*` | новые группы, Q_PROPERTY, миграция §4.4, `lastExternalBaseUrl` |
| `src/app/RecognitionController.*` | вместо чтения `provider/*` — `ensureConnectionReady()`; `cancelPendingStart()` в `stop()` |
| `src/app/AppController.*` | `busyState`, `canRecognize` §1.4, проброс `Runtime` |
| `resources/qml/SettingsDialog.qml` | вкладки Runtime / Models / Launch; в Connection — селектор режима; в Model — read-only alias в `Managed` |
| `resources/qml/MainWindow/Footer.qml` | индикатор состояния сервера |
| `resources/i18n/llocr_ru.ts` | новые строки |
| `CMakeLists.txt` | `src/runtime/`, `third_party/miniz`, платформенные `ProcessGuard_*`, `resources/models/` в ресурсы |
| `tests/CMakeLists.txt` | новые таргеты + мок-бинарь сервера |

---

## 7. Сквозные требования

### 7.1. Процессы
Только `setProgram` + `setArguments`. Пути пользователя никогда не
конкатенируются в строку команды. Рабочий каталог — каталог бинарника.

### 7.2. Транзакционность
Изменение `SettingsStore` — **последний** шаг любой установки. Все JSON —
через `QSaveFile` с `schemaVersion`.

### 7.3. Сеть
Только `https`. Редирект-политика не слабее `NoLessSafeRedirectPolicy`
(дефолт Qt6) **плюс** собственная проверка схемы, лимит 5 переходов и
**обязательное снятие заголовка `Authorization` при смене host**.
Проверять итоговый URL после всех редиректов.

### 7.4. Сетевой доступ к managed-серверу
`launch/host` ограничен `127.0.0.1` / `::1`. Значение вне loopback доступно
только при `runtime/allowNonLoopback = true` (advanced), с явным
предупреждением о том, что сервер не имеет аутентификации.
Даже при bind на `0.0.0.0` клиентский `baseUrl` остаётся `127.0.0.1`.

### 7.5. Матрица ошибок → сообщений

| Сигнатура | Сообщение / действие |
| --- | --- |
| порт занят | «Порт N занят. Смените порт или включите автоподбор» |
| `error: unknown argument` | автоматический ретрай без флага + пометка capability |
| `failed to load model` / нет файла | «Файл модели не найден: <путь>» + кнопка выбрать |
| `cudaMalloc failed` / `buffer_type_alloc_buffer` | «Недостаточно VRAM. Уменьшите `--n-gpu-layers` или `--ctx-size`» |
| `cudart64_*.dll` не найдена | «Не установлен CUDA runtime. Установите cudart-архив или выберите CPU/Vulkan» |
| `libvulkan.so.1` не найдена | «Vulkan недоступен, выберите другой backend» |
| `unknown model architecture` | «Формат GGUF не поддерживается этой сборкой llama.cpp» |
| build < `kMinimumSupportedBuild` | «Требуется llama.cpp b4000 или новее» |
| HTTP 403 GitHub | «Лимит GitHub API исчерпан, повтор после HH:MM» |
| HTTP 401/403 HF | «Репозиторий с ограниченным доступом» + ссылка на лицензию |
| health timeout | «Сервер не ответил за N с» + последние 20 строк лога |

### 7.6. Секреты
`hf/token` и `provider/apiKey` хранятся в `QSettings` **открытым текстом** —
это существующее поведение приложения. Обязательно:
- явное предупреждение в UI рядом с полем;
- токен **никогда** не пишется в лог, сообщения об ошибках и `toDisplayCommand()`;
- рекомендация выпускать HF-токен с правами **read-only**;
- перенос в системный кейчейн — Stage H, общей задачей.

### 7.7. Лицензии моделей
Перед скачиванием показывать название лицензии и ссылку на страницу модели.
Для gated-репозиториев — прямое указание, что условия принимаются на сайте HF,
а не в приложении.

### 7.8. Платформенные предупреждения
- **Windows:** свежескачанный неподписанный `llama-server.exe` может быть
  заблокирован SmartScreen или антивирусом. При `QProcess::FailedToStart`
  показывать соответствующую подсказку и предлагать ручной выбор бинарника.
- **Windows Firewall:** при первом bind возможен системный запрос — предупредить
  в мастере.
- **macOS:** карантин обычно **не** устанавливается на файл, записанный через
  `QNetworkAccessManager` (его ставит LaunchServices/браузер). Поэтому шага с
  `xattr` в плане нет. Если запуск всё же заблокирован Gatekeeper —
  показать диагностику и инструкцию, но **не** обходить защиту автоматически
  (ADR 35).

### 7.9. Деградация
Любая ошибка сети/распаковки/запуска оставляет UI работоспособным.
⛔ Режим `provider/mode` **не** меняется автоматически: пользователь может
вручную уйти в `External`, при этом `lastExternalBaseUrl` восстанавливается.

### 7.10. Стиль
clang-format, существующий стиль проекта, `qsTr` на всех новых строках UI,
асинхронность всех сетевых операций.

---

## 8. Тест-таргеты

| Таргет | Покрывает |
| --- | --- |
| `test_launch_config` | argv, `extraArgs`, дефолты, пути с пробелами/кириллицей |
| `test_settings_store` (расш.) | новые ключи, миграция §4.4, отсутствие автосмены режима |
| `test_runtime_lifetime` | единственность `RuntimeController` |
| `test_runtime_locator` | probe, терпимый парсинг версии, нестандартное имя файла |
| `test_capabilities` | таблица build-профилей + парсинг `--help`, отсев флагов |
| `test_server_process` | стейт-машина, health, таймаут, стоп, рестарт, лог |
| `test_ensure_connection` | External/Managed, дедупликация, отмена, таймаут |
| `test_download_manager` | `If-Range`/`Content-Range`, смена ETag, resume+sha256, отмена, место |
| `test_release_catalog` | фикстуры реальных релизов, sha256 из тела, выбор ассета |
| `test_archive_extractor` | zip-slip, UNC, symlink, bomb, дубликаты, права |
| `test_install_transaction` | сбой на каждом шаге, отсутствие мусора и правок настроек |
| `test_model_catalog` | pagination, revision pinning, mmproj, multi-part, кодирование |
| `test_model_registry` | восстановление, managed vs external, защита активной модели |

Вспомогательный таргет `mock_llama_server` — тестовый бинарь: `--version`,
`--help`, `/health`, `/v1/models`, управляемое падение и задержка старта.

**Ни один тест не ходит в реальную сеть.** Только локальный `QTcpServer`
и фикстуры в `tests/data/`.

---

## 9. Новые ADR (в `07-glossary-decisions.md`)

| # | Решение | Причина |
| --- | --- | --- |
| 26 | Два режима подключения `External` / `Managed`; провайдер остаётся transport-only | Не ломать существующий сценарий; управление процессом — отдельная ответственность |
| 27 | Управляемый сервер — только `llama.cpp` (`llama-server`), минимум `b4000` | Единый предсказуемый CLI; Ollama/LM Studio имеют свои менеджеры |
| 28 | Установка из GitHub Releases `ggml-org/llama.cpp`, распаковка своим кодом (vendored miniz), sha256 из тела релиза | Воспроизводимость, без внешних зависимостей; размер ≠ целостность |
| 29 | Модели с Hugging Face напрямую, **с пиннингом commit SHA**, sha256 из `lfs.oid` | Нет зависимости от Python; `main` изменчив |
| 30 | No-orphan: **сильная** гарантия на Windows (Job Object) и Linux (`PDEATHSIG`), **best-effort** на macOS + обнаружение осиротевшего процесса при старте | На macOS нет аналога `PDEATHSIG`; честная формулировка вместо ложного обещания |
| 31 | Мастер показывается по `runtime/setupVersion`, **без сетевых проб**; существующие профили считаются настроенными | Версия допускает эволюцию мастера; сетевая проба ненадёжна (VPN, выключенный сервер) |
| 32 | В `Managed` `baseUrl` и `modelId` вычисляются, `model/name` не перезаписывается | Единственный источник истины — запущенный процесс; настройки `External` сохраняются |
| 33 | ⛔ Автоматический attach к процессу на занятом порту запрещён | `/health == 200` не доказывает ни идентичность процесса, ни модель; чужим процессом нельзя владеть |
| 34 | Поддерживается только ZIP; `.tar.gz` не реализуется | llama.cpp публикует `.zip` на всех платформах; TAR-парсер — лишний код и поверхность атаки |
| 35 | ⛔ Автоматическое снятие `com.apple.quarantine` запрещено | Обход защиты ОС для скачанного исполняемого кода; к тому же QNAM карантин обычно не ставит |
| 36 | `RuntimeController` — singleton instance, создаётся в `main.cpp`; QML не может его создать | Определённый lifetime, исключены дубли экземпляров |
| 37 | `RecognitionController` получает подключение только через `ensureConnectionReady()` | Он не должен знать про режимы, процессы и health-check |
| 38 | Managed-сервер слушает только loopback; иное — advanced-опция с предупреждением | Сервер без аутентификации не должен попадать в сеть |
| 39 | Установка runtime транзакционна: staging → verify → probe → atomic rename → commit настроек | Исключает частично установленный runtime |
| 40 | `.part` создаётся в целевом каталоге, а не в общем `downloads/` | Cross-device `rename` не атомарен; модели часто на внешнем диске |
| 41 | Capability detection: allowlist по build + `--help` + ретрай при `unknown argument` | CLI llama.cpp меняется между сборками (например, `--flash-attn`) |
| 42 | Каталог пресетов разделён: встроенный `:/models/*` (RO) + пользовательский `<AppData>/.../catalog.json` (RW) | Файлы в Qt-ресурсах нельзя редактировать |
| 43 | Пресет задаёт точную связку model+mmproj+parser+prompt+ctx | `det_tokens` и prompt специфичны для модели; произвольная vision-GGUF даёт непарсируемый вывод |
| 44 | `autoStart` по умолчанию **выключен** | Иначе GUI при каждом запуске занимает несколько ГБ RAM/VRAM |
| 45 | `Authorization` снимается при редиректе на другой host | Иначе HF-токен утекает на CDN |

---

## 10. Порядок выполнения

```
A ──► B ──► C ──┬──► D ──┐
                └──► E ──┴──► G-core ──► F ──► G-UI ──► H
```

D и E независимы после C и могут выполняться параллельно разными агентами.

**Текущий статус:** ✅ **A выполнена** — каркас, режимы, resolver, SettingsStore-группы/миграция, RuntimePaths, ServerLaunchConfig, сакелет RuntimeController (External), RecognitionController через `ensureConnectionReady()`, SingleInstanceGuard. Далее по порядку — **B**.

**После каждого этапа обязательно:**
1. сборка на текущей платформе;
2. прогон всех тестов, включая существующие;
3. ручная проверка, что режим `External` не сломан;
4. проверка отсутствия процессов-сирот (для B и далее).

---

## 11. Definition of Done для всей задачи

- [ ] Чистый профиль: мастер → скачивание llama.cpp → скачивание модели →
      self-test → распознавание страницы, без терминала.
- [ ] Существующий профиль после обновления: мастер не появляется,
      `External` работает как раньше.
- [ ] Managed: старт по требованию, Stop прерывает и старт, и распознавание.
- [ ] Обрыв сети на большой загрузке → корректное возобновление.
- [ ] Прерванная установка не оставляет ни мусора, ни изменённых настроек.
- [ ] Нормальное закрытие не оставляет процессов на Win/Linux/macOS.
- [ ] Аварийное закрытие: Win/Linux — процесса нет; macOS — обнаружен при
      следующем старте и предложен к завершению.
- [ ] Все новые тесты зелёные, реальная сеть не используется.
- [ ] `hf/token` отсутствует в логах и в превью команды.
- [ ] Документы `01`–`07` обновлены, ADR 26–45 внесены.
