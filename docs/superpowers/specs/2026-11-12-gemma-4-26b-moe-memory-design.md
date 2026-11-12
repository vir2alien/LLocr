# Gemma 4 26B-A4B: поддержка модели и раскладка памяти (MoE) — дизайн

Дата: 2026-11-12. Статус: на одобрении владельца продукта.
Замеры и качество модели: `tmp/gemma26/RESEARCH.md` (вне репозитория).

## Intent и почему

Gemma 4 26B-A4B-it-qat (MoE: 30 блоков, 128 экспертов × 8 активных, encoder-free
vision, MTP-драфтер) показала на стенд-замерах лучший fix-результат за историю
стенда (57 формул: CER 0.055→0.026, индексы 0.85→0.92, 15 улучшено / 4 испорчено,
без мышления и подсказок) и читает лучше, чем чинит (read probe5: CER 0.031,
индексы 0.96). На машине владельца (6 GB VRAM, 16 GB RAM) она работает в режиме
частичной выгрузки (llama.cpp `--fit`) со скоростью ~55 с/блок fix.

Задача: (1) добавить модель в поставляемый каталог, (2) научить отображение
требований по памяти MoE-раскладке — показывать минимальные RAM и VRAM и
фактическую раскладку весов по бюджету карты.

## Goals

- Профиль `gemma-4-26b-a4b` в каталоге: роль fix (она же обслуживает протокол
  read, ADR 259), **не дефолт** (дефолт fix остаётся qwen3.5-4b).
- Эстиматор памяти: для моделей с флагом `cpuOffload` — три числа вместо одного:
  минимум RAM, минимум VRAM (пол при которой частичная выгрузка возможна),
  требование «полностью в VRAM» (существующее). По бюджету карты — фактическая
  раскладка весов VRAM/RAM.
- UI: строка кванта в дропдауне и блок в LaunchTab показывают оба минимума;
  «не влезает в VRAM» для таких моделей — не ошибка, а информирование; ошибкой
  остаётся только нехватка RAM.
- Launch-слой профиля: `n-gpu-layers auto` (llama.cpp `--fit` сам решает
  раскладку; платформенные 99 дают OOM), `ubatch-size 2048` (обязателен — без
  него сервер падает на первой картинке: non-causal vision), `ctx-size 8192`,
  `fit-target 512` (проверенная конфигурация машины владельца; не пользовательский
  knob — значение в модельном слое).
- Документация: раздел в docs/15 (EN+RU), строка в docs/18 §6, ADR в глоссарии.

## Non-goals

- Мастер автоподбора не меняется (выбор по размеру файла против 0.9×VRAM):
  26B выбирается вручную в «Моделях» — решение владельца.
- Управление `--fit-target` из приложения не делается: llama.cpp `--fit`
  справляется сам (проверено замерами), knobs не нужны.
- Языковые модели/другие кванты 26B не добавляются: в репозитории unsloth один
  квант q4_k_xl (14.25 GB) + mmproj F16 + MTP-драфтер.
- REAP-варианты (reap64/reap96) в каталог не идут.

## Anti-goals (что считаем провалом)

- Эстиматор завышает минимум RAM настолько, что модель отклоняется на машине,
  где она реально работает (16 GB RAM — рабочий минимум, проверено).
- На машинах, где модель влезает в VRAM целиком, отображение и запуск
  деградируют относительно сегодняшнего поведения.
- Сломанные существующие тесты каталога/эстиматора.

## Constraints

- Слои запуска: модельный слой заменяет одноимённые параметры платформы
  (`LaunchProfileStore::compose`); значения `Text` уходят на сервер дословно —
  `n-gpu-layers auto` валиден для llama.cpp (`--n-gpu-layers auto` = fit).
- Пресеты профилей компилируются в ресурс через `file(GLOB
  resources/profiles/models/*.json)` — новый JSON подхватывается сборкой.
- Каталог-тест `test_model_preset_catalog::shippedProfilesAreTheOnesTheDocsDescribe`
  держит таблицу поставляемых моделей — нужна новая строка.
