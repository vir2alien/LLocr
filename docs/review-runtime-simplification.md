# Review — «костыли и велосипеды» в runtime (упрощение и устранение оверхеда)

> Статус: **часть списка исправлена.** Выполнены группы 3.1–3.3, 1.2(НЕ ФИКСИЛИ),
> 2.2, 2.5–2.7, все баги 4.1–4.9 и компаратор 1.3. Остаются (см. раздел 5):
> упрощения 1.1 (отдельно), 1.4, 1.5, 2.1, 2.3, 2.4, 2.8, 3.4–3.8.
> Назначение: рабочий список для дальнейшей доработки. Агенты берут пункт,
> фиксируют в коде, ставят ему `**FIXED**` и дописывают, что именно изменилось.
> Номера сроков/строк могут «уплыть» после правок — проверять по функциям/файлам.

Этот документ — результат анализа кода, добавленного по
`docs/09-local-runtime-plan.md` (модуль `src/runtime/` + интеграция в `src/app/`
и QML), на предмет **костылей** (заплатки, хрупкий код) и **велосипедов**
(переписывание существующего/лишняя сложность). Отдельно вынесены реальные баги,
найденные попутно, — коды все были заявлены «исправлены и причёсаны», но примеры
ниже всё ещё в дереве.

## Сводная таблица

| # | Тип | Файл(ы) | Суть | Статус |
|---|-----|---------|------|--------|
| 1.1 | Велосипед | `src/runtime/ArchiveExtractor.cpp` | Рукописный ZIP + DEFLATE + Huffman + CRC-32 (~650 строк) вместо zlib/miniz | OPEN |
| 1.2 | Велосипед | `src/runtime/ServerLaunchConfig.cpp:32-72` | `parseExtraArgs` == `QProcess::splitCommand()` | **НЕ ФИКСИЛИ** — см. §1.2 (нет совпадения поведения) |
| 1.3 | Велосипед | `ModelRegistry.cpp` + `ModelInstaller.cpp` | Дублированный компаратор сортировки частей | **FIXED** (см. §1.3) |
| 1.4 | Велосипед | `ModelMemoryEstimator.cpp:57-203` | Рукописный парсер GGUF-заголовка (+ UB, см. 4.x) | OPEN |
| 1.5 | Велосипед | `ModelInstaller.h` | QML-модель списка через `int index → QVariantMap` | OPEN |
| 2.1 | Костыль | `LlamaServerProcess.cpp:306-343` | Хрупкий парсинг прогресса llama.cpp по stdout/stderr | OPEN |
| 2.2 | Костыль | `ReleaseCatalog.cpp:237-251` | Таймаут сети через `QEventLoop`+`QTimer` | **FIXED** (см. §2.2) |
| 2.3 | Костыль | `LlamaServerProcess.cpp:176-192` | Health-опрос без single-flight | OPEN |
| 2.4 | Костыль | `LlamaServerProcess.cpp:23-37,131-140` | TOCTOU-гонка свободного порта | OPEN |
| 2.5 | Костыль | `RuntimeController.cpp:558-566` | `cfg.port=0` — заплатка двойного `--port` | **FIXED** (см. §2.5) |
| 2.6 | Костыль | `RuntimeController.cpp:112-117,163,730`; `SettingsStore.cpp:24,51` | Stringly-typed режим подключения | **FIXED** (частично, см. §2.6) |
| 2.7 | Костыль | `RuntimeController.cpp`; `LlamaServerProcess` | Мэджик-числа и таймауты без имён | **FIXED** (см. §2.7) |
| 2.8 | Костыль | `ModelRegistry.cpp:116` | Сентинел `ctxSize==8192` перегружен как реальное значение | OPEN |
| 3.1 | Оверхед | `RuntimeController.cpp:389-526` | Две полные реализации self-test (дубль ~110 строк) | **FIXED** (см. §3.1) |
| 3.2 | Оверхед | `Setup/StepLaunch.qml:53-67` | `fmtCommand()` в QML дублирует `toDisplayCommand()` C++ | **FIXED** (см. §3.2) |
| 3.3 | Оверхед | `RuntimeController.cpp` | Ручной `QFutureInterface` + параллельный сигнальный канал для QML | **FIXED** (см. §3.3) |
| 3.4 | Оверхед | `RuntimeController.h` | Фасад «кухонный комбайн» (~20 свойств/инвокабл) | OPEN |
| 3.5 | Оверхед | `SettingsStore.h/.cpp` | ~90 однотипных геттер/сеттер/сигнал + дрейф `resetToDefaults` | OPEN |
| 3.6 | Оверхед | `RuntimeLocator.cpp:180-225` | LRU-кэш из 4 слотов для одного бинарника | OPEN |
| 3.7 | Оверхед | `UiController.h` + `main.cpp:105,110` | Тройная регистрация QML-синглтона | OPEN |
| 3.8 | Оверхед | `RecognitionController`/`AppController` | Парные методы, отличающиеся одним флагом | OPEN |
| 4.1 | Баг | `LlamaServerProcess.cpp:591-601` | `markFailed()` не убивает живого сына → deadlock restart | **FIXED** (см. §4.1) |
| 4.2 | Баг | `LlamaServerProcess.cpp:395-398` | `owner.json` не чистится при неожиданном выходе | **FIXED** (см. §4.2) |
| 4.3 | Баг | `LlamaServerProcess.cpp` | `/v1/models` фолбэк отключён на auto-restart | **FIXED** (см. §4.3) |
| 4.4 | Баг | `LlamaServerProcess.cpp:96-105` | `setOptions()` нарушает контракт «no-op while running» | **FIXED** (см. §4.4) |
| 4.5 | Баг | `LlamaServerProcess.cpp:289-310` | Ротация лога теряет строку-триггер | **FIXED** (см. §4.5) |
| 4.6 | Баг | `ModelMemoryEstimator.cpp:163-170` | Strict-aliasing UB в `GgufReader::takeValue` | **FIXED** (см. §4.6) |
| 4.7 | Баг | `ModelInstaller.cpp:390-443,655-684` | Data race: чтение `m_settings`/`m_paths` из QtConcurrent | **FIXED** (см. §4.7) |
| 4.8 | Баг | `ModelInstaller.cpp:513-537` | `sha256`/`lfsOid` собираются, но не исполняются | **FIXED** (см. §4.8) |
| 4.9 | Баг | `ModelInstaller.cpp` | Мёртвый метод + всегда-не-null указатель `mmprojRel` | **FIXED** (см. §4.9) |

