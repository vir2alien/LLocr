# Полный код-ревью LLocr

Дата: 2026-09-04  
Область: весь отслеживаемый проект, включая `src/`, `resources/qml/`, `tests/`, CMake-файлы и сетевые/runtime-компоненты.

## Резюме

Проверены 109 C++/header-файлов, 21 QML-файл и 20 тестовых исходников. Использованы:

- детерминированные Qt C++ и QML линтеры;
- `qmllint` 6.10.3;
- ручная трассировка асинхронных, сетевых, файловых и QML-модельных сценариев;
- попытка сборки существующего `build/`.

Найдено 19 подтвержденных дефектов и 13 целей для дополнительной проверки. Наиболее важные проблемы:

1. `ArchiveExtractor` допускает недостаточные bounds/overflow-проверки и оставляет частично распакованные файлы.
2. `OpenAiProvider` хранит указатель на уже удаленный `QNetworkReply`, что может привести к use-after-free.
3. `DownloadManager` не удаляет завершенные задачи; память и стоимость обновлений растут без ограничения.
4. Настройки timeout/port/launch принимают и сохраняют некорректные значения.
5. Отмена операций `ModelInstaller` не инвалидирует уже запущенные continuation `QtConcurrent`.
6. QML-списки пресетов и результатов поиска используют счетчик строк вместо уведомляемой модели и показывают устаревшие данные.
7. Pagination Hugging Face может отправить bearer-токен на URL из недоверенного `Link` header.

## Подтвержденные findings

### Критичный приоритет

#### [D-001] Недостаточные bounds-проверки локального ZIP-заголовка

- **Файл:** `src/runtime/ArchiveExtractor.cpp:292-303`
- **Категория:** безопасность, валидация входных данных
- **Проблема:** после чтения длин имени и extra-поля локального заголовка не проверяется, что оба поля целиком находятся в буфере архива до вычисления `dataOffset`.
- **Результат:** специально сформированный ZIP может привести к неверной арифметике смещений и чтению за пределами буфера.
- **Рекомендация:** проверять `nameLength + extraLength` через bounds-aware reader, валидировать `localOffset` и выполнять все сложения с проверкой переполнения.

#### [D-002] Overflow при проверке compression ratio ZIP

- **Файл:** `src/runtime/ArchiveExtractor.cpp:307`
- **Категория:** безопасность, валидация входных данных
- **Проблема:** выражение `compSize * kMaxCompressionRatio` может переполниться до сравнения с `uncompSize`.
- **Результат:** вредоносная запись может обойти ограничение compression ratio.
- **Рекомендация:** заменить умножение на проверку через деление с учетом остатка и нулевых размеров.

#### [D-003] Bearer-токен может уйти на недоверенный URL pagination

- **Файл:** `src/runtime/ModelCatalog.cpp:348-352`
- **Категория:** безопасность сети
- **Проблема:** URL из `Link: rel="next"` принимается без проверки scheme/host, а `pullGet()` повторно отправляет authorization header.
- **Результат:** компрометированный сервер или proxy может перенаправить запрос на другой host и получить HF-токен; возможен downgrade на HTTP.
- **Рекомендация:** разрешать только HTTPS и ожидаемый Hugging Face origin, корректно разрешать relative URL и удалять authorization при смене origin.

### Высокий приоритет

#### [D-004] Use-after-free для текущего сетевого ответа

- **Файл:** `src/providers/OpenAiProvider.cpp:111-136`
- **Категория:** lifecycle, надежность
- **Проблема:** `m_currentReply` сохраняет raw pointer после `reply->deleteLater()` и не обнуляется.
- **Результат:** последующий `abort()` может вызвать метод уже уничтоженного объекта.
- **Рекомендация:** использовать `QPointer<QNetworkReply>` и обнулять указатель только если завершившийся reply совпадает с текущим; отдельно определить политику параллельных запросов.

#### [D-005] Image provider использует controller после его уничтожения

