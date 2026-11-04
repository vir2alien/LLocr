# 13. MinerU2.5-Pro-2605-1.2B

> Английский оригинал: [docs/13-model-mineru.md](13-model-mineru.md)

> Самодостаточное руководство по MinerU2.5-Pro-2605-1.2B в том виде, в
> каком его использует LLocr: какие задачи он обслуживает, как запустить
> для него `llama-server`, как собрать его запросы вручную. Русское
> зеркало: `docs/13-model-mineru.ru.md`.

## 1. Что это и какие задачи обслуживает

**MinerU2.5-Pro-2605-1.2B** (opendatalab, Apache-2.0) — компактная
document-parsing VLM, обученная на двухшаговом протоколе
mineru-vl-utils: детекция разметки по странице, сжатой в 1036², затем
транскрипция блоков по типам. Одношагового страничного режима в её наборе
промптов нет.

Из четырёх задач LLocr (`docs/09-tasks-and-verification.md`) он обслуживает
две, и для **обеих он модель по умолчанию** (ADR 252):

| задача | роль в приложении | парсер |
| --- | --- | --- |
| `layout` | проход разметки «Разметить страницу» (тулбар страницы, ADR 149–150) | `mineru-layout` |
| `blockRecognition` | транскрипция блока за блоком после разметки («Распознать», «Распознать блоки») | `raw` |

У обеих ролей слой запуска идентичен, поэтому последовательность
«разметка → распознавание» ни разу не перезагружает веса. На `decision`
модель не ставится (эта задача говорит на визуальном диффе MiniCPM-OCR
Jev) и на `fix` тоже. В приложении проход разметки идёт через
`LayoutController` → `LayoutModel`, проход блоков — через
`CheckController` → `BlockRecognitionModel`; оба собирают тело запроса
через `VisionChatModel::buildRoot` (`src/models/`). Профиль:
`resources/profiles/models/mineru.json`.

## 2. Файлы и загрузка

Репозиторий: `mradermacher/MinerU2.5-Pro-2605-1.2B-GGUF` (сторонняя
GGUF-упаковка; оригинальные веса — `opendatalab/MinerU2.5-Pro-2605-1.2B`).
Профиль объявляет `minBuild: b4000`.

| файл | размер (профильное `sizeBytes`) | назначение |
| --- | --- | --- |
| `MinerU2.5-Pro-2605-1.2B.Q8_0.gguf` | 531,066,976 (~531 МБ) | рекомендуемая квантизация |
| `MinerU2.5-Pro-2605-1.2B.Q6_K.gguf` | 505,735,264 | альтернативная квантизация |
| `MinerU2.5-Pro-2605-1.2B.Q5_K_M.gguf` | 420,084,832 | альтернативная квантизация |
| `MinerU2.5-Pro-2605-1.2B.Q4_K_M.gguf` | 397,806,688 | альтернативная квантизация |
| `MinerU2.5-Pro-2605-1.2B.Q3_K_M.gguf` | 355,465,312 | альтернативная квантизация |
| `MinerU2.5-Pro-2605-1.2B.mmproj-Q8_0.gguf` | 709,409,600 (~709 МБ) | vision-проектор — **обязателен** |

Без mmproj модель не видит картинку (примечание самого профиля). Файлы
ставятся приложением через Настройки → Модели; на диске репозиторий
ложится как `<modelsDir>/mradermacher__MinerU2.5-Pro-2605-1.2B-GGUF/…` —
`/` в id репозитория превращается в `__`
(`ModelInstallTransaction::repoDirName`). Каталог моделей по умолчанию —
`<AppData>/llocr/LLM OCR/models` (`QStandardPaths::AppDataLocation` +
`models`).

## 3. Запуск llama-server

Приложение собирает команду в `ServerLaunchConfig::toArguments()`
(`src/runtime/ServerLaunchConfig.cpp`): сначала базовые флаги (`--model
--mmproj --alias --host --port`), затем параметры, отсортированные по
`order`, из трёх слоёв — общей **политики**
(`resources/profiles/serverLaunch.json` → `policy.parameters`),
**платформенного профиля** (тот же файл, ключ os + backend) и **слоя
модели** (`roles.<task>.launch` в профиле модели). Флаг слоя модели
заменяет одноимённую строку платформенного слоя
(`LaunchProfileStore::compose`); alias — только ярлык, в сравнение при
перезапуске он не входит, поэтому один запущенный сервер обслуживает обе
роли.

Windows, CUDA (платформенный профиль `win-cuda`), роль layout:

```bat
llama-server.exe ^
  --model "C:\Users\me\AppData\Roaming\llocr\LLM OCR\models\mradermacher__MinerU2.5-Pro-2605-1.2B-GGUF\MinerU2.5-Pro-2605-1.2B.Q8_0.gguf" ^
  --mmproj "C:\Users\me\AppData\Roaming\llocr\LLM OCR\models\mradermacher__MinerU2.5-Pro-2605-1.2B-GGUF\MinerU2.5-Pro-2605-1.2B.mmproj-Q8_0.gguf" ^
  --alias llocr-layout ^
  --host 127.0.0.1 --port 8081 ^
  --parallel 1 --cache-reuse 0 --no-context-shift --no-warmup --verbosity 3 ^
  --n-gpu-layers 99 --flash-attn on --ctx-size 16384 ^
  --special
```