---

## 1. Велосипеды (переписывание существующего)

### 1.1 Рукописный ZIP + DEFLATE + Huffman + CRC-32
**Файл:** `src/runtime/ArchiveExtractor.cpp`
**Статус: OPEN**

Полностью рукописная реализация RFC 1951: `crc32` (таблица 256, `0xEDB88320`),
`BitReader` (LSB-first), `HuffmanTrie`, `DeflateDecoder` (fixed/dynamic/stored
блоки, скользящее окно 32K), парсинг ZIP центрального каталога + EOCD
(`extractZip`). Это ~650 строк и одновременно самый рискованный участок из всего
runtime: inflate — сложный для аудита формат, рис многих классов багов
(zip-slip, decompression bombs, падение на битых блоках — частично уже видно в
виде исключений `throwCapExceeded`).

Почему не `qUncompress`: Qt даёт только zlib-обёртку (`qCompress`/`qUncompress`),
а записи ZIP лежат в raw-deflate формате, поэтому их нельзя поодиночке.
`QCryptographicHash` не имеет `Crc32` — ручной CRC сам по себе оправдан.

Рекомендация: прилинковать системный `zlib` (`inflate`) или `miniz`/`libzip` для
inflate, оставив свой парсинг центрального каталога и hardening (нормализация
имён, лимиты распаковки, проверка путей) как слой поверх доверенного декодера.

### 1.2 — `parseExtraArgs` == `QProcess::splitCommand()`
**Файл/строки:** `src/runtime/ServerLaunchConfig.cpp:32-72`
**Статус:** **НЕ ФИКСИЛИ** (поведение не одинаковое)

Ручной токенизатор на пробелы с учётом одинарных/двойных кавычек (с машинкой
`quote`/`hadToken`). Qt 6 предоставляет `QProcess::splitCommand()`.
Однако **эмпирическая проверка на Qt 6.10.3/macOS показала расхождение**
(прогон против строк из `test_launch_config`):