- UI-строки через `qsTr` + переводы по конвенции репозитория; размеры шрифтов
  только через `Theme.*`.
- Секреты/настройки — не затрагиваются.

## Approaches (рассмотренные)

- **A. Минимум** — только новые числа в дропдауне и строка в LaunchTab, без
  расчёта раскладки. Отвергнут владельцем.
- **B. Раскладка в эстиматоре** — принят: эстиматор знает пол VRAM, минимум RAM
  и считает раскладку весов по бюджету карты.
- **C. B + управление fit-target из приложения** — отвергнут: `--fit` llama.cpp
  справляется сам, knobs усложняют без пользы.

Решения владельца: вариант B; мастер как есть; роль fix, не дефолт.

## Числа модели (измерены llama.cpp b12137, `--device none`, ubatch 2048,
ctx 8192, бюджет изображения 1120, MTP-драфт; `build/measure-memory/*-19162.log`)

| Файл | Размер |
| --- | --- |
| `gemma-4-26B-A4B-it-qat-UD-Q4_K_XL.gguf` | 14 249 047 104 |
| `mmproj-F16.gguf` | 1 193 058 784 |
| `mtp-gemma-4-26B-A4B-it.gguf` | 251 939 328 |

`memory`-блок профиля:

```
nLayer 30   nKvLayers 5   nHead 16   nKvHead 8   headDim 128
nEmbd 2816  nFf 2112      nMoeLayers 30   nExpertUsed 8   nExpertFf 704
visionTokens 1120          visionBytesPerToken 616 980
draftBytes 251 939 328     draftKvBytesPerToken 4 096   draftContextBytes 346 051 051
workingSetBytesPerToken 220 216   workingSetContextBytes 2
kvBytesPerToken 20 480     kvBytesFixed 629 145 600
cpuOffload true            (новое поле)
```

Замечания к замеру: headDim — 128 из кэша (GGUF-ключ `key_length` 512 — не
per-head ширина, ловушка §7.5 документации); `kvBytesFixed` 3072 клетки скользящих
слоёв при batch 2048 — клетки зависят от батча, читаются из лога (§7.8); скрипт
`scripts/measure-memory.sh` имел мёртвый флаг `--ubatch` (опечатка `UBACK`) —
исправлен в рамках этой работы.

Следствия формулы (ADR 191): требование «полностью в VRAM» ≈ **18.2 GiB**;
пол частичной выгрузки (всё, кроме основных весов, с той же маржой 8 %+64 MiB)
≈ **3.9 GiB**; минимум RAM (веса + запас 2 GiB) ≈ **15.2 GiB → показываем
«RAM ≥ 16 GiB»**. Контроль реальностью: на 6 GB карте `--fit` занял 5.05 GiB
(пол 3.9 + ~5 слоёв весов) при RAM 15.8 GiB — сходится.

## Раскладка (эстиматор, вариант B)

Новые поля в `ModelMemoryShape`/профиле: `cpuOffload: bool`,
`ramHeadroomBytes: qint64` (0 = константа эстиматора, 2 GiB).

Новые поля в `ModelMemoryEstimate`:
- `vramFloorBytes` = (mmproj + draft + draftContext + kvCache + computeBuffer +
  visionBuffer) + (64 MiB + 8 % этой суммы) — только для `cpuOffload`;
- `requiredRamBytes` = weightsBytes + ramHeadroomBytes — RAM-пол для сплита;
- `splitForVramBudget(vramBudget)` → {weightsInVram, weightsInRam}:
  `weightsInVram = clamp(vramBudget − vramFloorBytes, 0, weightsBytes)`,
  `weightsInRam = weightsBytes − weightsInVram`. Вызывает LaunchTab с бюджетом
  `0.9 × VRAM` (тот же `kMemoryBudgetFactor`).

Семантика предупреждений: `overVramBudget` для `cpuOffload` — не ошибка (UI
показывает информационный текст с раскладкой и требованием RAM); ошибка —
`overRamBudget` (новое): `requiredRamBytes > systemRamBytes` — **порог 1.0×**,
решение билд-чека: 0.9× давал бы красную ошибку на 16-гигабайтной машине
владельца, где модель проверенно работает (mmap деградирует плавно), а это
анти-цель дизайна. Для моделей без флага поведение не меняется вовсе.