Для роли blockRecognition всё то же самое, кроме
`--alias llocr-blockrecognition`. Порт приложения по умолчанию — `0`
(автовыбор); `8081` здесь стоит вместо любого свободного порта. macOS Metal
и Linux CUDA гоняют те же флаги; Windows/Linux Vulkan ставят
`--flash-attn off` (там он экспериментальный); профиль `cpu` выбрасывает
`--n-gpu-layers`, ставит `--flash-attn off` и `--ctx-size 8192`.

| флаг | значение | слой, который его ставит | зачем |
| --- | --- | --- | --- |
| `--model` | путь к квантизации | базовый — настройки запуска задачи | веса |
| `--mmproj` | путь к проектору | базовый — настройки запуска задачи | vision-проектор, обязателен |
| `--alias` | `llocr-layout` / `llocr-blockrecognition` | базовый — `roles.<task>.alias` | имя, которое несёт поле `model` запроса |
| `--host` | `127.0.0.1` | базовый — настройки (`launchHost`) | локальный сервер |
| `--port` | `8081` (по умолчанию `0` = авто) | базовый — настройки (`launchPort`) | |
| `--parallel` | `1` | политика (`serverLaunch.json`) | один слот конкурентности |
| `--cache-reuse` | `0` | политика | нет окна переиспользования контекста между запросами |
| `--no-context-shift` | — | политика | контекст стабилен, без сдвига KV при переполнении |
| `--no-warmup` | — | политика | не делать прогревочный прогон при загрузке (быстрее старт) |
| `--verbosity` | `3` | политика | уровень журнала сервера для окна журнала |
| `--n-gpu-layers` | `99` | платформа `win-cuda` | все слои выгружаются на GPU |
| `--flash-attn` | `on` | платформа `win-cuda` | |
| `--ctx-size` | `16384` | платформа `win-cuda` | промпт + картинка + ответ |
| `--special` | — | слой модели — `roles.layout.launch` и `roles.blockRecognition.launch` | выводить special-токены в ответе |

**`--special` обязателен.** Ответ разметки — это и есть special-токенная
разметка — `<|box_start|>x1 y1 x2 y2<|box_end|><|ref_start|>TYPE<|ref_end|>`
— а табличные ответы несут ячеечные токены OTSL (`<fcel>`/`<ecel>`/`<nl>`);
сервер по умолчанию вырезает special-токены, и вырезанный ответ
разбирается как пустой. Это была первопричина первых отказов
«no layout blocks».

Сжатие страницы в 1036² делает вызывающий (`LayoutController` жёстко
ресайзит страницу в 1036×1036, не сохраняя пропорции, повторяя
апстрим-калибровку; нормированные координаты блоков от сжатия не зависят) —
страницы в родном разрешении на промпт разметки не отправлять.

## 4. Сборка запроса

`POST <server>/v1/chat/completions` с `Content-Type: application/json`;
`Authorization: Bearer <key>` уходит только когда настроен ключ (внешний
режим — управляемому локальному серверу ключ не нужен). Картинка идёт
**перед** текстом (профиль не задаёт `promptBeforeImage`), в виде PNG
data URL. Сборщик (`VisionChatModel::buildRoot`) всегда пишет
`chat_template_kwargs.enable_thinking`; профиль не объявляет reasoning,
поэтому там всегда `false`. Запрос никогда не несёт прежний текст блока —
кроп единственный источник истины (ADR 163).

**layout** — системное сообщение — `roles.layout.systemPrompt`, картинка
страницы в 1036², промпт — `roles.layout.prompts[0]`:

```json
{
  "model": "llocr-layout",
  "messages": [
    { "role": "system", "content": "You are a helpful assistant." },
    { "role": "user", "content": [
      { "type": "image_url", "image_url": { "url": "data:image/png;base64,<base64>" } },
      { "type": "text", "text": "\nLayout Detection:" }
    ] }
  ],
  "temperature": 0.0,
  "top_p": 0.01,
  "repeat_penalty": 1.0,
  "presence_penalty": 0.0,
  "frequency_penalty": 0.0,
  "stream": false,
  "cache_prompt": true,
  "max_tokens": 4096,
  "chat_template_kwargs": { "enable_thinking": false }
}
```

**blockRecognition** — кроп блока плюс типовой промпт из `blockPrompts`:
текстовые типы → `\nText Recognition:`, `table` →
`\nTable Recognition:`, `equation`/`formula` → `\nFormula Recognition:`
(обратите внимание на ведущий `\n`; для каждой метки, которую может
назвать проход разметки, есть своя запись):