- `--spaced 'a b c'` — ручной парсер → `['a b c']` (одна лексема),
  `splitCommand` → `['a', 'b', 'c']` (три лексемы, кавычки не понимает).
- `"a"b` — ручной → `['a','b']`, `splitCommand` → `['ab']`.

То есть `splitCommand` **не учитывает одинарные кавычки** (это «shell-like»
Unix-путь). Раз одиночные кавычки — заявленное поведение (см. заголовок и тест
`parseExtraArgsQuotesAndSpaces`), и он влияет на реальные `launchExtraArgs`,
передаваемые серверу, — замена стала бы регрессией. Решение: оставить ручной
парсер как есть. Если одинарные кавычки в будущем не нужны, можно
заменить `splitCommand` и упростить тест/комментарий — отдельная задача.

### 1.3 — дублированный компаратор сортировки частей
**Файлы:** `src/runtime/ModelRegistry.cpp` (`sortSplitParts`),
`src/runtime/ModelInstaller.cpp` (`partLess`)
**Статус: FIXED**

Побайтово один и тот же компаратор части GGUF вокруг `ModelCatalog::splitMultiPart`.
Вынести в одну свободную функцию (например, `ModelCatalog::splitAscending`) и
использовать её в обоих местах.

**FIXED (1.3):** общий компаратор вынесен в `ModelCatalog::splitAscending(a, b)`
(объявление в `ModelCatalog.h`, реализация в `ModelCatalog.cpp` рядом с
`splitMultiPart`). `ModelRegistry::sortSplitParts` теперь
`std::sort(..., &ModelCatalog::splitAscending)`, а `partLess`-лямбда удалена из
`selectModelFiles`, где `std::sort(..., &ModelCatalog::splitAscending)`. Требует
`ModelCatalog.h` (уже инклюдится обоими файлами). Поведение побайтово
совпадает с прежним.

### 1.4 — рукописный парсер GGUF
**Файл/строки:** `src/runtime/ModelMemoryEstimator.cpp:57-203` (`GgufReader`)
**Статус:** OPEN

Полноценный мини-парсер заголовка GGUF: magic/version/счётчики, u64-строки, 13
типов `GGUF_*`. Переписывание бинарного формата, на который выше уже есть
ридер у llama.cpp (не линкуется). Отчасти оправданно (нет зависимости), но и
несёт UB (п. 4.6). Либо линковать готовый парсер, либо оставить, но устранить
UB и оформить как самодостаточный модуль.

### 1.5 — QML-модель списка через `int index → QVariantMap`
**Файл:** `src/runtime/ModelInstaller.h` (`installedInfo`/`presetInfo`/`searchResult`)
**Статус:** OPEN

Три геттера возвращают `QVariantMap` по целочисленному индексу; QML строит
свои списки поверх `*Count`. Это ручная реализация того, что даёт нюха
`QAbstractListModel`/`QStringListModel` (роли, `count`, построчное чтение,
привязки `model[].field`). Переход на модель с ролями уберёт ручную сборку
`QVariantMap` (например, `installedInfo` — это ручная struct→QMap конверсия).

---

## 2. Костыли (заплатки/хрупкое)

### 2.1 — хрупкий парсинг прогресса llama.cpp
**Файл/строки:** `src/runtime/LlamaServerProcess.cpp:306-343`
**Статус:** OPEN

Прогресс извлекается из stdout/stderr: поиск `%` через `lastIndexOf(u'%')`,
обратный проход по цифрам, подстрока `"llama_new_context_with_model"`. Любая
смена формата логов в новой сборке llama.cpp тихо ломает прогресс-бар и может
именно сместить парсинг (`"5"` внутри постороннего слова, last-%-gonza).
Вариант: парсить только последний числовой токен перед `%`; если не чинить —
отказаться от процента и показывать детерминированный «Loading…».

### 2.2 — таймаут сети через `QEventLoop` + `QTimer`
**Файл/строки:** `src/runtime/ReleaseCatalog.cpp:237-251` (`fetchReleasesLocal`)
**Статус:** **FIXED**