- **Файл:** `src/main.cpp:107-120`, `src/app/OcrImageProvider.*`
- **Категория:** lifecycle, QML integration
- **Проблема:** `engine` создается раньше `appController`, но уничтожается позже из-за обратного порядка stack destruction. Provider принадлежит engine и хранит raw pointer на controller.
- **Результат:** при teardown или отложенном запросе изображения возможен доступ к уничтоженному controller.
- **Рекомендация:** обеспечить, чтобы controller жил дольше engine, явно уничтожать engine до controller либо использовать явный lifetime contract и безопасную отвязку provider.

#### [D-006] Асинхронные thumbnail-запросы читают изменяемую модель без синхронизации

- **Файл:** `resources/qml/MainWindow/ThumbDelegate.qml:34-42`, `src/app/OcrImageProvider.cpp:24-41`
- **Категория:** thread safety, QML image provider
- **Проблема:** `Image { asynchronous: true }` обращается к provider, который напрямую читает страницы и изображения controller, пока GUI может удалять или переупорядочивать страницы.
- **Результат:** гонки, неконсистентные кадры, invalid index или crash.
- **Рекомендация:** выдавать immutable image snapshots под синхронизацией или отключить asynchronous provider path и гарантировать чтение в owner thread.

#### [D-007] Список DownloadTask растет без ограничения

- **Файл:** `src/runtime/DownloadManager.cpp:35-42, 205-215`
- **Категория:** lifecycle, память
- **Проблема:** завершенные, ошибочные, отмененные и paused задачи остаются в `m_tasks` и QObject hierarchy навсегда.
- **Результат:** рост памяти, row count и времени всех последующих пересчетов на протяжении жизни приложения.
- **Рекомендация:** ввести bounded history или удалять terminal tasks через `beginRemoveRows()/endRemoveRows()`, отделив активные задачи от истории.

#### [D-008] Неограниченный размер загрузки

- **Файл:** `src/runtime/DownloadTask.cpp:436-452`
- **Категория:** безопасность, ресурсы
- **Проблема:** Content-Length используется только для progress, а фактически принятые байты не ограничиваются; `readAll()` пишет произвольный поток.
- **Результат:** URL с неверным размером или бесконечным stream может исчерпать диск и память.
- **Рекомендация:** задать лимиты для runtime/model, отклонять слишком большой Content-Length и abort при превышении фактического лимита; читать bounded chunks.

#### [D-009] Архив целиком загружается в память

- **Файл:** `src/runtime/ArchiveExtractor.cpp:170-180`
- **Категория:** ресурсы, отказ в обслуживании
- **Проблема:** `archive.readAll()` выполняется до проверки лимитов размера, числа файлов и распакованного объема.
- **Результат:** большой архив может вызвать чрезмерное потребление памяти до срабатывания защит от zip bomb.
- **Рекомендация:** проверять `QFileInfo::size()` до чтения и перейти к seek/stream reader с bounded buffers.

#### [D-010] Распаковка не транзакционна

- **Файл:** `src/runtime/ArchiveExtractor.cpp:313-353`
- **Категория:** целостность установки
- **Проблема:** файлы пишутся непосредственно в destination; ошибка поздней записи оставляет результаты ранних entries.
- **Результат:** частично установленный runtime и stale files при повторной попытке.
- **Рекомендация:** распаковывать во временный staging directory и атомарно promote только после успеха либо удалять весь созданный набор при ошибке.

#### [D-011] Ошибка CUDA companion archive превращается в warning

- **Файл:** `src/runtime/RuntimeInstaller.cpp:492-497`
- **Категория:** корректность установки
- **Проблема:** failure извлечения CUDA runtime не меняет `out.ok`; установка отмечается успешной.
- **Результат:** UI может считать CUDA runtime готовым, а запуск позднее падает из-за отсутствующих библиотек.
- **Рекомендация:** считать ошибку companion extraction fatal с rollback либо явно сохранять CPU-only результат без CUDA-конфигурации.

#### [D-012] Отмена ModelInstaller не инвалидирует futures