```json
{
  "model": "llocr-blockrecognition",
  "messages": [
    { "role": "system", "content": "You are a helpful assistant." },
    { "role": "user", "content": [
      { "type": "image_url", "image_url": { "url": "data:image/png;base64,<base64>" } },
      { "type": "text", "text": "\nTable Recognition:" }
    ] }
  ],
  "temperature": 0.0,
  "top_p": 0.01,
  "repeat_penalty": 1.0,
  "presence_penalty": 1.0,
  "frequency_penalty": 0.05,
  "stream": false,
  "cache_prompt": true,
  "max_tokens": 8192,
  "chat_template_kwargs": { "enable_thinking": false }
}
```

- `model` — это alias, с которым сервер был запущен (управляемый режим),
  или имя модели задачи (внешний режим).
- `presence_penalty 1.0` / `frequency_penalty 0.05` у blockRecognition —
  апстримовый антициклический набор, останавливающий зацикливание таблиц
  и формул; у роли layout оба параметра нейтральные.
- `stream: false` отдаёт парсеру один полный ответ; `cache_prompt: true`
  держит эмбеддинги картинки и системное сообщение закэшированными между
  страницами и блоками. Оба параметра берутся из списка `request`
  профиля, как и все параметры сэмплирования выше.

**Формы ответа.**

- layout: один блок на строку,
  `<|box_start|>x1 y1 x2 y2<|box_end|><|ref_start|>TYPE<|ref_end|>`, за
  которым может стоять токен поворота (`<|rotate_up|>` и компания). Парсер
  `mineru-layout` читает строки структурно — никто не вправе вырезать
  служебные токены до него. Затем парсер поглощает покрытых детей в
  составные контейнеры (коэффициент покрытия ≥ 0.9: `equation_block` →
  `equation`/`text`, `image_block` → `image`/`chart`/`image_caption`,
  `list` → `text`/`list_item`) и стилизует метки через `blocks.styles`
  профиля (`doc_title`/`paragraph_title` → heading,
  `caption`/`footnote`/`header`/`footer` → italic, `list`/`list_item` →
  text, …).
- blockRecognition: простой текст блока, читается как есть (`raw`) —
  таблицы в OTSL (`<fcel>`/`<ecel>`/`<nl>`), формулы в LaTeX; фенс
  Markdown вокруг всего ответа срезается.

Проверено вживую на корпусе eq_hard: проход разметки возвращает
позиционированные блоки, поглощение сливает покрытых детей (22 → 18
боксов на референсной странице); распознанные блоки дальше идут через
обычный конвейер decision/fix.

## 5. Бюджеты

| бюджет | значение | источник |
| --- | --- | --- |
| окно контекста | 16384 на GPU-платформах, 8192 на `cpu` | платформенные профили `serverLaunch.json` |
| `max_tokens`, layout | 4096 | `roles.layout.maxOutput` |
| `max_tokens`, blockRecognition | 8192 | `roles.blockRecognition.maxOutput` |
| токены изображения, страница разметки | 1369 (сетка 37² у входа 1036²) | `memory.visionTokens`, используется оценкой памяти (`ModelProfiles::visionTokensFor`) |
| токены изображения, кроп блока | ~512 (дефолтная оценка приложения) | `kDefaultCropTokens` — профиль не задаёт `image-min-tokens` |

`maxOutput` роли подставляется в каждый запрос как `max_tokens` профилем
запроса задачи (`ModelProfiles::requestWithMaxOutput`). Ни одна роль не
объявляет reasoning, поэтому бюджет уходит как есть — подрезка
`RequestBudget` из ADR 193 принадлежит задаче fix. Страховка — само окно
контекста сервера: страница в 1036² (~1369 токенов изображения) плюс ответ
в 4096 токенов влезают в 16384 с запасом; профиль `cpu` (8192) оставляет
меньше запаса.

## 6. Известные ограничения

- **Только два шага.** Обученного одношагового страничного промпта нет;
  просить сырую транскрипцию страницы — out of distribution.
- Разметка ответа — служебный словарь: любой новый потребитель
  layout-ответа обязан обходить санитайзер сервис-токенов (регэксп,
  вырезающий `<|...|>`, съедает данные).
- GGUF-репозиторий — сторонняя упаковка (`mradermacher`).
- **MLX-конверсия MinerU2.5 теряет кириллицу.** Замерено на
  `mlx-community/MinerU2.5-2509-1.2B-bf16` под omlx: layout-ответ приходит
  транслитом (латинские двойники, буквы через пробел), тогда как та же
  страница через llama.cpp (GGUF этого профиля) читается правильно.
  Текстовый запрос без картинки к MLX-сборке отвечает тем же транслитом,
  значит дефект — в токенизаторе той конверсии: для русских документов
  берите MinerU в сборке llama.cpp или другую модель.

## 7. Ссылки

- GGUF: <https://huggingface.co/mradermacher/MinerU2.5-Pro-2605-1.2B-GGUF>
- Оригинальные веса:
  <https://huggingface.co/opendatalab/MinerU2.5-Pro-2605-1.2B>
- Смежные руководства: `14-model-glm-ocr.md` (вторая модель
  распознавания), `09-tasks-and-verification.md` (четыре задачи и
  конвейер), `18-memory-estimate.md` (сколько VRAM нужно серверу).
- Русское зеркало: `docs/13-model-mineru.ru.md`.