Был ручной `QTimer` + флаг `timedOut` + `reply->abort()` вокруг `QEventLoop`.
Теперь таймаут задаётся как `QNetworkRequest::setTransferTimeout(timeoutMs)` —
сеть сама прерывает запрос (`finished` + `OperationCanceledError`), а флаг и
`QTimer` удалены. Функция остаётся синхронной (вызывается блокирующе из
`QtConcurrent::run` в `RuntimeInstaller::startCatalogFetch`).

### 2.3 — health-опрос без single-flight
**Файл/строки:** `src/runtime/LlamaServerProcess.cpp:176-192`
**Статус:** OPEN

`QTimer` интервал 250 мс шлёт новый `GET /health` каждый тик, не отменяя
в-полёте. При задержке ответа запросы копится. На loopback безобидно, но из-за
«пауза в поле traces» лучше держать флаг `m_healthInFlight` (или перезапускать
опрос по приходу ответа).

### 2.4 — TOCTOU-гонка свободного порта
`:` `src/runtime/LlamaServerProcess.cpp:23-37, 131-140`
**Статус:** OPEN

Порт «занимают» через `QTcpServer::listen(localhost, 0)` и сразу закрывают,
затем отдают llama-server (`--port`). Между закрытием сокета и биндингом дочкой
порт может занять другой процесс → «address already in use». Лучше передавать
серверу порт 0 и читать реальный порт из его вывода — гонка исчезает
полностью; минимум — ловить `failed to bind` и выкаты автоматического
сообщение «Port is busy».

### 2.5 — `cfg.port = 0` — заплатка двойного `--port`
**:` `src/runtime/RuntimeController.cpp:558-566` (`startServer()`)
**Статус:** **FIXED**

Код обнулял порт в `ServerLaunchConfig`, чтобы `toArguments()` не эмитил
второй `--port`, и отдельно подавал порт через `LlamaServerProcess::Options`
(`opts.port`).
Решение: убрана заплатка `cfg.port = 0` — `toArguments()` теперь единственный
источник `--host`/`--port` для фиксированного порта; `LlamaServerProcess::spawn()`
добавляет `--port` **только если его нет в argv** (случай auto-pick, порт 0,
где он сам резервирует конкретный порт). Дубликат `--port` устранён, порт — в
одном месте на дескрипт пути. Комментарий в `Options.arguments` обновлён.

### 2.6 — Stringly-typed режим подключения
**Файлы/строки:** `src/runtime/RuntimeController.cpp:112-117,163,730`;
`src/app/SettingsStore.cpp:24,51`
**Статус:** **FIXED (частично)**

`connectionMode` хранится в QSettings как строка `"external"`/`"managed"` и
пишется из QML (8 сайтов: `Settings.connectionMode === "managed"` и т.д.).
Полный переход на int-энум в QSettings + регистрация энума в QML — большой
риск по поверхности (мастер, вкладка настроек, ADR 26 о не-флипе режима),
отложен.
Сделано минимально-безопасно: в `SettingsStore` добавлены канонические
барьеры `ConnectionMode mode()` / `setMode(ConnectionMode)` (строко↔энум-маппинг
сосредоточен здесь), `modeFromSettings()` переведён на `mode()` (убран разбросанный
сравнение строк из runtime), а `setConnectionMode(QString)` теперь **отвергает
неизвестные значения** (qWarning+ignore), чтобы опечатка не сохранялась и
потом не молча читалась как External. QML-строковый API не тронут.

### 2.7 — магик-числа и таймауты без имён
**:` `src/runtime/RuntimeController.cpp:282,548,753`; `src/runtime/LlamaServerProcess.cpp`
**Статус:** **FIXED**

- `RuntimeController.cpp`: `setTransferTimeout(10000)`, `probeCached(…,5000)`
  (два места), `shutdownSync(5000)` — вынесены в константы
  `kModelsRequestTimeoutMs`/`kProbeTimeoutMs`/`kShutdownTimeoutMs`.
- `LlamaServerProcess.cpp`: ротация `5*1024*1024` и `256` →
  `kLogRotateSizeBytes`/`kLogRotateCheckEveryLines`; окно рестарта
  `5*60*1000`, лимит `3`, задержка `500` → `kRestartWindowMs`/
  `kMaxRestartsInWindow`/`kRestartDelayMs`. Все в file-local anonymous-namespace
  с пояснением.