- **Файл:** `src/runtime/ModelInstaller.cpp:368-437, 742-748`
- **Категория:** async lifecycle
- **Проблема:** `cancelInstall()` сбрасывает состояние и downloads, но preparation/search continuation продолжает безусловно менять state и данные.
- **Результат:** отмененная операция может после новой операции вернуть UI в старое состояние или подменить pending model.
- **Рекомендация:** использовать generation/operation token, захватывать его в continuation и игнорировать устаревшие результаты.

#### [D-013] Некорректная валидация ответа `/v1/models`

- **Файл:** `src/runtime/RuntimeController.cpp:302-324`
- **Категория:** протокол, валидация
- **Проблема:** не проверяются object root, array `data`, object elements и непустой string `id`.
- **Результат:** malformed response может считаться ready connection с пустым или невалидным model id.
- **Рекомендация:** строго валидировать схему и отклонять ответ без хотя бы одного валидного id с диагностикой protocol error.

#### [D-014] Настройки сохраняют отрицательные/нулевые timeout и invalid port

- **Файл:** `src/app/SettingsStore.cpp:159-164, 584-589, 688-693`
- **Категория:** валидация
- **Проблема:** setters напрямую сохраняют значения QML без диапазонов.
- **Результат:** immediate timeout, невалидный port/context/launch параметр и непредсказуемые ошибки запуска.
- **Рекомендация:** валидировать на setter и getter, ограничить timeout положительным диапазоном, port значением `0` или `1..65535`, остальные launch параметры согласно контракту llama-server.

#### [D-015] Directory setup failure игнорируется

- **Файл:** `src/runtime/RuntimeController.cpp:397-400`
- **Категория:** обработка ошибок
- **Проблема:** результат `RuntimePaths::ensureDirectories()` отбрасывается, хотя метод возвращает подробную ошибку.
- **Результат:** server запускается в недоступном runtime directory и истинная причина теряется.
- **Рекомендация:** немедленно завершать start с surfaced error; аналогично проверить места в `RuntimeInstaller.cpp:359,368`.

#### [D-016] Ошибки записи resume metadata игнорируются

- **Файл:** `src/runtime/DownloadTask.cpp:625-640`
- **Категория:** обработка ошибок
- **Проблема:** не проверяются write/stream status и результат `QSaveFile::commit()`.
- **Результат:** stale или отсутствующие metadata приводят к неверному resume behavior.
- **Рекомендация:** проверять запись и commit, записывать понятную ошибку или отключать resume для поврежденного partial state.

#### [D-017] Model registry silently treats unknown origin as external

- **Файл:** `src/runtime/ModelRegistry.cpp:28, 59, 97`
- **Категория:** целостность данных
- **Проблема:** любое значение кроме `managed` превращается в `External`; byte size хранится как JSON double.
- **Результат:** поврежденная запись меняет lifecycle semantics, а большие размеры округляются.
- **Рекомендация:** отклонять unknown origin, отдельно обрабатывать legacy missing field и хранить qint64 как decimal string либо проверять безопасный диапазон.

#### [D-018] Connection в SettingsDialog слушает не Settings

- **Файл:** `resources/qml/SettingsDialog.qml:359-364`
- **Категория:** QML bindings
- **Проблема:** `Connections` без `target` слушает родительский ComboBox, хотя handler читает `Settings.connectionMode`.
- **Результат:** ComboBox не синхронизируется после reset/wizard/внешнего изменения настроек.
- **Рекомендация:** явно указать `target: Settings` или использовать контролируемую binding/write-back схему.

#### [D-019] QML delegates не обновляются при неизменном количестве строк

