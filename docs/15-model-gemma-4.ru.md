# 15. Руководство по модели: Gemma 4 E4B / 12B QAT (fix)

> Английский оригинал: [docs/15-model-gemma-4.md](15-model-gemma-4.md)

Самодостаточное руководство по двум моделям Gemma 4 QAT: какие задачи они
обслуживают, что скачивать, как запустить `llama-server`, как собирается
запрос. Каждое значение прослеживается до профилей моделей
(`resources/profiles/models/gemma-4-e4b.json` / `gemma-4-12b.json`),
`resources/profiles/serverLaunch.json`,
`resources/profiles/requests/fix-requests.json` или названного исходного
файла. Русское зеркало: [`15-model-gemma-4.ru.md`](15-model-gemma-4.ru.md).

## 1. Что это за модели и какую задачу они обслуживают

- **Gemma 4** (Google DeepMind), чекпоинты **QAT**, переупакованные Unsloth —
  int4-веса, обученные с учётом квантизации: качество (почти) bfloat16 при
  ~72% меньшем расходе памяти. Лицензия: Apache-2.0 (условия Gemma).
  Мультимодальные (текст + изображение), 140+ языков, родной контекст
  128K (E4B) / 256K (12B) — сервер всё равно работает с маленьким
  `ctx-size` из §5.
- **E4B** — 4,5 млрд эффективных параметров (8 млрд с Per-Layer Embeddings),
  отдельный зрительный энкодер (~150 млн). **12B Unified** — 11,95 млрд,
  «без энкодера»: патчи изображения входят в декодер напрямую; llama.cpp всё
  равно загружает проектор отдельным файлом `mmproj`. Зрительный бенчмарк
  карточки (OmniDocBench 1.5, среднее расстояние редактирования): E4B
  **0.181**, 12B **0.164**.
- Задача: **исправление** (`roles.fix`, алиас `llocr-fix`): на входе кроп
  одного блока плюс OCR-кандидат, на выходе **OK / FIX / REVIEW** по
  протоколу `full_block` (ADR 192) — `OK` оставляет кандидата, `FIX` несёт
  **полный исправленный блок**, `REVIEW` отдаёт блок человеческим глазам.
  Та же модель обслуживает задачу **чтения** («Перераспознать», ADR 259) с
  собственными промптами протокола чтения и контрактом FIX/REVIEW
  (ADR 260/266). Транскрипция и задача decision — работа других моделей
  (ADR 252/254).

## 2. Файлы и скачивание

Репозитории (`files.repo` в профилях; `minBuild` **b9600** — первая линейка
llama.cpp с поддержкой Gemma4 MTP и её ранними исправлениями):