### 2.8 — сентинел `ctxSize == 8192`
**Файл/строки:** `src/runtime/ModelRegistry.cpp:116`
**Статус:** OPEN

`if (e.ctxSize != 8192)` — 8192 одновременно и дефолтное значение, и
«не писать» в index. Модель, явно выбранная с контекстом 8192, не сохранится в
index и при следующей загрузке упадёт в fallback-дефолт. Разделить «сентинел»
и «реальное значение» (например, отдельный bool-флаг «задано явно»).

---

## 3. Оверхед / дублирование / лишняя сложность

### 3.1 — Две полные реализации self-test (главный дубль)
**Файл/строки:** `src/runtime/RuntimeController.cpp:389-526`
**Статус:** **FIXED**

`runSelfTestQml()` почти построчно повторял `runSelfTest()`: цепочка
`new QFutureWatcher<ResolvedConnection>` → проверка `conn.baseUrl.isEmpty()` →
`new QFutureWatcher<OcrResult>` → классификация результата. Единственное
отличие — результат зеркалился в `m_selftestOk/m_selftestMessage` вместо
структуры `SelfTestResult`.

Решение (~−68 строк): `runSelfTestQml()` теперь делегирует в `runSelfTest()` и
зеркалит `SelfTestResult` в QML-состояние:

```cpp
void RuntimeController::runSelfTestQml() {
    if (m_selftestRunning) return;
    if (modeFromSettings(m_settings) == ConnectionMode::External) {
        m_selftestOk = false;
        m_selftestMessage = tr("Self-test is available only in Managed mode");
        emit selftestFinished();
        return;
    }
    m_selftestRunning = true; m_selftestOk = false;
    m_selftestMessage = tr("Running self-test…"); emit selftestFinished();

    auto *watch = new QFutureWatcher<SelfTestResult>(this);
    connect(watch, &QFutureWatcher<SelfTestResult>::finished, this, [this, watch]() {
        const SelfTestResult r = watch->result();
        watch->deleteLater();
        m_selftestRunning = false;
        m_selftestOk = r.ok;
        m_selftestMessage = r.ok ? r.text
                                 : (r.error.isEmpty() ? tr("Recognition failed") : r.error);
        emit selftestFinished();
    });
    watch->setFuture(runSelfTest());
}
```

Цепочка `QFutureWatcher<ResolvedConnection>` повторялась также в
`RecognitionController::ensureResolve` и в обоих self-test; она полностью упала
благодаря переходу на колбэк-резолв (см. §3.3), где её заменил прямой вызов
`ensureConnectionReady(std::function)`.

### 3.2 — `StepLaunch.fmtCommand()` дублирует `toDisplayCommand()` в QML
**Файл/строки:** `resources/qml/Setup/StepLaunch.qml:53-67(используется:218-228)`
**Статус:** **FIXED**

В QML заново собиралась командная строка запуска без shell-экранирования —
пути с пробелами/кириллицей рендерились неоднозначно, и поведение разъезжалось
с C++-версией `ServerLaunchConfig::toDisplayCommand()` (которая экранирует и
имеет unit-тесты).
Решение: добавлен `Q_INVOKABLE QString launchCommandPreview()` на
`RuntimeController` — взял `ServerLaunchConfig::fromSettings`, выставил
`program` и вернул `toDisplayCommand(probeCached(...).capabilities)`. В
`StepLaunch.qml` удалена `fmtCommand()`; превью вместо неё читает
`root.commandPreview`, который обновляется в `refreshAll()` при смене любого
launch-поля (путь модели, ctx, кэш, порт, host, gpu, alias). Один источник истины.