- **Файлы:** `resources/qml/ModelsTab.qml:147-151, 231-236`, `resources/qml/Setup/StepModel.qml:128-132, 206-210`
- **Категория:** QML model/view
- **Проблема:** `ListView.model` равен integer count, а данные берутся из plain invokable `presetInfo()`/`searchResult()`. Сигналы `presetsChanged`/`searchChanged` не меняют count и не обновляют delegate properties.
- **Результат:** повторный поиск или reload каталога с тем же count оставляет на экране старые title/repository/VRAM.
- **Рекомендация:** использовать настоящий `QAbstractListModel` с reset/dataChanged либо generation property, от которой зависят delegate bindings.

## Investigation targets

Требуют проверки тестом или уточнения контракта:

- `src/runtime/RuntimeController.cpp:159-160`: неограниченный список pending resolve callbacks.
- `src/runtime/RuntimeController.cpp:236`: callback может re-enter `completeResolve()` через новый start/stop.
- `src/runtime/RuntimeController.cpp:471-483`: restart lambda захватывает `QMetaObject::Connection` до присваивания.
- `src/runtime/ModelRegistry.cpp:134-145`: load без registry lock допускает lost update между процессами.
- `src/runtime/RuntimeInstaller.cpp:220-245`: update check может завершиться во время install и перезаписать state.
- `src/runtime/ModelInstaller.cpp:125,135`: equal-sized model selection зависит от tree/QHash order.
- `src/runtime/ModelInstaller.cpp:99`: mmproj выбирается по порядку API.
- `resources/qml/SetupWizard.qml:31,74`: `currentStep` объявлен как `Item`, но читается `.complete`; `qmllint` это не смог типизировать.
- `resources/qml/MainWindow/ThumbPanel.qml:24`: `cacheBuffer: 10000` и asynchronous thumbnails могут создать существенное memory pressure.
- `src/runtime/ModelRegistry.cpp:289`: removal проверяет containment primary path, но отдельно следует проверить `e.dir` перед recursive removal.

## Детерминированные проверки и качество

Линтеры также сообщили о большом числе низкоприоритетных замечаний, не являющихся самостоятельными подтвержденными runtime-дефектами:

- QML ordering, explicit root ids, `property var`, `var` вместо `let/const`, dot notation для anchors.
- повторная сборка `roleNames()` и default branches в model `data()`.
- raw integer timeout API без явного duration-типа.
- отсутствие `setTransferTimeout()` и явной TLS-диагностики на ряде сетевых путей.
- `QPair`, `QScopedPointer`, `qBound`, незащищенные `std::min/std::max` для Windows.
- синхронный flush каждой строки runtime log и линейные/квадратичные проходы в `DownloadManager`, логах и parser deduplication.

Эти пункты стоит закрывать после функциональных и security-дефектов; часть из них уже покрывается отдельными пользовательскими таймерами или относится только к тестовым fixture.

## Проверка сборки и тестов

Сборка не выполнена: существующий `build/` настроен под Unix Makefiles, но при запуске `cmake --build build -j 8` CMake использовал Ninja и завершился ошибкой `ninja: error: loading 'build.ninja': No such file or directory`. В соответствии с инструкциями проекта конфигурация не пере создавалась.

`qmllint` найден (`/opt/homebrew/bin/qmllint`, version 6.10.3). Полная type-level проверка приложения ограничена отсутствием доступного сгенерированного QML-модуля `LLocr`; поэтому часть диагностики QML требует запуска после успешной сборки.

## Рекомендуемый порядок исправлений

1. Закрыть ZIP bounds/overflow, размер архива, транзакционную распаковку и ограничения загрузки.
2. Исправить lifetime/reentrancy `OpenAiProvider` и порядок уничтожения engine/controller/provider.
3. Добавить строгую валидацию registry, `/v1/models`, user catalog и всех пользовательских runtime settings.
4. Ввести operation generation для `ModelInstaller` и защитить installer от пересечения update/install.
5. Заменить integer-count QML views на уведомляемые list models и исправить `Connections.target`.
6. Ограничить/архивировать завершенные downloads и оптимизировать aggregate progress.
7. Повторно сконфигурировать build-directory с Unix Makefiles только при необходимости и выполнить `cmake --build build -j 8`, затем `ctest --test-dir build`.
