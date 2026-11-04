# 06. Среда разработки и дистрибуция

> Английский оригинал: [docs/06-dev-setup.md](06-dev-setup.md)

## Инструменты
| Область          | Инструмент                                  | Статус |
| ---------------- | ------------------------------------------- | ------ |
| Система сборки   | CMake                                       | ✅ используется |
| Контроль версий  | Git + GitHub/GitLab                         | ✅ используется |
| CI/CD            | GitHub Actions (`.github/workflows/ci.yml`) — шлюз `clang-format` на каждый push, полная матрица на `workflow_dispatch` (**ждёт шага подготовки Qt 6.10.3**); та же последовательность локально запускается через `scripts/check.sh` | 🟡 |
| C++-зависимости  | vcpkg — **только Windows**, поставляет ZLIB (`x64-windows`); в сборке macOS не участвует | 🟡 |
| Форматирование   | clang-format                                | ✅ используется |
| Тесты            | Qt Test (юнит-тесты в `tests/`)             | ✅ готово |

> **Примечание о сборке:** переиспользуйте существующее дерево `build/`
> (Unix Makefiles, `CMAKE_PREFIX_PATH=~/Qt/6.10.3/macos`); `VCPKG_ROOT` не
> задан, и vcpkg в сборке macOS не участвует. `scripts/check.sh` оборачивает
> всю проверку (configure → build → `ctest` → clang-format → `qmllint`) —
> именно её запускает CI. Сам CI устроен в два уровня (ADR 121): шлюз
> `clang-format` без Qt на каждый push и полная матрица только на
> `workflow_dispatch` — на хостед-раннерах нет Qt, а шаг подготовки всё ещё
> `TODO(provision-qt)`. Qt **6.10.3**, под который собирается проект, —
> официальная сборка онлайн-установщика (`qt.qt6.6103.clang_64` +
> `extensions.qtwebengine.6103` в `~/Qt/components.xml`), не заказная, так
> что шаг может использовать `jurplel/install-qt-action` (aqtinstall) —
> рецепт для конкретного хоста лежит в заголовке воркфлоу.
>
> **QML — настоящий модуль** (`qt_add_qml_module` в `src/CMakeLists.txt`,
> ADR 101): список файлов собирается глобом с `CONFIGURE_DEPENDS` (больше не
> ручной список на 49 строк), файлы компилируются `qmlcachegen` и адресуются
> как `qrc:/qt/qml/LLocr/…`. Двум QML-синглтонам (`Theme`, `BlockNames`)
> нужно `QT_QML_SINGLETON_TYPE` в CMake *и* `pragma Singleton` в файле —
> без свойства сгенерированный `qmldir` перечисляет их как обычные типы
> компонентов, и любой `Theme.accent` читается в рантайме как `undefined`.
> `BlockNames` живёт в корне модуля, а не в `Common/`, потому что `qmllint`
> разрешает синглтон через ближайший `qmldir`, а сгенерированный
> `Common/qmldir` его не объявляет.
>
> **C++-синглтоны по-прежнему регистрируются вручную** в `main.cpp`
> (`qmlRegisterSingletonInstance`): чтобы сделать их типами модуля, нужны
> фабрики `create()` (и объекты живут дольше движка), а у классов нет
> конструктора по умолчанию, который мог бы вызвать `qmltyperegistrar`.
> Как следствие, `qmllint` их не видит, и `.qmllint.ini` фиксирует, какие
> категории предупреждений из-за этого ненадёжны (`UnusedImports`).
>
> **На Windows** vcpkg **участвует**: набор MSVC2022 в Qt Creator задаёт
> toolchain vcpkg (`C:/vcpkg`, триплет `x64-windows`), который поставляет
> **ZLIB** для `find_package(ZLIB REQUIRED)` — см. раздел сборки Windows
> ниже и `THIRD_PARTY_NOTICES.md`.
>
> **macOS: держите одну версию Qt в процессе.** Qt 6 встраивает свою версию
> в приватные символы (`QtPrivate_6_10_3` …); если в процессе оказываются
> фреймворки Qt из двух разных установок, процесс падает на старте с
> `Symbol not found: __ZN14QObjectPrivateC2E16QtPrivate_<ver>`. Триггер: в
> окружении запуска `DYLD_FRAMEWORK_PATH` перечисляет `/opt/homebrew/lib`
> (Qt из Homebrew, `QtCore.framework` → `Cellar/qtbase/<ver>`) ПЕРЕД
> каталогами установленной Qt. dyld ищет по этой переменной **в первую
> очередь** любое имя, содержащее `.framework` — это сильнее `LC_RPATH` и
> даже абсолютных ссылок (проверено), так что никакая правка на стороне
> бинарника не поможет. Qt Creator внедряет её через `disclaim`, который
> переводит его внутренние `_QTC_DYLD_FRAMEWORK_PATH`/`_QTC_DYLD_LIBRARY_PATH`
> (видны в run env) в `DYLD_*` для запускаемого процесса.
>   1. **Лаунчер dev-сборки (этот репозиторий, автоматически):**
>      `src/CMakeLists.txt` добавляет шаг POST_BUILD (не-bundle macOS),
>      превращающий результат сборки `bin/llocr` в скрипт-обёртку: он
>      перезапускает настоящий бинарник (`bin/llocr.real`, лежит рядом) с
>      `env -u DYLD_FRAMEWORK_PATH -u DYLD_LIBRARY_PATH`. dyld этого потомка
>      разрешает Qt 6.10.3 через `LC_RPATH` бинарника, так что Run из Qt
>      Creator просто работает. Пересоздаётся при каждой сборке.
>   2. **Терминал**: `scripts/run-macos.sh` делает то же самое (`env -u …`).
>      Приложение печатает фактически загруженную QtCore на старте
>      (`runtime Qt: <ver> <libs path>`), так что любой регресс смешивания
>      виден мгновенно.
>   3. **Релиз**: hardened-бандл `.app` (`-DLLOCR_MACOS_APP_BUNDLE=ON` +
>      `cmake --install`); macdeployqt копирует фреймворки в бандл
>      (install names переписываются), а hardened runtime вырезает `DYLD_*`.
>   Задать `DYLD_FRAMEWORK_PATH` в project/kit env Qt Creator НЕ помогает:
>   Creator сам управляет переменными `_QTC_*` и игнорирует простое имя.
>   CMake также проваливает шаг конфигурации, если модули Qt6 разрешаются
>   из разных установок (защита в глубину, defense-in-depth).