### 3.3 — ручной `QFutureInterface` и параллельный сигнальный канал
**:` `src/runtime/RuntimeController.cpp` (`ensureConnectionReady()`,
`beginManagedResolve()`, `QFutureInterface<ResolvedConnection>`)
**Статус:** **FIXED**

Резолв был построен на рукописном `QFutureInterface` + флаг `m_resolveInProgress`
для `RecognitionController`, а для QML существовал отдельный сигнальный путь
(`runSelfTestQml`, `selftest*`). Два параллельных канала одного воркфлоу и породили
п. 3.1.
Решение: `ensureConnectionReady()` переведён на колбэк
`void ensureConnectionReady(std::function<void(const ResolvedConnection&)>)`;
`QFutureInterface<ResolvedConnection>`/`makeFuture`/`resolveExternalFuture`
удалены. Параллельные вызыватели кэшируют свой колбэк в `m_resolveCallbacks`
(dedup сохраняется), а `completeResolve()` дёргает все накопленные колбэки.
`RecognitionController::ensureConnectionReady()` и `runSelfTest()` переведены на
этот же API (убраны `QFutureWatcher<ResolvedConnection>`). External резолвится
по-прежнему синхронно (ADR 26). `runSelfTest()` оставлен `QFuture<SelfTestResult>`
(нужен тестам), но внутри теперь использует колбэк-цепочку. Тест
`test_ensure_connection` переписан под колбэк-API и зелёный (unsandboxed
из-за loopback-сокетов).

### 3.4 — `RuntimeController` — «кухонный комбайн»
**:` `src/runtime/RuntimeController.h`
**Статус:** OPEN

~20 `Q_PROPERTY`/`Q_INVOKABLE` на одном QML-синглтоне: lifecycle сервера,
резолв, self-test (п. 3.1), оценка памяти, ring-buffer логов, discovery путей,
статик `localPath`. Кандидаты на вынос: `SelfTestController` (self-test + состояние
`selftest*`), `RuntimeLog` (ring-buffer + лог-окно). Это сведёт к нулю соблазн
плодить дубли вроде 3.1.