Единицы — ГиБ (2^30): веса 13.27 GiB, mmproj 1.11 GiB, пол 3.88 GiB,
«полностью» 18.2 GiB, `requiredRamBytes` 15.27 GiB → метка «RAM ≥ 16 GiB».

## UI (что увидит пользователь)

Дропдаун-строка (TaskModelsTable, `quantItemText`):

```
⬇ q4_k_xl — 14.25 GiB · RAM ≥ 16 GiB · VRAM ≥ 3.9 GiB (split) / 18.2 GiB (full)
```

LaunchTab (блок под таблицей параметров запуска, `cpuOffload` + известная карта):

```
Video memory for this model: 18.2 GiB on this card — weights 14.25 GiB,
projector 1.17 GiB, draft 0.24 GiB, KV cache 0.74 GiB, working set 0.45 GiB,
vision 0.64 GiB, overhead 1.45 GiB.
Partial offload: the card holds ~1.5 GiB of the weights, ~11.8 GiB stay in RAM —
RAM ≥ 16 GiB of 15.8 GiB on this machine.
```

(числа примера — `splitForVramBudget(0.9 × 6 GiB)`: бюджет 5.4 GiB − пол 3.88
= 1.52 GiB весов в VRAM; правка билд-чека — в черновике были ошибочные 4.9/9.3.)

(вторая строка видна только когда модель не влезает в карту целиком; при
достатке карты текст «Partial offload» не показывается). Тексты — EN в коде,
RU — по конвенции переводов репозитория; шрифты/цвета через `Theme.*`.

## Launch-слой профиля (roles.fix.launch)

`spec-type draft-mtp`, `spec-draft-n-max 2`, `temp 0`, `jinja`,
`reasoning-budget 0`, `image-min-tokens 1120` — как у 12B; плюс
`ubatch-size 2048` (описание: encoder-free vision, non-causal, дефолт 512 роняет
сервер), `ctx-size 8192` (замер: 16384 не улучшает ни скорость, ни VRAM-пол),
`n-gpu-layers auto` (замена платформенных 99: полная модель в 6 GB не помещается,
llama.cpp `--fit` распределяет сам; `auto` валиден), `fit-target 512`.
KV-кэш остаётся платформенным f16 (решение билд-чека: q8_0 требует flash-attn,
который на win-vulkan выключен платформой; оценка с f16 совпадает с конфигом).
minBuild профиля — **b12137** (решение билд-чека: старые сборки молча парсят
`auto` как 0; замер сделан на b12137). Request-параметры роли —
как у 12B (temperature 0, нейтральные штрафы, stop `</CANDIDATE>`,
`cache_prompt false`, `stream false`).

## Что остаётся билдеру

- Точные формулировки строк UI (EN) и их переводы; где в LaunchTab встаёт
  вторая строка (после существующей, тем же стилем).
- Точные места правок: `ModelMemoryEstimator.{h,cpp}`, `ModelProfiles` (парсинг
  `cpuOffload`/`ramHeadroomBytes` в `Memory`), `RuntimeController::
  estimateModelMemory` (новые ключи карты + `overRamBudget`), `ModelQuantModel`
  (флаг и метки в quantMap), `TaskModelsTable.qml`, `LaunchTab.qml`,
  тесты `test_model_memory_estimator` / `test_model_preset_catalog`,
  `resources/profiles/models/gemma-4-26b-a4b.json`, docs/15 (+RU), docs/18 §6,
  ADR в docs/07 (следующий номер).
- Установка модели на машину владельца: скопировать три файла из
  `C:\AI\Models\gemma-4-26B-A4B-it-qat-GGUF` в
  `<modelsDir>/unsloth__gemma-4-26B-A4B-it-qat-GGUF/` вместо повторной загрузки.
- Номер ADR и формулировка решения в глоссарии.