## Дистрибуция — ⬜ не начата
| ОС      | Формат             | Инструмент                      |
| ------- | ------------------ | ------------------------------- |
| Windows | .exe-установщик    | Inno Setup / NSIS + windeployqt |
| macOS   | .dmg (подписанный) | macdeployqt                     |
| Linux   | AppImage / Flatpak | linuxdeployqt / flatpak-builder |

## Зависимости окружения
- Qt6 (включая модули **Qt PDF**, **Qt WebEngine**, **Qt Positioning**,
  **Qt WebChannel** и **Qt LinguistTools**), компилятор C++. **На Windows
  компилятор должен быть MSVC 2022 64-bit (набор «Desktop Qt 6.10.3
  MSVC2022 64bit»); MinGW-сборка Qt не поставляет модуль Qt WebEngine**
  (см. ADR 54). На macOS/Linux подходят и Clang/GCC.
- **Заголовочные файлы и нативная библиотека DjVuLibre** — опционально;
  включает ввод DjVu. Без них сборка проходит успешно, а открытие файлов
  `.djvu` сообщает понятную ошибку в рантайме (ADR 69; см. ниже).
- Pandoc — для экспорта в DOCX/PDF (внешняя зависимость, опционально
  поставляется в комплекте).
- Python 3.x — для вспомогательных скриптов в `scripts/` (см.
  [Вспомогательные скрипты разработчика](06-dev-setup.md#developer-helper-scripts)
  ниже); сам сервис RAG отложен (ADR 6).
- **Локальному рантайму и моделям ничего из перечисленного не нужно**:
  загрузки llama.cpp (GitHub Releases), загрузки GGUF с Hugging Face,
  распаковка ZIP + `.tar.gz` (zlib) и HTTP-клиент встроены в приложение —
  внешний Python и дополнительные нативные инструменты не требуются.

## DjVuLibre (опционально, включает ввод DjVu)

Ввод DjVu использует **C API декодирования DjVuLibre**, а не консольный
конвертер. CMake LLocr **не** скачивает, не клонирует, не собирает и не
устанавливает DjVuLibre. Когда зависимость отсутствует (или настроена не
полностью), CMake печатает **предупреждение** и собирается **без** поддержки
DjVu: `DjVuDocument.cpp` и DjVu-ветки `DocumentModel` исключаются из
компиляции, `test_djvu_document` и `test_app_import` пропускаются, а открытие
файла `.djvu` в приложении сообщает об ошибке и подсказывает, как включить
поддержку (ADR 69; жёсткое требование ADR 65 отменено). Передайте
`-DLLOCR_WITH_DJVU=OFF`, чтобы явно пропустить поиск зависимости и
предупреждение.

### macOS и Linux

Используйте поддерживаемый системный пакет разработки. Примеры:

```sh
# macOS / Homebrew
brew install djvulibre pkg-config

# Debian / Ubuntu
sudo apt install libdjvulibre-dev pkg-config

# Check the actual module name (not "djvulibre" or "libdjvu")
pkg-config --modversion ddjvuapi
pkg-config --cflags --libs ddjvuapi
```

На Unix корневой CMake сначала пробует `pkg_check_modules(... IMPORTED_TARGET
 ddjvuapi)` и предоставляет результат как `DjVuLibre::DjVuLibre`. Свой
префикс можно сделать видимым через `PKG_CONFIG_PATH` (каталог, содержащий
`ddjvuapi.pc`). Либо задайте `DJVULIBRE_ROOT` или явные пути ниже, чтобы
обойти pkg-config. Держите архитектуру согласованной с Qt (в частности
arm64 против x86_64 в Homebrew на macOS).

Для отдельной сборки из исходников upstream документирует `./configure`,
`make`, `make install` для релизных архивов; Git-чекинты вместо
`./configure` используют `./autogen.sh` и требуют Autotools. Задайте
`--prefix` на локальный каталог установки зависимости и затем предъявите
эту установку LLocr. JPEG нужен для нечастых DjVu-файлов с JPEG-кодированием;
TIFF используется некоторыми upstream-утилитами. Следуйте требованиям
именно того upstream-релиза, который собираете. Предпочитайте сборки
с разделяемыми библиотеками; резервный путь через raw-библиотеку не выводит
зависимости статических архивов и определения API для статической линковки
на Windows.

### Windows: подготовка нативной зависимости MSVC x64

**Не запускайте `vcpkg install djvulibre` и не рассчитывайте на порт
`libdjvu`.** На 2026-09-17 и URL `djvulibre/portfile.cmake`, указанный для
этой задачи, и полученные пути `libdjvu/portfile.cmake` /
`libdjvu/vcpkg.json` под `microsoft/vcpkg/master/ports` возвращали 404.
vcpkg остаётся документированным источником zlib, но не проверенным путём
установки DjVuLibre.

Используйте доверенную совместимую dev-сборку MSVC x64 или подготовьте её
отдельно из [upstream DjVuLibre](https://github.com/DjVuLibre/djvulibre):

1. Выберите и зафиксируйте upstream-релиз/коммит, который собираете.
   Прочитайте его раздел `README` про Windows, `win32/djvulibre/dirs.props`
   и README-файлы зависимостей под `win32/`. Решение ожидает отдельные
   деревья исходников JPEG/zlib/TIFF; уже установленный vcpkg-вариант zlib
   эти деревья автоматически не заполняет. Сам проект `libdjvulibre`
   ссылается на `libjpeg`.
2. Откройте `win32/djvulibre/djvulibre.sln` в Visual Studio 2022.
   Просмотренные здесь upstream-файлы определяют **только Debug/Release
   Win32** с явной настройкой линковщика `MachineX86`. Перенацельте
   toolset/SDK и создайте настоящие конфигурации **x64** для библиотеки и её
   проектов-зависимостей; обновите зависящие от архитектуры настройки
   линковщика и наследуемые листы свойств по мере необходимости. Простого
   выбора командной строки x64 недостаточно.
3. Соберите **DLL** `libdjvulibre` и её библиотеку импорта для Release
   (`/MD`) и, желательно, Debug (`/MDd`). Upstream использует базовое имя
   `libdjvulibre` для обеих конфигураций, в отдельных выходных каталогах.
   Перенацеливание этого устаревшего решения под MSVC 2022 **здесь не было
   проверено сборкой**; это требования подготовки, а не проверенная сборка
   одной командой.
4. Предоставьте LLocr заголовки и реальные пути `.lib`, как описано ниже.
   Одна DLL — это не пакет разработки. Не используйте 32-битную или MinGW
   `.a`-библиотеку с набором MSVC x64 Qt этого приложения.

### Поиск в CMake и переопределения

Следующие переменные кэша понимаются на каждой платформе:

| Переменная | Значение |
| --- | --- |
| `LLOCR_WITH_DJVU` | Опция сборки (по умолчанию `ON`): искать DjVuLibre и включить ввод DjVu; при отсутствии зависимости — автоматическая деградация с предупреждением. `OFF` полностью пропускает поиск |
| `DJVULIBRE_ROOT` | Необязательный префикс зависимости; также обходит pkg-config на Unix |
| `DJVULIBRE_INCLUDE_DIR` | Каталог, **содержащий** `libdjvu/ddjvuapi.h`, а не сам каталог `libdjvu`; корень upstream-исходников подходит |
| `DJVULIBRE_LIBRARY_RELEASE` | Полный путь к Release-библиотеке (на Windows — библиотека импорта `.lib`) |
| `DJVULIBRE_LIBRARY_DEBUG` | Полный путь к Debug-библиотеке; в отдельном каталоге, если имя файла совпадает |
| `DJVULIBRE_DLL_RELEASE` / `DJVULIBRE_DLL_DEBUG` | DLL Windows, соответствующая одноимённой библиотеке импорта; переопределите, если автоматический поиск не справился |

Резервный поиск ищет `djvulibre` / `libdjvulibre` в `lib`, `lib64`,
`Release` или `lib/Release`; Debug-поиск смотрит в `debug/lib`, `Debug`
или `lib/Debug` внутри заданных корней/префиксов и также принимает суффикс
`d`. Для нестандартных раскладок используйте явные переменные. Обе
конфигурации сохраняются, если присутствуют; RelWithDebInfo и MinSizeRel
используют Release. Если найдена только одна библиотека, она используется
для всех конфигураций, и CMake печатает этот выбор. Лучше предоставить обе
соответствующие сборки. При смене установки зависимости кэшированные пути
нужно обновить/очистить.

Пример **подготовленной раскладки** (создайте её из собственной сборки x64;
LLocr эти файлы не устанавливает и их существование не предполагает):

```text
C:/deps/djvulibre/include/libdjvu/ddjvuapi.h
C:/deps/djvulibre/lib/libdjvulibre.lib
C:/deps/djvulibre/debug/lib/libdjvulibre.lib
C:/deps/djvulibre/bin/libdjvulibre.dll
C:/deps/djvulibre/debug/bin/libdjvulibre.dll
```

С такой раскладкой добавьте `-DDJVULIBRE_ROOT=C:/deps/djvulibre` в аргументы
CMake в Qt Creator и перегенерируйте существующее дерево. Чтобы вместо этого
указать каждый файл по отдельности (из корня репозитория, после подготовки
файлов):

```bat
cmake -S . -B build/Desktop_Qt_6_10_3_MSVC2022_64bit_Debug ^
  -DDJVULIBRE_INCLUDE_DIR=C:/deps/djvulibre/include ^
  -DDJVULIBRE_LIBRARY_RELEASE=C:/deps/djvulibre/lib/libdjvulibre.lib ^
  -DDJVULIBRE_LIBRARY_DEBUG=C:/deps/djvulibre/debug/lib/libdjvulibre.lib
```

### Развёртывание и проверка в рантайме

На Windows сборка `llocr` копирует соответствующую DLL DjVuLibre рядом с
исполняемым файлом после линковки (`copy_if_different`). Debug использует
Debug-DLL; Release/RelWithDebInfo/MinSizeRel используют Release, с тем же
резервом одной библиотеки, что и при линковке. Поиск DLL проверяет каталог
библиотеки импорта и соседние каталоги `bin`, плюс раскладки `bin/Debug`,
`bin/Release`, `debug/bin`, `bin`, `Debug` и `Release` относительно
`DJVULIBRE_ROOT`. Для других раскладок переопределите `DJVULIBRE_DLL_DEBUG` /
`DJVULIBRE_DLL_RELEASE`. Отсутствующая DLL отключает поддержку ввода DjVu
с предупреждением (шаг копирования DLL тогда опускается); ошибкой
конфигурации это больше не является (ADR 69).

Qt DLL и любые дополнительные динамические зависимости DjVuLibre всё ещё
нужно разворачивать или держать доступными на `PATH`. Тесты, собранные без
сборки `llocr`, тоже нуждаются в DLL DjVuLibre на `PATH` или рядом с их
исполняемым файлом. Для Debug-сборки с примерной подготовленной раскладкой
используйте:

```bat
set PATH=C:\deps\djvulibre\debug\bin;C:\Qt\6.10.3\msvc2022_64\bin;%PATH%
```

Для Release используйте `C:\deps\djvulibre\bin` (а также для Debug, если
используется только Release-библиотека). Не смешивайте на `PATH` Debug/Release
DLL с одинаковым базовым именем. Для пакетируемых сборок macOS/Linux включите
разделяемую библиотеку и её рантайм-замыкание зависимостей, при
необходимости исправьте install names/RPATH; не считайте, что один
`windeployqt` или `macdeployqt` упакует и DjVuLibre. Включите уведомления/
лицензию и выполните обязательства GPL по соответствующим исходникам для
точно той поставляемой версии и любых патчей; см. `THIRD_PARTY_NOTICES.md`.

После подготовки зависимости перегенерируйте существующую сборку, затем
соберите `llocr`, `test_document_model` и `test_djvu_document`. Цели,
зависящие от DjVu (`test_djvu_document`, `test_app_import`), существуют
только при включённой поддержке DjVu. Например, в среде MSVC:

```bat
cmake --build build/Desktop_Qt_6_10_3_MSVC2022_64bit_Debug --target llocr test_document_model test_djvu_document
ctest --test-dir build/Desktop_Qt_6_10_3_MSVC2022_64bit_Debug -R "^test_(djvu_document|document_model)$" --output-on-failure
```

Проверено на этой машине под Windows с MSVC x64, Qt 6.10.3 и DjVuLibre
3.5.30: `llocr` собирается, и все 27 целей CTest проходят, включая 29
проверок DjVu. Локальный префикс зависимости —
`build/deps/djvulibre-msvc-x64` (заголовки в `include`, библиотеки импорта
в `lib/Debug` и `lib/Release`, DLL в `bin/Debug` и `bin/Release`). Debug-DLL
была скопирована рядом с
`build/Desktop_Qt_6_10_3_MSVC2022_64bit_Debug/bin/llocr.exe`. Рецепты
локальной сборки и происхождение — в `build/deps/DJVULIBRE-HANDOFF.md`
(неконтролируемые артефакты сборки, свежий чекаут их не поставляет). Эта
сборка зависимости использует локальную обёртку CMake, а не старое
upstream-решение Visual Studio. Сборки macOS/Linux и релизные сборки
приложения всё ещё требуют проверки.

Проверенные источники по зависимости:
- [README по сборке upstream](https://github.com/DjVuLibre/djvulibre/blob/master/README)
- [Шаблон pkg-config](https://github.com/DjVuLibre/djvulibre/blob/master/libdjvu/ddjvuapi.pc.in)
- [Публичный API и лицензия](https://github.com/DjVuLibre/djvulibre/blob/master/libdjvu/ddjvuapi.h)
- [Проект библиотеки MSVC](https://github.com/DjVuLibre/djvulibre/blob/master/win32/djvulibre/libdjvulibre/libdjvulibre.vcxproj)
- [Пакет разработки Debian](https://packages.debian.org/stable/libdjvulibre-dev)

## Сборка под Windows (MSVC 2022 64-bit)
Qt поставляет отдельные бинарники Windows под каждый тулчейн. LLocr
использует **Qt WebEngine** (предпросмотр Markdown), а модуль предоставляется
только в пакете **MSVC 2022 64-bit** — MinGW-сборки него нет. Пакеты Qt
лежат в одной папке версии: `C:/Qt/6.10.3/msvc2022_64` (с WebEngine) и
`C:/Qt/6.10.3/mingw_64` (его **нет** — Qt WebEngine отсутствует).

Сначала подготовьте **DjVuLibre**, как описано выше, и держите его
соответствующую DLL на `PATH` для команд сборки/тестов ниже. Эти команды
переиспользуют кэшированные аргументы `DJVULIBRE_*`; конфигурация с нуля
тоже нуждается в этих аргументах (или в префиксе зависимости на
`CMAKE_PREFIX_PATH`).

ZLIB (распаковка ZIP/gzip в `ArchiveExtractor`) разрешается через
`find_package(ZLIB REQUIRED)`; на Windows её поставляет toolchain vcpkg
(`C:/vcpkg/installed/x64-windows`, версия 1.3.x) — см.
`THIRD_PARTY_NOTICES.md`.

### Windows: настройка vcpkg + zlib (первый раз, в Qt Creator)
На Windows LLocr нужен ZLIB на этапе конфигурации
(`find_package(ZLIB REQUIRED)`), и с исходниками он **не** поставляется.
На новой машине настройте его один раз так:

1. **Установите vcpkg** (если его нет). Откройте PowerShell от имени
   администратора (подойдёт и обычный терминал, если не хотите писать под
   `Program Files`):
   ```cmd
   git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   ```
2. **Установите zlib для MSVC 64-bit**:
   ```cmd
   C:\vcpkg\vcpkg.exe install zlib:x64-windows
   ```
3. **Укажите проекту на vcpkg.** В Qt Creator: *Projects → Build & Run →
   ваш набор → Build Settings*, и в поле **CMake arguments** добавьте:
   ```
   -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
   ```
   Эквивалент для командной строки при конфигурации с нуля показан
   в примере ниже.

Готовое дерево — `build/win-msvc2022` (пресет `dev-win` из
`CMakePresets.json`: **Ninja**, Debug, Qt в `C:/Qt/6.10.3/msvc2022_64`,
toolchain vcpkg). Инкрементальные сборки переиспользуют его напрямую;
сборка с нуля — те же две команды после удаления дерева (ADR 176):

```bat
:: In an “MSVC … x64 Developer Command Prompt” (vcvars64.bat):
cmake --preset dev-win             :: configure (skip if the tree exists)
cmake --build --preset dev-win     :: build llocr + tests
set PATH=C:\Qt\6.10.3\msvc2022_64\bin;%PATH%   :: Qt DLLs for running tests
ctest --test-dir build\win-msvc2022 -j 4
```

В Qt Creator набор «Desktop Qt 6.10.3 MSVC2022 64bit» работает со своим
генератором **NMake Makefiles JOM**, а `dev-win` использует **Ninja** — оба
собираются с чистого дерева после relayout по ADR 176 (QML-модуль объявлен
рядом с целью `llocr`, а дерево QML живёт в `src/qml/`; до этого свежие
сборки падали с «dependent 'src\llocr_autogen\timestamp' does not exist»).
Старые деревья `build/Desktop_Qt_6_10_3_MSVC2022_64bit_*` снова собираются
после `cmake .` внутри них.

## Установка, тесты, запуск
```sh
# from the repo root (see AGENTS.md for the exact Qt location):
cmake --build build -j 8            # app + tests
ctest --test-dir build              # all unit tests (base + runtime suite)
# run the app:
./build/bin/llocr                   # (exact binary name per platform)
```
Юнит-тесты зарегистрированы в `tests/CMakeLists.txt`, включая
`test_document_model` и `test_djvu_document` (оба компилируют
`DocumentModel` и `DjVuDocument` и линкуют DjVuLibre), плюс набор рантайма
и вспомогательный бинарник `mock_llama_server`. Текущий список целей — через
`ctest --test-dir build -N`.

## Вспомогательные скрипты разработчика
- `scripts/reset-settings-linux.sh` / `scripts/reset-settings-macos.sh` /
  `scripts/reset-settings-windows.bat` — стирают `QSettings`, корень
  рантайма вместе с его моделями, кэши и прочие данные приложения, чтобы
  сымитировать свежий профиль.
- `scripts/convert-hf-gguf.py` — конвертирует чекпоинт Hugging Face в
  GGUF-квантизации и выдаёт фрагмент профиля (sha256/sizeBytes) для
  подключения новой модели.
- `scripts/probe-ocr-reply.py` — снимает сырой OCR-ответ модели для одной
  страницы (запрос, который отправляет само приложение), чтобы отличить
  плохой ответ модели от плохого разбора.

## macOS: ключ API и dev-сборка
Ключ API живёт в login Keychain, и Keychain привязывает элемент к коду,
который его создал. Dev-сборка подписана ad-hoc, а идентичность ad-hoc
подписанного бинарника — хэш его кода; **каждая пересборка меняет её**,
поэтому элемент становится недостижимым: приложение после перезапуска не
находит ключ, а Keychain запрашивает разрешение, на которое фоновое чтение
ответить не может (ADR 232).

Дайте dev-сборке стабильную идентичность — один раз:

```sh
scripts/macos-dev-sign.sh --create   # self-signed "LLocr Dev" in its own keychain
```

Скрипт создаёт сертификат с расширенным назначением ключа для подписи кода
(сертификат без него — не идентичность подписи: связка ключей его сохранит,
а политика подписи кода отвергнет), импортирует его вместе с ключом одним
бандлом, добавляет в настройки доверия пользователя, помещает его связку
ключей в список поиска связок ключей (codesign ищет там идентичность
подписи и игнорирует собственную опцию `--keychain`) и проверяет, что
идентичность действительно там. Если её нет, скрипт печатает то, что система
сообщает о связке ключей. После этого сборка подписывает бинарник сама
(шаг ничего не делает, пока идентичности нет), и Keychain сохраняет ключ
между пересборками. Релизную сборку по той же причине нужно подписывать
Developer ID: дистрибуция и так подписывается (§ Дистрибуция).

**Ключ, записанный прежней сборкой, остаётся привязанным к ней.** Его читают
ещё раз — Keychain запрашивает разрешение, и «Always Allow» добавляет
текущую сборку к элементу, — либо он удаляется в Keychain Access (элемент
`org.llocr.app` / `provider/apiKey`) и вводится заново. Дальше стабильная
идентичность его удерживает.

Пока элемент недостижим, приложение сообщает об этом вместо тихого отказа:
вкладка настроек называет причину, и запрос вообще не отправляется — сервер
ответил бы `401 API key required`, а это ничего не говорит о Keychain
(ADR 233). Ответьте на запрос Keychain «Always Allow» либо удалите элемент
в Keychain Access и введите ключ заново.

Ключ никогда не пишется в файл настроек, и файлового резерва нет: если
Keychain отвергает ключ, приложение сообщает об этом и держит ключ в памяти
до своего выхода.

## Каталоги данных (управляемый рантайм и модели)
Управляемый рантайм, модели, кэш и журналы живут под платформенным корнем
app-data (по умолчанию `<AppData>/LLocr/`, переопределяется через
`runtime/rootDir` и `runtime/modelsDir`):

```
<AppData>/LLocr/
├── runtime/
│   ├── .install.lock                       # QLockFile (installs)
│   ├── staging/<uuid>/                     # temporary extraction
│   └── llama.cpp-<build>-<backend>-<os>-<arch>/  # installed build
│       └── llama-server[.exe]
├── models/
│   ├── .registry.lock
│   ├── index.json                          # ModelRegistry
│   ├── catalog.json                        # user preset catalog
│   └── <org>__<repo>/                      # downloaded GGUF(s) + .part
├── cache/
│   └── releases.json                       # GitHub releases cache (TTL 6 h)
└── logs/
    └── llama-server.log                    # rotating, 5 MB × 3
```

Примечания:
- `runtime/` и `models/` можно переносить через настройки (`SettingsStore`
  `runtime/rootDir`, `runtime/modelsDir`); при смене пути файлы **не**
  перемещаются — UI предупреждает и предлагает пересканировать реестр
  моделей.
- Частичные загрузки (`.part` + `.part.meta`) всегда живут **рядом** с
  целевым файлом, чтобы финальное переименование оставалось атомарным
  (ADR 40) — не в общем каталоге downloads/.
- Блокировка единственного экземпляра (владельца рантайма) —
  `<rootDir>/.instance.lock`; когда её держит другой экземпляр, операции
  **сервера** Managed блокируются (External продолжает работать). Установки
  охраняются отдельно через `runtime/.install.lock`, а записи индекса
  моделей — через `models/.registry.lock` (ADR 46), так что 2-й экземпляр
  может устанавливать рантайм/модели, пока 1-й использует External.
- `owner.json` (пишется `ProcessGuard` на macOS) записывает PID/порт
  управляемого сервера, чтобы при следующем старте можно было обнаружить
  осиротевший сервер (ADR 30, ADR 107). Он читается на старте: футер
  предлагает «Stop it», когда записанный процесс ещё работает и его образ
  совпадает, — автоматически он никогда не убивается.
- Переменные окружения: ни одна не требуется. Для загрузок приложение
  следует платформенным настройкам прокси
  (`QNetworkProxyFactory::useSystemConfiguration()`). Существует один
  **опциональный** (opt-in) отладочный переключатель: `LLOCR_RAW_DEBUG=1`
  заставляет OCR-модель писать каждое тело запроса и каждый сырой ответ
  модели дословно в `<AppLocalDataDir>/raw-debug/` (по файлу на запрос,
  без ротации). Тело запроса содержит полное изображение страницы в base64,
  поэтому держите его выключенным, если разбор не нужно диагностировать
  (ADR 94).

## Структура репозитория (текущая)
```
LLocr/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── core/         # OcrResult, ConnectionConfig (transport), OcrRequest,
│   │                 #   LlamaClient, RequestProfile, LaunchProfile
│   ├── models/       # OcrModel (profile-driven adapter), VisionChatModel,
│   │                 #   BlockRecognitionModel, DecisionModel, LayoutModel,
│   │                 #   FixModel, FixDiffGate
│   ├── parsers/      # IOutputParser, ParserOptions, RawParser,
│   │                 #   DetTokenParserBase (shared engine),
│   │                 #   MinerULayoutParser (the markup pass),
│   │                 #   DetTokenFormat (shared text
│   │                 #   helpers), OtslTable (tables), ParserFactory,
│   │                 #   BlockStyleMap
│   │                 #   (label → style, from profiles/labels.json)
│   ├── runtime/      # ConnectionMode, ResolvedConnection, RuntimeState,
│   │                 #   RuntimeController, RuntimePaths, RuntimeLocator,
│   │                 #   ServerCapabilities, ServerLaunchConfig,
│   │                 #   LlamaServerProcess, ProcessGuard_*, DownloadTask,
│   │                 #   DownloadManager, ReleaseCatalog, ArchiveExtractor,
│   │                 #   InstallTransaction, ModelCatalog, ModelPreset,
│   │                 #   ModelPresetCatalog, ModelRegistry, ModelInstaller,
│   │                 #   RuntimeInstaller, ModelMemoryEstimator, SingleInstanceGuard
│   └── app/          # AppController, CheckController, DecisionController,
│                     #   FixController, LayoutController, DocumentModel, DjVuDocument,
│                     #   PageListModel, BoxListModel, PageEditStore,
│                     #   OcrImageProvider, SettingsStore, UiController,
│                     #   I18n, Exporter, PageIndex.h
├── resources/
│   ├── qml/          # Main.qml, SettingsDialog.qml, ExportDialog.qml,
│   │   │             #   Theme.qml, WindowSettings.qml, SetupWizard.qml,
│   │   │             #   ServerLogWindow.qml, ModelsTab.qml
│   │   ├── MainWindow/  # Header, ThumbPanel, ThumbDelegate, ImagePanel,
│   │   │                #   ImagePreview, WorkPanel, MarkdownPreview, Footer
│   │   └── Setup/       # StepWelcome, StepRuntime, StepModel, StepLaunch, StepDone
│   ├── preview/      # marked + KaTeX + preview.html (Markdown preview)
│   ├── models/       # default-presets.json (built-in preset catalog)
│   ├── icons/         # app-icon.svg + llocr-*.png + llocr.ico / llocr.icns +
│   │                  #   hicolor/** (Linux) + llocr.rc (Win) + llocr.desktop.in
│   └── i18n/          # llocr_ru.ts (compiled/embedded by qt_add_translations)
├── tests/            # document/DjVu + base + runtime suites; mock_llama_server helper
├── docs/
└── AGENTS.md
```

> **Примечание:** `OcrRequest` живёт в `core/OcrRequest.h`;
> `ConnectionConfig` (транспорт, бывший `ProviderConfig`) и `LlamaClient`
> живут в `src/core/`; адаптеры OCR-моделей живут в `src/models/`;
> `ResolvedConnection` и слой рантайма живут в `src/runtime/`. QML живёт
> под `src/qml/`.