### 3.5 — `SettingsStore`: ~90 однотипных квадруплетов + дрейф `resetToDefaults`
**:` `src/app/SettingsStore.h/.cpp`
**Статус:** OPEN

Каждое поле — механический гет/сет/`Q_PROPERTY`/сигнал (`setBaseUrl`:
`if (v == m) return; m_settings.setValue(k, v); emit kChanged();`). `resetToDefaults()`
вручную перечисляет ~50 сеттнеров, и каждый новый ключ надо вручную дописывать
в обоих местах → дрейф. Решение: таблица дефолтов `{key, type, default}` +
итеративный reset. Не приоритет на переписывание, но это самая повторяемая часть.

### 3.6 — LRU-кэш на 4 слота для одного бинарника
**Файл/строки:** `src/runtime/RuntimeLocator.cpp:180-225`
**Статус:** OPEN

Полный LRU на `QVector` с re-order и `kMaxCachedProbes=4` для того, что по
самому комментарию это один бинарник («A single managed server binary is used
at a time»). Достаточно одной пары ключ+результат или `QCache`. Плюс это
статический не-thread-safe глобал.

### 3.7 — тройная регистрация `UiController`
**Файлы:** `src/app/UiController.h` (`QML_ELEMENT QML_SINGLETON`) + `main.cpp:105,110`
**Статус:** OPEN

Класс объявлен `QML_ELEMENT QML_SINGLETON` (регистрация через модуль), а в
`main.cpp` дополнительно `qmlRegisterUncreatableType` (строки 105) и контекст-
проперти `uiController` (строки 110). При настоящем `qt_add_qml_module` это
двойная регистрация. Оставить один механизм (модульную регистрацию), убрать
остальные.

### 3.8 — парные методы, отличающиеся одним флагом
**Файлы:** `src/app/RecognitionController.cpp:26-50` (`startCurrent`/`startAll`),
`src/app/AppController.cpp:309-331` (`recognizeCurrent`/`recognizeAll`);
`AppController.cpp:58-69` (четыре `emit configChanged()` лямбды)
**Статус:** OPEN

Пары совпадают кроме флага → свести к одному `start(index, all, total)` /
`recognize(index, all)`; четыре одинаковые лямбды ре-эмита — к именованному слоту.

---

## 4. Реальные баги (высококонфидентные)

> Эти пункты не про «упрощение», а про фактически неверное поведение. Пометка
> `BUG` означает, что код принимает неверное состояние/не выполняет заявленное.

### 4.1 [BUG] `markFailed()` не убивает живого процесса → `start()` в deadlock
`src/runtime/LlamaServerProcess.cpp:591-601`
При таймауте старта (`waitForStarted`/health не достучались) код вызывает
`markFailed()`, который не останавливает живого сына. `start()` далее
возвращает «Server is already running», UI может показывать `Failed` при живом
дочернем процессе, а при его смерти — авто-рестарт с `Failed`. Лечение: на пути
таймаута старта детерминированно убить процесс до `markFailed()`.

**FIXED (4.1):** `markFailed()` теперь детерминированно убивает живого сына
(`kill()` + `waitForFinished(2000)`) перед переходом в `Failed` — Enter в `Failed`
никогда не оставляет живой дочерний процесс, поэтому `start()` больше не
«мёртво петляет» на «already running». Чтобы убийство не провоцировало
авто-рестарт «с Failed», `onProcessFinished()` учитывает `m_state != Failed` в
`restartEligible` и не зовёт `markFailed` повторно, когда статус уже `Failed`
(исключает дублирующую запись в лог). Покрыто полным прогоном тестов (в т.ч.
`test_ensure_connection::healthTimeoutSurfacesError`).

### 4.2 `owner.json` не чистится при неожиданном выходе
`src/runtime/LlamaServerProcess.cpp:395-398`
`clearOwnerJson()` выполняется только когда остановка пользовательская
(`m_stopRequested` + `stopOnExit`). При native/крах/неудачном bind запись
(pid+порт+program) остаётся; macOS reuse находит стейл-запись. Собирать owner
на любом терминальном состоянии процесса, а не только на stop-патэ.

**FIXED (4.2):** `clearOwnerJson()` теперь вызывается в начале
`onProcessFinished()` — как только дочерний процесс завершился по любой причине,
стейл-запись owner удаляется. Авто-рестарт потом пишет свежий owner в `spawn()`
(`writeOwnerJson()`). Путь «оставить сервер запущенным» (`stopOnExit=false`) не
доходит до `onProcessFinished` — owner сохраняется как задумано.

### 4.3 `/v1/models` фолб отключён на auto-restart
`src/runtime/LlamaServerProcess.cpp:134-140`
`start()` сбрасывал `m_modelsProbed=false`, а auto-restart зовёт `spawn()`
напрямую без сброса. Фолбак «просить /v1/models, если нет /health» выполнится
только в первый раз. Сброс — в начало `spawn()`.

**FIXED (4.3):** сброс `m_modelsProbed` (и `m_healthReached`) перенесён в начало
`spawn()` — общего пути и для первого старта, и для авто-рестарта. Фолбак
`/v1/models` снова доступен на каждой попытке старта.

### 4.4 `setOptions()` нарушает контракт «no-op while running»
`src/runtime/LlamaServerProcess.cpp:96-105`
В заголовке заявлено «No-op while running», но гвардия отсутствует. Если вызвать
среди запущенного сервера (или pendin-рестарта), `m_opts` молча перезапишется и
запланированный `singleShot(500ms)`-рестарт поднимет сервер с новыми опциями.
Добавить `if (m_process.state() != QProcess::NotRunning) return;`.

**FIXED (4.4):** гвардия добавлена — при ненулевом состоянии процесса
`setOptions()` возвращается, не трогая `m_opts`, что защищает от
полу-рестарта с новыми опциями.

### 4.5 ротация лога теряет строку-триггер
`src/runtime/LlamaServerProcess.cpp:289-310`
При превышении лимита `rotateLogIfNeeded()` закрывает/переименовывает файл, и
поток делает `return` без записи этой строки — она теряется. `m_logStream`
остаётся на закрытом девайсе до следующей записи. После ротации переоткрыть
файл и продолжить запись текущей строки.

**FIXED (4.5):** `rotateLogIfNeeded()` возвращает `bool` (была ли ротация); в
`appendLogFile()` после ротации файл немедленно переоткрывается (WriteOnly|
Append|Text), и текущая (триггерная) строка пишется без потерь.

### 4.6 [BUG] Strict-aliasing UB в GGUF-ридере
`src/runtime/ModelMemoryEstimator.cpp:165,170` (`GgufReader::takeValue`)
`*reinterpret_cast<float*>(&raw)`/`*reinterpret_cast<double*>(&raw)` — нарушение
строго-алиасинга (UB), на `-O2/-O3` может грузиться мусор. Заменить на
`std::memcpy(&f, &raw, sizeof f)` (или весь ридер на `QDataStream
`setFloatingPointPrecision`+`LittleEndian`). (`Int8` через `const qint8*` корректен —
`char`-алиасующий тип).