| Репозиторий | Файл | id кванта | Размер | Назначение |
| --- | --- | --- | --- | --- |
| [`unsloth/gemma-4-E4B-it-qat-GGUF`](https://huggingface.co/unsloth/gemma-4-E4B-it-qat-GGUF) | `gemma-4-E4B-it-qat-UD-Q4_K_XL.gguf` | q4_k_xl | 3.9 GiB | основной квант (Unsloth Dynamic) |
| 〃 | `gemma-4-E4B-it-qat-UD-Q2_K_XL.gguf` | q2_k_xl | 3.0 GiB | мобильный mixture-квант |
| 〃 | `mmproj-F16.gguf` | f16 | 944 MiB | зрительный проектор — **обязателен**, без него модель не видит изображение |
| 〃 | `mtp-gemma-4-E4B-it.gguf` | q4_0 | — | **MTP-драфтер** |
| [`unsloth/gemma-4-12B-it-qat-GGUF`](https://huggingface.co/unsloth/gemma-4-12B-it-qat-GGUF) | `gemma-4-12B-it-qat-UD-Q4_K_XL.gguf` | q4_k_xl | 6.3 GiB | единственный квант репозитория (более высокие точности ухудшают качество, по данным упаковщика) |
| 〃 | `mmproj-F16.gguf` | f16 | 167 MiB | зрительный проектор — **обязателен** |
| 〃 | `mtp-gemma-4-12B-it.gguf` | q4_0 | — | MTP-драфтер |

- MTP-драфтер лежит в корне репозитория как «smart Q4_0» почти без потерь
  (в папке `MTP/` репозитория лежат другие точности). LLocr ставит его
  явно — профиль объявляет файл (`files.mtp`), установка скачивает его из
  того же репозитория и записывает как `launch/draftPath` роли (передаётся
  как `--model-draft`).
- Приложение раскладывает репозиторий как `<modelsDir>/<org>__<repo>/…` —
  например `<modelsDir>/unsloth__gemma-4-E4B-it-qat-GGUF/…`
  (`ModelInstallTransaction::repoDirName`).
- Оценщик памяти читает GGUF-ключи метаданных `gemma4.*`, поэтому
  предупреждение мастера о памяти отражает настоящие числа слоёв/голов.

## 3. Запуск llama-server

Одна конкретная команда — Gemma 4 **E4B** на **Windows · CUDA** (платформенный
профиль `win-cuda`), ровно как её собирает `ServerLaunchConfig::toArguments`
(`src/runtime/ServerLaunchConfig.cpp`) — сначала основные поля, затем
составленные параметры по порядку:

```bat
llama-server.exe ^
  --model C:\LLocr\models\unsloth__gemma-4-E4B-it-qat-GGUF\gemma-4-E4B-it-qat-UD-Q4_K_XL.gguf ^
  --mmproj C:\LLocr\models\unsloth__gemma-4-E4B-it-qat-GGUF\mmproj-F16.gguf ^
  --model-draft C:\LLocr\models\unsloth__gemma-4-E4B-it-qat-GGUF\mtp-gemma-4-E4B-it.gguf ^
  --alias llocr-fix --host 127.0.0.1 --port 8080 ^
  --parallel 1 --cache-reuse 0 --no-context-shift --no-warmup --verbosity 3 ^
  --n-gpu-layers 99 --flash-attn off --ctx-size 16384 ^
  --spec-type draft-mtp --spec-draft-n-max 4 --temp 0 ^
  --jinja --reasoning-budget 0 --image-min-tokens 1120
```

Легенда слоёв: **policy** = `serverLaunch.json → policy.parameters`,
**platform** = `serverLaunch.json → profiles[win-cuda]`, **model** =
`gemma-4-e4b.json → roles.fix.launch`, **core** = поля настроек, которыми
владеет `ServerLaunchConfig`. `LaunchProfileStore::compose` сливает слои:
запись модели заменяет одноимённую запись платформы/политики
(`flash-attn off`, `parallel`, `no-warmup`), остальное дописывается
и перенумеровывается.

| Флаг | Значение | Слой | Зачем |
| --- | --- | --- | --- |
| `--model` / `--mmproj` / `--model-draft` | пути | core | веса, проектор, драфтер (путь драфта из записи установки роли) |
| `--alias` | `llocr-fix` | core | `alias` роли; по нему маршрутизируется поле `model` запроса |
| `--host` / `--port` | `127.0.0.1` / выбрать любой | core | настройки по умолчанию (`127.0.0.1`, `0` = выбрать автоматически); ручная команда должна назвать порт |
| `--parallel` | `1` | policy (повторено в модели) | один слот декодирования; MTP-драфтинг однопоточный |
| `--cache-reuse` | `0` | policy | переиспользование контекста между запросами отключено |
| `--no-context-shift` | флаг | policy | держать контекст стабильным; без KV-сдвига при переполнении |
| `--no-warmup` | флаг | policy (повторено в модели) | пропустить warmup-прогон при загрузке (быстрее старт, медленнее первый запрос) |
| `--verbosity` | `3` | policy | уровень журнала сервера |
| `--n-gpu-layers` | `99` | platform | все слои на GPU |
| `--flash-attn` | `off` | **model (E4B)** | команда карточки для E4B идёт с `-fa off`; заменяет платформенный `on` |
| `--ctx-size` | `16384` | platform | промпт + изображение + окно вывода (8192 в CPU-профиле) |
| `--spec-type` | `draft-mtp` | model | спекулятивное декодирование с репозиторным MTP-драфтером |
| `--spec-draft-n-max` | `4` | model | драфт-токенов за шаг (значение карточки; упаковщик советует попробовать 1–6) |
| `--temp` | `0` | model | серверное жадное декодирование |
| `--jinja` | флаг | model | чат-шаблон GGUF разбирает канал мыслей Gemma 4 — режим рассуждений запроса решает, рассуждает ли модель первой |
| `--reasoning-budget` | `0` | model | серверный дефолт: рассуждений нет, пока запрос не попросит (запрос перекрывает) |
| `--image-min-tokens` | `1120` | model | Gemma 4 квантизует изображение в один из пяти бюджетов **70/140/280/560/1120**; карточка рекомендует верхние бюджеты для OCR и мелкого текста |

**Отличия 12B**: в её списке запуска нет записи `flash-attn` — стоит
платформенное значение (`on` на Metal/CUDA, `off` на Vulkan/CPU, как в
`-fa on` карточки) — плюс один дописанный флаг **`--ubatch-size 2048`**:
12B декодирует патчи изображения в основную модель с non-causal attention,
которую нельзя разрезать по убатчам; с серверным дефолтом 512 кроп блока,
растянутый до бюджета 1120 токенов, попадает чуть выше капа (ресайз
округляет вверх до сетки патчей) и убивает сервер по GGML-ассерту. E4B
кодирует изображения в собственном проекторе на каузальном пути и оставляет
дефолт.

## 4. Сборка запроса

Транспорт: `FixModel` поверх `VisionChatModel` (`src/models/`),
`POST /v1/chat/completions`, один запрос на блок, `stream: false`. Системный
промпт и промпт типа блока берутся из общего профиля запросов
`fix-requests.json` (роль называет его через `promptProfile`; слияние —
фолбэк: собственная формулировка роли выиграла бы, ADR 182/267). Изображение
— кроп блока как PNG data URL; кандидат — эффективный текст блока, формула
уже в каноническом LaTeX (ADR 194).

**Протокол исправления** (`full_block`) — буквальное тело для блока `text`,
рассуждения выключены (обе длинные строки дословно лежат в
`resources/profiles/requests/fix-requests.json`, ключи `systemPrompt` и
`blockPrompts.text`):

```json
{
  "model": "llocr-fix",
  "messages": [
    { "role": "system", "content": "<fix-requests.json → systemPrompt, verbatim>" },
    { "role": "user", "content": [
      { "type": "text", "text": "<fix-requests.json → blockPrompts.text, verbatim>" },
      { "type": "image_url", "image_url": { "url": "data:image/png;base64,<base64 of the block crop>" } },
      { "type": "text", "text": "OCR candidate:\n<CANDIDATE>\nLemma 2 holds for every n ≥ 1.\n</CANDIDATE>" }
    ] }
  ],
  "chat_template_kwargs": { "enable_thinking": false },
  "temperature": 0.0,
  "repeat_penalty": 1.0,
  "presence_penalty": 0.0,
  "frequency_penalty": 0.0,
  "stream": false,
  "cache_prompt": false,
  "stop": ["</CANDIDATE>"],
  "max_tokens": 2048,
  "reasoning_budget_tokens": 0
}
```

Порядок частей пользователя на проводе: `[промпт типа, изображение,
кандидат]` — флаг `promptBeforeImage` роли (`FixModel::buildRequestBody`) по
умолчанию `false`, и gemma-профили его не ставят, поэтому кандидат следует за
изображением (`FixModel` держит промпт типа первым в обоих состояниях
флага).

Поля верхнего уровня (`VisionChatModel::buildRoot`): `model` = вычисленный id
модели — алиас управляемого сервера `llocr-fix` или имя модели внешнего
сервера; `messages` как выше; затем **каждый** параметр запроса профиля как
JSON-поле верхнего уровня, плюс два подставленных:

| Поле | Значение | Источник |
| --- | --- | --- |
| `temperature` | `0.0` | роль `request` — жадное декодирование для протокола исправления (чатовое сэмплирование карточки — для разговора) |
| `repeat_penalty` / `presence_penalty` / `frequency_penalty` | `1.0` / `0.0` / `0.0` | роль `request` — нейтральные |
| `stream` | `false` | роль `request` — парсер получает один полный ответ |
| `cache_prompt` | `false` | роль `request` — запросы блоков остаются независимыми от общего KV-кэша драфта |
| `stop` | `["</CANDIDATE>"]` | роль `request` — обрывает ответ по вердикту; здесь месту только маркер протокола (названный end-of-text токен срезал бы ответ до пустоты) |
| `max_tokens` | по блоку, §5 | роль `maxOutput` 8192 как дефолт (`requestWithMaxOutput`); перекрывается по типу блока из `fix-requests.json → maxOutputByBlock` (`FixController::fixBlock`) |
| `reasoning_budget_tokens` | `0` / 512 / 1024 / 1536 | подставляется `applyReasoningBudget` из глобальных настроек (`check/fixReasoningBudgetTokens` — исправление, `check/readReasoningBudgetTokens` — чтение; дефолт off) |
| `chat_template_kwargs.enable_thinking` | `true` тогда и только тогда, когда `reasoning_budget_tokens` > 0 | `buildRoot` — бюджет заодно и переключатель (ADR 193) |

**Протокол чтения** («Перераспознать», `FixProtocol::ReadBlock`) — то же
тело, кроме: промпты берутся из
`resources/profiles/requests/read-requests.json` (роль исправления может
перекрыть своими `readSystemPrompt`/`readBlockPrompts`; gemma-профили этого
не делают), `max_tokens` — из `maxOutputByBlock` профиля чтения, бюджет
рассуждений — бюджет чтения — и **части-кандидата нет**: содержимое
пользователя — только `[промпт типа, изображение]`; распознанный текст
никогда не едет (ADR 266), читатель пишет блок с одной картинки.

**Ответ**: `choices[0].message.content` (мысль, когда включена, приходит
отдельно в `message.reasoning_content` и никогда не доходит до парсера;
`--jinja` заставляет llama.cpp направлять её туда). После снятия служебных
токенов и внешней кодовой ограды вердикт — первый токен: `OK` оставляет
байты блока; `FIX` + перевод строки + **полный исправленный блок** проходит
дифф-гейт (`FixDiffGate::fixBlock`, по-блочные правила эквивалентности
ADR 183), который кладёт исправление в `box.correctedText` или оставляет
собственные байты кандидата, когда исправление — лишь переписывание его же
(вердикт Ok); `REVIEW` отдаёт блок человеческим глазам. Деградации в Review:
ключевое слово `FIX` с пустой нагрузкой; ответ, обрезанный бюджетом
(`finish_reason: "length"` — фрагмент не исправление); ответ вне трёх
вердиктов (старый `EMPTY`, свободный текст) — отклонение.

**Обработка бюджета рассуждений** (`FixModel::check`, поведение ADR 193/230):
ответ, несущий `reasoning_content` при пустом `content`, — модель потратила
весь бюджет на рассуждения — повторяется **один раз без рассуждений**
(`reasoning_budget_tokens: 0`, `max_tokens` уменьшен на объём рассуждений),
с пометкой в футере; сервер запоминается, и последующие запросы пропускают
рассуждения с самого начала.

## 5. Бюджеты

| Величина | Значение | Источник |
| --- | --- | --- |
| Контекст сервера (`ctx-size`) | 16384 (GPU: Metal/CUDA) / 8192 (CPU) | платформенные профили `serverLaunch.json` |
| дефолт `max_tokens` | 8192 | роль `maxOutput` |
| `max_tokens` по блоку | page_number 64, header/footer 256, seal/title/key_value 512, caption/footnote/ref_text 1024, text/abstract/list 2048, code/equation 4096, table 16384 | `fix-requests.json → maxOutputByBlock` (алиасы: formula→equation, reference→ref_text, image/table_caption→caption, figure/table_footnote→footnote); тип без записи сохраняет дефолт 8192. Весь блок едет в ответе, поэтому бюджет обязан его вмещать |
| Бюджет рассуждений | off / 512 / 1024 / 1536 | глобальные настройки `check/fixReasoningBudgetTokens` (исправление) и `check/readReasoningBudgetTokens` (чтение), дефолт off — подставляется в каждый запрос, без перезапуска сервера (ADR 268) |
| Нижняя граница токенов изображения | 1120 | `--image-min-tokens` (§3) |

Как вычисляется бюджет запроса (`src/core/RequestBudget.cpp`,
`FixController::fixBlock`): промпт оценивается как
`imageTokens + cyrillicChars/2 + otherChars/4` по системе + промпту типа +
кандидату; свободное место — `ctx-size − promptTokens`; **ответ занимает
своё место первым**, **рассуждения уступают раньше ответа**, и запрос несёт
`max_tokens = answer + reasoning` с обрезанным рассуждением. Если выбранное
рассуждение не помещается, оно ужимается, и футер говорит, до чего.

## 6. Известные ограничения

- **E4B работает с выключенным flash attention** (собственная команда
  карточки); 12B хочет его включённым — это дефолт платформенного слоя,
  который профиль 12B не перекрывает. **12B нужен `--ubatch-size 2048`**
  (§3): с дефолтом 512 первый декод изображения убивает сервер (`non-causal
  attention requires n_ubatch >= n_tokens`).
- Мысль, обрезанная на середине (маленький бюджет), иногда вливается в сам
  ответ — за исправленным блоком следует хвост объяснения. Режим off
  остаётся дефолтом; берите бюджет достаточно большим, чтобы мысль
  уместилась.
- MTP-драфтинг однопоточный (`--parallel 1`); `cache_prompt` остаётся
  выключенным, чтобы запросы блоков были независимы от общего KV-кэша
  драфта. Более ранние сборки llama.cpp не знают Gemma4 MTP (`minBuild`
  b9600).
- Измерено на llama.cpp b11799 (модель `gemma-4-E4B-it-qat-UD-Q4_K_XL`,
  сервер `--jinja --reasoning-budget 0`): шаблон gemma-4 знает переключатель
  `enable_thinking` и канал мысли, бюджет едет в запросе и режет мысль на
  нужном токене, бюджет 0 не даёт рассуждений вовсе. 12B выдаёт *пустой*
  блок мысли при выключенных рассуждениях; `--jinja` вырезает маркеры, так
  что `content` приходит чистым в обоих случаях.
- Перезапуск управляемого сервера возвращает роль, которую он обслуживал
  (`restartBringsBackTheStartedRole`) — не «чините» смену роли перевыбором
  модели.

## 7. Где что лежит и ссылки

Код: профили моделей `resources/profiles/models/gemma-4-e4b.json` /
`gemma-4-12b.json`; тексты запросов
`resources/profiles/requests/fix-requests.json` / `read-requests.json`;
`VisionChatModel::buildRoot`, `FixModel::buildRequestBody` /
`parseResponse`, `FixDiffGate::fixBlock` (`src/models/`);
`ServerLaunchConfig::toArguments` + `LaunchProfileStore::compose`
(`src/runtime/`); `src/core/RequestBudget.cpp` + `FixController::fixBlock`
(`src/app/`). Документы: `docs/09-tasks-and-verification.md` (словарь задач
и конвейер), `docs/18-memory-estimate.md` (VRAM),
`docs/16-model-qwen3.5.md` (второе семейство исправления).

- E4B QAT GGUF: <https://huggingface.co/unsloth/gemma-4-E4B-it-qat-GGUF>
- 12B QAT GGUF: <https://huggingface.co/unsloth/gemma-4-12B-it-qat-GGUF>
- Unsloth «How to Run Gemma 4 QAT»: <https://unsloth.ai/docs/models/gemma-4/qat>
- Гайд Unsloth по MTP (точности драфтера, флаги `--spec-*`):
  <https://unsloth.ai/docs/models/mtp>
- llama.cpp: Gemma4 MTP #23398, ассистенты E2B/E4B #24282
- Семейство Gemma и лицензия: <https://ai.google.dev/gemma/docs/core>
- Русское зеркало: [`15-model-gemma-4.ru.md`](15-model-gemma-4.ru.md)

Проектные решения: ADR 88 (профили — весь контракт), 126 (слои запуска),
162–164 (задача исправления, двухслойная верификация), 183 (по-блочная
эквивалентность), 192 (`full_block`), 259/260/266 (протокол чтения).