**FIXED (4.6):** `Float32`/`Float64` теперь читаются через `std::memcpy`
(добавлен `#include <cstring>`), строго-алиасинг-UB устранён; `Int8`-путь через
`const qint8*` оставлен (корректен).

### 4.7 [BUG] Data race чтения `m_settings`/`m_paths` из QtConcurrent
`src/runtime/ModelInstaller.cpp:390-443,655-684` (`beginPrepare`, `startSearch`)
Лямбды `QtConcurrent::run` захватывали `this` и читают `&m_settings`/`&m_paths`
(main-thread QObject) на воркер-потоке параллельно с `setHfToken` из QML →
data race. Снапшот `token`/`modelsDir` в локальные переменные до запуска воркера.

**FIXED (4.7):** и в `beginPrepare`, и в `startSearch` значения `hfToken()`
(и `modelsDir()` в начале `beginPrepare`) снапшотятся в локальные переменные до
`QtConcurrent::run`, воркер получает только копии и не захватывает `this` —
чтение мейн-тредовых QObject-членов из пула устранено.

### 4.8 `sha256`/`lfsOid` собираются, но не исполняются
`src/runtime/ModelInstaller.cpp:513-537` (`enqueueFile`)
В `req.sha256` отдавалась пустая строка (единственная «сильная проверка» ADR 29
`lfsOid` и `preset.sha256` никуда не вяжутся); GGUF-magic проверяется только
у primary-части, не у `mmproj`/доп. Проверить дигест через `DownloadTask::verifySha256`.

**FIXED (4.8):** `req.sha256` теперь заполняется дигестом для каждого файла.
Приоритет: пинованный `preset.sha256` по имени файла (нижний регистр), иначе
`HfFile::lfsOid` для этого repoPath (ADR 29). `Pending.fileSha256` добавлен.
`DownloadTask::verifySha256` сверяет каждый скачанный файл (primary, `mmproj` и
части), а не только GGUF-magic primary-части. Для не-LFS файлов (нет дигеста)
проверка по-прежнему пропускается.

### 4.9 мёртвый код и `-не-null` указатель
`src/runtime/ModelInstaller.cpp` (`onPresetsLoadedInternal` — невызываемый
резерв), `selectModelFiles` принимал `QString *mmprojRel` и всегда не-null
(единственный вызов с `&mmprojRel`) — убрать null-guard, сделать ссылку/структуру.

**FIXED (4.9):** удалён невызываемый `ModelInstaller::onPresetsLoadedInternal()
` (пустой резерв) из `.cpp` и заголовка. `selectModelFiles` теперь принимает
`QString &mmprojRel` (ссылка встроена в файл-статик функцию), null-guard убран,
единственный вызов в `beginPrepare` упрощён (−`&`).

---

## 5. Рекомендуемый порядок правок

1. **3.1 + 3.2 + 3.3** — закрыть дубль self-test и завязать preview команды +
   убрать ручной резолв-колбэк. Наибольший эффект на «велосипедо-фактор».
2. **1.2 + 2.2** — `QProcess::splitCommand`, убрать `QEventLoop` в сети
   (`setTransferTimeout`). Дёшево и безопасно.
3. **2.5 + 2.6 + 2.7** — `cfg.port=0` заплат, stringly-режимы, именованные
   таймауты.
4. **4.1–4.6 (баги процесса/лога/ридера)** — в первую очередь верный животных
   `markFailed`/`owner.json`/ротация и явный aliasing.
5. **1.1** — замена inflate на zlib/miniz: максимальная отдача по риску, но и
   самая объёмная; планировать отдельно со стадией тестов.

## 6. Замечания по процессу

- Перед правкой пунктов с номерами строк сверять с текущим кодом — после стадии
  «причеывания» некоторые номера могли сдвинуться.
- Каждый пункт после фикса помечать `**FIXED**` и дописывать, что изменилось.
- Не трогать то, что в этом документе не упомянуто, без отдельного разбора:
  детерминированная часть (например, HANDLE в `flushToDisk`, `detectPlatform`)
  уже покрыта предыдущим ревором (`docs/review-llm-management.md`).