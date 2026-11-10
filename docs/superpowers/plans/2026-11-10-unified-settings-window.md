# Единое окно настроек — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Свести пять раздельных окон настроек (Интерфейс, Вывод, Проверка, Runtime, Модели) в одно окно: список разделов слева, содержимое справа, одна «Сохранить»/«Отмена» на всё, мастер настройки и сброс в подвале.

**Architecture:** Новое окно `SettingsWindow.qml` (`ApplicationWindow`) держит список разделов (`SettingsSectionNav.qml`) и пять страниц-разделов, у каждой один и тот же интерфейс: `loadValues()` / `saveValues()` / `cancelValues()` / `resetValues()` и сигнал `edited()`. Живые разделы (Интерфейс, Проверка) применяют выбор сразу и откатываются снимком при «Отмене»; черновиковые (Вывод, Runtime, Модели) пишутся по «Сохранить». Старые пять окон удаляются, пункт меню тулбара остаётся один.

**Tech Stack:** Qt6 / QML (QtQuick Controls 2), C++ (`llocr_config`), CMake, QTest + QQmlEngine для QML-тестов.

**Spec:** `docs/superpowers/specs/2026-11-10-unified-settings-window-design.md` — план спорит из него; исполнителю читать оба.

## Global Constraints

- **Коммиты делают субагенты-исполнители** — по одному коммиту на задачу, на ветке `feature/unified-settings` (пользователь разрешил 10.11.2026, чтобы ревью строило diff-пакеты). Историю пользователь может переписать; коммиты задач — технические, а не релизные.
- После каждой задачи — зелёный `scripts/check.sh`; после задач, добавляющих QML-файлы (T1–T4), первый прогон — `scripts/check.sh --configure` (новые `.qml` требуют re-glob модуля).
- Слои (ADR 106): новый код — только QML (`llocr_app` не растёт); единственное изменение C++ — `src/config/SettingsStore.cpp` (T5), то есть слой `llocr_config`.
- UI-строки — `qsTr` (английский источник) + правка `resources/i18n/llocr_ru.ts` (T11). Размеры и шрифты текста и иконок в QML — только `Theme.*` (ADR 293, правило 6 AGENTS.md); ширины и высоты раскладки — числами, как в остальных окнах (`width: 380` в `UiSettingsWindow`), в `Theme` для них токенов нет.
- Комментарии — по правилу 7 AGENTS.md: только «почему», никаких пересказов кода и баннеров.
- Иконки — `resources/icons/ui/<name>.svg`, каталог подхватывается глобом `resources/icons/ui/*.svg` в `src/CMakeLists.txt` (правки CMake не нужны). Глифы — monochrome, `stroke=currentColor`, как остальные.
- Секреты (ADR 227): ключ API и токен HF живут в OS-хранилище; сброс настроек их **не** трогает, и они никогда не попадают в `QSettings`, лог, командную строку или файл проекта.
- Тесты: каждый новый таргет — `tests/CMakeLists.txt` + `add_test(NAME ... COMMAND ...)`; таймаут и `QT_QPA_PLATFORM=offscreen` проставляются в конце файла автоматически (ADR 99).
- Windows собирается только MSVC (ADR 54); ничего в плане этого не меняет.

## Review Focus

- **Секреты при сбросе.** `resetRuntimeDefaults()` сегодня вызывает `dropSecret(kApiKey)` и `clearTaskConnections()` (тот роняет и ключи задач). После T5 сброс не должен трогать OS-хранилище — иначе «Сбросить все настройки» уничтожит ключ API. Пин: T5, шаг 3; ручной пункт T9.
- **Установленное не теряется.** `resetToDefaults()` чистит `QSettings` целиком, а в таблице значений есть `tasks/check/modelPath` и подобные — пути установленных моделей. После T5 эти пути (и `runtime/rootDir`, `runtime/modelsDir`, `runtime/setupVersion`) переживают сброс. Пин: T5, шаг 2.
- **Живые разделы откатываются.** Смена темы в разделе Интерфейс и переключение типа блока в разделе Проверка применяются сразу; «Отмена» обязана вернуть прежние значения. Пин: T2 (снимки), T7 (тест отката).
- **«Сохранить» не закрывает окно, если модель не скачана.** `applyTaskChoices()` возвращает список недостающих моделей — тогда показывается диалог загрузки, а окно остаётся открытым. Пин: T3 (диалог), T4 (шаг с `saveValues()` → bool).
- **Мастер — модальный диалог главного окна.** Открытый из окна настроек, он перекроет его (оверлей главного окна). Окно настроек на время мастера скрывается и возвращается после его закрытия. Пин: T7.
- **Пять старых окон удаляются только после того, как новое окно работает.** Порядок задач: T4 (новое окно) → T6 (удаление старых). Пин: порядок задач.

---

### Task 1: Список разделов слева

**Files:**
- Create: `src/qml/Settings/SettingsSectionNav.qml`
- Create: `resources/icons/ui/server.svg`, `resources/icons/ui/box.svg`
- Create: `tests/test_settings_nav_qml.cpp`
- Modify: `tests/CMakeLists.txt` (новый таргет, рядом с `test_segmented_control_qml`, ~строка 589)

**Interfaces:**
- Consumes: `Theme` (синглтон `LLocr`), `Icon` (`src/qml/Common/Icon.qml`).
- Produces: `SettingsSectionNav` — `ListView` с обязательным свойством `sections` (массив `{ id, title, icon }`), сигнал `sectionPicked(string sectionId)`, свойство `currentIndex` (наследуется от `ListView`), вычисляемое `currentSectionId` и `currentSectionTitle`.

- [ ] **Step 1: Написать падающий тест** `tests/test_settings_nav_qml.cpp`:

```cpp
// The section list on the left of the settings window (ADR 297).
//
// The nav is a plain list: the window owns the sections, the nav reports the
// pick. The test loads the shipped QML from the source tree and clicks the
// real rows — the list is what the user touches.

#include <QDir>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest>

#include "app/UiController.h"
#include "config/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

class TestSettingsNavQml : public QObject
{
    Q_OBJECT

    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase();
    void theListShowsTheFiveSections();
    void aClickReportsThePickedSection();

private:
    std::unique_ptr<QObject> loadNav();
    QObject *itemAt(QObject *nav, int index);
    void clickItem(QObject *item);

    QString m_qmlRoot;
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<UiController> m_uiController;
    std::unique_ptr<QQmlEngine> m_engine;
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_parent = nullptr;
    QStringList m_warnings;
};

void TestSettingsNavQml::initTestCase()
{
    m_qmlRoot = QFINDTESTDATA(QStringLiteral("../src/qml"));
    QVERIFY(!m_qmlRoot.isEmpty());

    m_settings = std::make_unique<SettingsStore>();
    m_uiController = std::make_unique<UiController>(*m_settings);
    qmlRegisterSingletonInstance("LLocr", 1, 0, "UiController", m_uiController.get());

    const auto registerFile = [](const QString &path, const char *name, bool singleton) {
        const QUrl url = QUrl::fromLocalFile(path);
        if (singleton)
            qmlRegisterSingletonType(url, "LLocr", 1, 0, name);
        else
            qmlRegisterType(url, "LLocr", 1, 0, name);
    };
    registerFile(m_qmlRoot + QStringLiteral("/Theme.qml"), "Theme", true);
    registerFile(m_qmlRoot + QStringLiteral("/Common/Icon.qml"), "Icon", false);
    registerFile(m_qmlRoot + QStringLiteral("/Common/LLOLabel.qml"), "LLOLabel", false);
    registerFile(m_qmlRoot + QStringLiteral("/Settings/SettingsSectionNav.qml"), "SettingsSectionNav", false);

    m_engine = std::make_unique<QQmlEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &reported) {
        for (const QQmlError &error : reported)
            m_warnings.append(error.toString());
    });

    m_window = std::make_unique<QQuickWindow>();
    m_parent = new QQuickItem(m_window->contentItem());
    m_parent->setSize(QSizeF(800, 600));
    m_window->resize(800, 600);
    m_window->show();
}
```

Тело тестов:

```cpp
void TestSettingsNavQml::theListShowsTheFiveSections()
{
    std::unique_ptr<QObject> nav = loadNav();
    QVERIFY(nav);
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(QStringLiteral("\n"))));

    QVariant count = nav->property("count");
    QCOMPARE(count.toInt(), 5);

    const QVariantList sections = nav->property("sections").toList();
    QCOMPARE(sections.size(), 5);
    QCOMPARE(sections.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("interface"));
    QCOMPARE(sections.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("output"));
    QCOMPARE(sections.at(2).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("verification"));
    QCOMPARE(sections.at(3).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("runtime"));
    QCOMPARE(sections.at(4).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("models"));
    // Every row carries a glyph that exists in the icon set.
    for (const QVariant &entry : sections) {
        const QString icon = entry.toMap().value(QStringLiteral("icon")).toString();
        const QString id = entry.toMap().value(QStringLiteral("id")).toString();
        QVERIFY2(!icon.isEmpty(), qPrintable(id));
        const QString glyph = QFINDTESTDATA(QStringLiteral("../../resources/icons/ui/") + icon + QStringLiteral(".svg"));
        QVERIFY2(!glyph.isEmpty(), qPrintable(icon));
    }
}

void TestSettingsNavQml::aClickReportsThePickedSection()
{
    std::unique_ptr<QObject> nav = loadNav();
    QVERIFY(nav);

    QSignalSpy spy(nav.get(), SIGNAL(sectionPicked(QString)));
    QObject *row = itemAt(nav.get(), 2);
    QVERIFY(row);
    clickItem(row);

    QCOMPARE(nav->property("currentIndex").toInt(), 2);
    QCOMPARE(nav->property("currentSectionId").toString(), QStringLiteral("verification"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("verification"));
}
```

Вспомогательные функции:

```cpp
    std::unique_ptr<QObject> loadNav()
    {
        m_warnings.clear();

        // The window owns the sections; the test plays the window's part.
        QVariantList sections;
        const QList<QPair<QString, QString>> rows = {
            {QStringLiteral("interface"), QStringLiteral("Interface")},
            {QStringLiteral("output"), QStringLiteral("Output")},
            {QStringLiteral("verification"), QStringLiteral("Verification")},
            {QStringLiteral("runtime"), QStringLiteral("Runtime")},
            {QStringLiteral("models"), QStringLiteral("Models")},
        };
        for (const auto &row : rows) {
            QVariantMap entry;
            entry.insert(QStringLiteral("id"), row.first);
            entry.insert(QStringLiteral("title"), row.second);
            entry.insert(QStringLiteral("icon"), row.first == QStringLiteral("interface") ? QStringLiteral("monitor")
                                : row.first == QStringLiteral("output") ? QStringLiteral("file-text")
                                : row.first == QStringLiteral("verification") ? QStringLiteral("circle-check")
                                : row.first == QStringLiteral("runtime") ? QStringLiteral("server")
                                : QStringLiteral("box"));
            sections.append(entry);
        }

        const QString path = QDir(m_qmlRoot).filePath(QStringLiteral("Settings/SettingsSectionNav.qml"));
        QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
        if (!component.isReady()) {
            m_warnings.append(component.errorString());
            return nullptr;
        }

        // The sections are a required property: they come with the object.
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{QStringLiteral("sections"), sections}}));
        if (!object) {
            m_warnings.append(component.errorString());
            return nullptr;
        }
        if (auto *item = qobject_cast<QQuickItem *>(object.get())) {
            item->setParentItem(m_parent);
            item->setWidth(200);
            item->setHeight(400);
        }
        // The delegates are created in the polish: let it happen before the test
        // asks for a row.
        QTest::qWait(20);
        return object;
    }

    QObject *itemAt(QObject *nav, int index)
    {
        // A delegate is not a child of the ListView: it comes from itemAtIndex().
        QQmlExpression expression(QQmlEngine::contextForObject(nav), nav,
                                  QStringLiteral("itemAtIndex(%1)").arg(index));
        return expression.evaluate().value<QObject *>();
    }

void TestSettingsNavQml::clickItem(QObject *item)
{
    auto *quick = qobject_cast<QQuickItem *>(item);
    QVERIFY(quick);
    const QPointF center = quick->mapToScene(QPointF(quick->width() / 2.0, quick->height() / 2.0));
    QTest::mouseClick(m_window.get(), Qt::LeftButton, Qt::NoModifier, center.toPoint());
}
```

- [ ] **Step 2: Зарегистрировать таргет** в `tests/CMakeLists.txt` (рядом с `test_segmented_control_qml`):

```cmake
qt_add_executable(test_settings_nav_qml test_settings_nav_qml.cpp)

target_link_libraries(test_settings_nav_qml PRIVATE
    Qt6::Test
    Qt6::Quick
    Qt6::QuickControls2
    llocr_app
)
llocr_add_ui_icons(test_settings_nav_qml)
add_test(NAME test_settings_nav_qml COMMAND test_settings_nav_qml)
```

- [ ] **Step 3: Убедиться, что тест падает** — `cmake --build build -j 8 --target test_settings_nav_qml && ctest --test-dir build -R test_settings_nav_qml` — FAIL: файл `Settings/SettingsSectionNav.qml` не найден.

- [ ] **Step 4: Написать `src/qml/Settings/SettingsSectionNav.qml`**:

```qml
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import LLocr
import "../Common"

// The section list on the left of the settings window (ADR 297). The window
// owns the sections; the nav only reports the pick.
ListView {
    id: root

    required property var sections
    signal sectionPicked(string sectionId)

    implicitWidth: 200
    clip: true
    currentIndex: 0
    spacing: 0
    model: root.sections
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    readonly property string currentSectionId: {
        if (root.currentIndex < 0 || root.currentIndex >= root.sections.length)
            return ""
        return root.sections[root.currentIndex].id
    }

    readonly property string currentSectionTitle: {
        if (root.currentIndex < 0 || root.currentIndex >= root.sections.length)
            return ""
        return root.sections[root.currentIndex].title
    }

    delegate: Item {
        id: row

        required property var modelData
        required property int index

        width: ListView.view.width
        height: Theme.rowHeightLarge

        Rectangle {
            anchors.fill: parent
            radius: Theme.controlRadius
            color: row.index === root.currentIndex ? Theme.selected
                 : (rowHover.hovered ? Theme.surfaceSunken : "transparent")
        }

        Icon {
            id: rowIcon

            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            name: row.modelData.icon
            size: Theme.iconSizeMedium
            color: row.index === root.currentIndex ? Theme.textPrimary : Theme.textSecondary
        }

        LLOLabel {
            anchors.left: rowIcon.right
            anchors.leftMargin: Theme.spacingSmall
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            text: row.modelData.title
            elide: Text.ElideRight
            font.pixelSize: Theme.bodySize
            color: row.index === root.currentIndex ? Theme.textPrimary : Theme.textSecondary
        }

        HoverHandler { id: rowHover }

        TapHandler {
            onTapped: {
                root.currentIndex = row.index
                root.sectionPicked(row.modelData.id)
            }
        }
    }
}
```

- [ ] **Step 5: Добавить иконки** `resources/icons/ui/server.svg` и `resources/icons/ui/box.svg` — monochrome-глифы в стиле остальных (24×24, `stroke="currentColor"`, `stroke-width="2"`, `stroke-linecap="round"`, `stroke-linejoin="round"`, без `fill`). Содержимое `server.svg`:

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <rect width="20" height="8" x="2" y="2" rx="2" ry="2"/>
  <rect width="20" height="8" x="2" y="14" rx="2" ry="2"/>
  <line x1="6" x2="6.01" y1="6" y2="6"/>
  <line x1="6" x2="6.01" y1="18" y2="18"/>
</svg>
```

`box.svg`:

```svg
<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
  <path d="M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16Z"/>
  <path d="m3.3 7 8.7 5 8.7-5"/>
  <path d="M12 22V12"/>
</svg>
```

- [ ] **Step 6: Собрать и прогнать** — `cmake --build build -j 8 --target test_settings_nav_qml && ctest --test-dir build -R test_settings_nav_qml` — PASS.

- [ ] **Step 7: Полный прогон** — `scripts/check.sh --configure` (новые QML-файлы требуют re-glob) — зелёный.

---

### Task 2: Страницы-разделы Интерфейс, Вывод, Проверка, Runtime

**Files:**
- Create: `src/qml/Settings/InterfaceSection.qml`
- Create: `src/qml/Settings/OutputSection.qml`
- Create: `src/qml/Settings/VerificationSection.qml`
- Create: `src/qml/Settings/RuntimeSection.qml`
- Modify: `src/qml/Common/InterfaceSettings.qml` (сигнал `edited()`)
- Modify: `src/qml/Settings/OutputTab.qml` (сигнал `edited()`, флаг `loading`)
- Modify: `src/qml/Settings/VerificationBlocksTab.qml` (`edited()` при переключении типа блока и «Выбрать все»/«Снять все»)
- Modify: `src/qml/Settings/RuntimeTab.qml` (сигнал `edited()`)

**Interfaces:**
- Consumes: `Settings`, `UiController`, `I18n`, `Verification`, `RuntimeInstaller`, `Runtime` (синглтоны `LLocr`); существующие `InterfaceSettings`, `OutputTab`, `VerificationBlocksTab`, `RuntimeTab`.
- Produces: единый контракт раздела — `signal edited()`, `function loadValues()`, `function saveValues()`, `function cancelValues()`, `function resetValues()`. У трёх «живых» страниц (`InterfaceSection`, `VerificationSection`, `RuntimeSection`) — ещё `function beginSession()`: снимок отката берётся один раз за сеанс окна, а не при каждой загрузке значений. `saveValues()` раздела «Модели» (T3) возвращает `bool` (false — модели не скачаны, окно не закрывается); остальные возвращают `undefined`.

- [ ] **Step 1: Добавить сигнал `edited()` в `src/qml/Common/InterfaceSettings.qml`.** В корневой `ColumnLayout` объявить `signal edited()`; в обработчике языка (`onActivated` у `languageBox`) добавить строку `root.edited()`, в `onActivated` сегмент-контрола темы — тоже. Корневому элементу нужен `id: root`:

```qml
ColumnLayout {
    id: root

    spacing: Theme.spacingSmall

    signal edited()
```

и в обработчиках:

```qml
        onActivated: (idx) => {
            Settings.language = ["system", "en", "ru"][idx]
            I18n.setLanguage(Settings.language)
            root.edited()
        }
```

```qml
        onActivated: (idx) => {
            UiController.mode = [UiController.System, UiController.Light,
                                 UiController.Dark][idx]
            root.edited()
        }
```

- [ ] **Step 2: Добавить сигнал `edited()` и флаг `loading` в `src/qml/Settings/OutputTab.qml`.** В корень добавить `id: root` (уже есть) и `signal edited()`, `property bool loading: false`. В `loadValues()` поставить `root.loading = true` в начале и `root.loading = false` в конце; в `saveValues()` — без флага. Обработчики:

```qml
    LLOComboBox {
        id: parserBox
        Layout.fillWidth: true
        model: root.parserModel
        textRole: "label"
        onActivated: if (!root.loading) root.edited()
    }
```

```qml
    OptionCheck {
        id: splitPagesCheck
        label: qsTr("Split pages")
        onToggled: if (!root.loading) root.edited()
    }
```

то же для `pageNumbersCheck`, `tablesAsHtmlCheck`, `orientationCheckBox`, `orientationAutoTurnCheck`; для `marginSpin` — `onValueChanged: if (!root.loading) root.edited()`; для сегмент-контрола ориентации — `onActivated: (idx) => { root.orientationIndex = idx; if (!root.loading) root.edited() }`.

- [ ] **Step 3: Добавить `edited()` в `src/qml/Settings/VerificationBlocksTab.qml`.** В `BlockTypeRow` обработчик `TapHandler` дописать `root.edited()` после `setEnabled`:

```qml
        TapHandler {
            onTapped: {
                const sourceRow = Verification.blockModel.rowOfType(typeRow.type)
                if (sourceRow >= 0) {
                    Verification.blockModel.setEnabled(sourceRow, !typeRow.checkEnabled)
                    root.edited()
                }
            }
        }
```

и в обработчики «Выбрать все» / «Снять все»:

```qml
                TapHandler {
                    onTapped: {
                        Verification.blockModel.setAllEnabled(true)
                        root.edited()
                    }
                }
```

```qml
                TapHandler {
                    onTapped: {
                        Verification.blockModel.setAllEnabled(false)
                        root.edited()
                    }
                }
```

- [ ] **Step 4: Добавить сигнал `edited()` в `src/qml/Settings/RuntimeTab.qml`.** Корень уже имеет `id: runtimeLayout`; добавить `signal edited()` и в обработчики: `connectionTimeoutField.onTextChanged`, `responseTimeoutField.onTextChanged` → `runtimeLayout.edited()`; сегмент-контрол режима подключения:

```qml
            onActivated: (idx) => {
                Settings.connectionMode = (idx === 1) ? "managed" : "external"
                runtimeLayout.edited()
            }
```

- [ ] **Step 5: Написать `src/qml/Settings/InterfaceSection.qml`** — язык и тема применяются сразу, откат — по снимку:

```qml
pragma ComponentBehavior: Bound

import QtQuick

import LLocr
import "../Common"

// The interface settings apply at once — the theme must be seen while it is
// picked — so the section keeps a snapshot and puts the old values back when
// the window is cancelled (ADR 297).
Item {
    id: root

    signal edited()

    property string snapshotLanguage: ""
    property int snapshotTheme: -1

    function loadValues() {
        root.snapshotLanguage = Settings.language
        root.snapshotTheme = UiController.mode
    }

    function saveValues() {
    }

    function cancelValues() {
        if (root.snapshotLanguage !== Settings.language) {
            Settings.language = root.snapshotLanguage
            I18n.setLanguage(Settings.language)
        }
        if (root.snapshotTheme !== UiController.mode)
            UiController.mode = root.snapshotTheme
    }

    function resetValues() {
        Settings.language = "system"
        I18n.setLanguage("system")
        UiController.mode = UiController.System
    }

    InterfaceSettings {
        anchors.fill: parent
        onEdited: root.edited()
    }
}
```

- [ ] **Step 6: Написать `src/qml/Settings/OutputSection.qml`**:

```qml
pragma ComponentBehavior: Bound

import QtQuick

import LLocr
import "../Common"

// The output settings are a draft: the widgets hold the values until the
// window saves them.
Item {
    id: root

    signal edited()

    function loadValues() {
        outputTab.loadValues()
    }

    function saveValues() {
        outputTab.saveValues()
    }

    function cancelValues() {
        outputTab.loadValues()
    }

    function resetValues() {
        Settings.resetOutputDefaults()
        outputTab.loadValues()
    }

    OutputTab {
        id: outputTab
        anchors.fill: parent
        onEdited: root.edited()
    }
}
```

- [ ] **Step 7: Написать `src/qml/Settings/VerificationSection.qml`** — типы блоков живут в модели C++ и пишутся сразу; откат — перечитывание файла промптов:

```qml
pragma ComponentBehavior: Bound

import QtQuick

import LLocr
import "../Common"

// The block types are edited in the live model and are written to the user
// file on save; the model's loadValues() re-reads the saved state, so it is
// also the undo.
Item {
    id: root

    signal edited()

    function loadValues() {
        blocksTab.loadValues()
        Verification.loadValues()
    }

    function saveValues() {
        blocksTab.saveValues()
        Verification.save()
    }

    function cancelValues() {
        root.loadValues()
    }

    function resetValues() {
        Verification.resetToDefaults()
        blocksTab.resetValues()
        root.loadValues()
    }

    VerificationBlocksTab {
        id: blocksTab
        anchors.fill: parent
        onEdited: root.edited()
    }
}
```

- [ ] **Step 8: Написать `src/qml/Settings/RuntimeSection.qml`** — снимок живых значений (режим подключения, backend, релиз) переносится из старого окна:

```qml
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import LLocr
import "../Common"

// The runtime section mixes a draft (the timeouts, the endpoint) with live
// writes (the connection mode, the backend, the release): the live ones are
// restored from the snapshot when the window is cancelled.
Item {
    id: root

    signal edited()

    property var applySnapshot: null

    function takeApplySnapshot() {
        root.applySnapshot = {
            connectionMode: Settings.connectionMode,
            backend: RuntimeInstaller.backend,
            selectedRelease: RuntimeInstaller.selectedRelease
        }
    }

    function restoreApplySnapshot() {
        if (!root.applySnapshot)
            return
        Settings.connectionMode = root.applySnapshot.connectionMode
        RuntimeInstaller.backend = root.applySnapshot.backend
        RuntimeInstaller.selectedRelease = root.applySnapshot.selectedRelease
    }

    function loadValues() {
        root.takeApplySnapshot()
        rtTab.loadValues()
        Runtime.refreshSingleInstanceLock()
    }

    function saveValues() {
        rtTab.saveValues()
    }

    function cancelValues() {
        root.restoreApplySnapshot()
        rtTab.loadValues()
    }

    function resetValues() {
        Settings.resetRuntimeDefaults()
        rtTab.loadValues()
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        contentHeight: rtTab.implicitHeight
        ScrollBar.vertical.policy: ScrollBar.AsNeeded
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        RuntimeTab {
            id: rtTab
            width: parent.width
            onEdited: root.edited()
        }
    }
}
```

- [ ] **Step 9: Собрать** — `cmake --build build -j 8` — без ошибок (qmllint проверит типы через `scripts/check.sh`).

- [ ] **Step 10: Полный прогон** — `scripts/check.sh --configure` — зелёный.

---

### Task 3: Раздел «Модели»

**Files:**
- Create: `src/qml/Settings/ModelsSection.qml`
- Modify: `src/qml/Common/TaskModelsTable.qml` (сигнал `edited()`, сигнал `runtimeSectionRequested()` вместо `runtimeSettingsRef`)
- Modify: `src/qml/Settings/LaunchTab.qml` (кнопка «Configure runtime…» просит раздел Runtime)

**Interfaces:**
- Consumes: `TaskModelsTable` (`syncDraft()`, `resetDraft()`, `saveTabs()`, `markDraftSaved()`, `buildChoices()`, `applyExternalDraft()`, `resetTabs()`, `fmtBytes()`), `ModelInstaller` (`refreshInstalled()`, `reloadPresets()`, `rescanRegistry()`, `applyTaskChoices()`, `busy`, `statusMessage`, `progress`, `taskQueueActive`, `installedFiles`, `installedCount`, `cancelTaskInstalls()`, `removeInstalledEntry()`, `openInstalledEntryFolder()`), `RoleProfiles` (`choiceTitles()`).
- Produces: `ModelsSection` с тем же контрактом раздела; `saveValues()` возвращает `true`, когда выборы записаны, и `false`, когда модель не скачана (окно должно остаться открытым); сигнал `runtimeSectionRequested()` — кнопка «Configure runtime…» в попапе задачи просит окно показать раздел Runtime.

- [ ] **Step 1: Добавить сигнал `edited()` в `src/qml/Common/TaskModelsTable.qml`.** В корень `Item` (уже `id: table`) объявить `signal edited()`; в обработчиках выбора модели (`onActivated` у `modelBox`) и кванта (`onActivated` у `quantBox`) дописать `table.edited()`. То же — в обработчике имени модели в внешнем режиме.

- [ ] **Step 1a: Заменить `runtimeSettingsRef` на сигнал `runtimeSectionRequested()`.** В `src/qml/Common/TaskModelsTable.qml` удалить свойство `property var runtimeSettingsRef: null` и добавить `signal runtimeSectionRequested()`; в `src/qml/Settings/LaunchTab.qml` заменить свойство `runtimeSettingsRef` на `property var hostTable: null` и переписать кнопку «Configure runtime…»:

```qml
        LLOButton {
            text: qsTr("Configure runtime…")
            onClicked: {
                if (root.hostTable)
                    root.hostTable.runtimeSectionRequested()
            }
        }
```

а в попапе таблицы (`TaskModelsTable.qml`) связать `LaunchTab` с таблицей:

```qml
                LaunchTab {
                    id: launchTab
                    role: taskSettingsPopup.task
                    hostTable: table
                }
```

`LaunchTab.qml` больше не знает про окно настроек — он просит таблицу, а та — окно.

- [ ] **Step 2: Написать `src/qml/Settings/ModelsSection.qml`** — содержимое переносится из `TaskModelsWindow` (таблица задач, очередь загрузки, файлы на диске, диалог «Models to download»):

```qml
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// The models section: the task rows (model and quantization per task), the
// download queue and the files on disk. It is a draft — the host applies it
// on save (ADR 297).
Item {
    id: root

    signal edited()
    // The popup of a task offers the runtime settings: in the unified window
    // that is the "Runtime" section.
    signal runtimeSectionRequested()

    readonly property bool externalMode: Settings.connectionMode === "external"

    function loadValues() {
        ModelInstaller.refreshInstalled()
        ModelInstaller.reloadPresets()
        ModelInstaller.rescanRegistry()
        taskTable.syncDraft()
    }

    function saveValues() {
        taskTable.saveTabs()
        if (root.externalMode) {
            taskTable.applyExternalDraft()
            return true
        }

        const missing = ModelInstaller.applyTaskChoices(taskTable.buildChoices())
        taskTable.markDraftSaved()
        if (missing.length === 0)
            return true

        missingDialog.items = missing
        missingDialog.open()
        return false
    }

    function cancelValues() {
        taskTable.resetDraft()
    }

    function resetValues() {
        taskTable.resetTabs()
        taskTable.resetDraft()
        root.loadValues()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        TaskModelsTable {
            id: taskTable
            Layout.fillWidth: true
            externalMode: root.externalMode
            onEdited: root.edited()
            onRuntimeSectionRequested: root.runtimeSectionRequested()
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: ModelInstaller.busy
            spacing: Theme.spacingSmall

            InstallerStatusLabel {
                Layout.fillWidth: true
                isError: ModelInstaller.state === ModelInstaller.Error
                busy: ModelInstaller.busy
                statusText: ModelInstaller.statusMessage
            }
            InstallerProgressRow {
                Layout.fillWidth: true
                busy: ModelInstaller.busy
                progress: ModelInstaller.progress
                cancelVisible: ModelInstaller.taskQueueActive
                onCancelClicked: ModelInstaller.cancelTaskInstalls()
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall

            LLOLabel {
                Layout.fillWidth: true
                font.pixelSize: Theme.bodySize
                color: Theme.textPrimary
                text: qsTr("Downloaded model files on disk")
            }

            LLOLabel {
                id: filesErrorLabel

                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.footnoteSize
                color: Theme.error
            }

            ListView {
                id: filesList

                Layout.fillWidth: true
                implicitHeight: Math.min(contentHeight, 300)
                clip: true
                spacing: Theme.spacingTiny
                visible: ModelInstaller.installedCount > 0
                model: ModelInstaller.installedFiles
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: RowLayout {
                    id: fileRow

                    required property string entryId
                    required property string title
                    required property string quant
                    required property string sizeLabel
                    required property int usageCount
                    required property string usageText
                    required property bool removable
                    required property string note

                    width: filesList.width
                    implicitHeight: Theme.controlHeight
                    spacing: Theme.spacingSmall

                    LLOLabel {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                        font.pixelSize: Theme.bodySmallSize
                        text: fileRow.title
                        color: Theme.textPrimary
                    }
                    LLOLabel {
                        Layout.preferredWidth: 64
                        font.pixelSize: Theme.bodySmallSize
                        text: fileRow.quant
                        color: Theme.textSecondary
                    }
                    LLOLabel {
                        Layout.preferredWidth: 76
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: Theme.bodySmallSize
                        text: fileRow.sizeLabel
                        color: Theme.textSecondary
                    }
                    LLOLabel {
                        Layout.preferredWidth: 165
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                        font.pixelSize: Theme.bodySmallSize
                        text: fileRow.usageText
                        color: fileRow.usageCount > 0 ? Theme.textPrimary : Theme.textMuted
                    }
                    ToolButton {
                        implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
                        implicitHeight: Math.max(Theme.controlHeight, topPadding + bottomPadding + contentItem.implicitHeight)
                        onClicked: filesErrorLabel.text = ModelInstaller.openInstalledEntryFolder(fileRow.entryId)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Open folder")

                        contentItem: Item {
                            implicitWidth: folderIcon.size
                            implicitHeight: folderIcon.size

                            Icon {
                                id: folderIcon
                                anchors.centerIn: parent
                                name: "folder"
                                size: Theme.iconSizeMedium
                                color: Theme.textSecondary
                            }
                        }
                    }
                    ToolButton {
                        implicitWidth: leftPadding + rightPadding + contentItem.implicitWidth
                        implicitHeight: Math.max(Theme.controlHeight, topPadding + bottomPadding + contentItem.implicitHeight)
                        onClicked: {
                            if (!fileRow.removable) {
                                filesErrorLabel.text = fileRow.note
                                return
                            }
                            deleteFileDialog.entryId = fileRow.entryId
                            deleteFileDialog.subject = fileRow.title + " · " + fileRow.quant
                            deleteFileDialog.usageCount = fileRow.usageCount
                            deleteFileDialog.open()
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: fileRow.removable ? qsTr("Delete") : fileRow.note

                        contentItem: Item {
                            implicitWidth: trashIcon.size
                            implicitHeight: trashIcon.size

                            Icon {
                                id: trashIcon
                                anchors.centerIn: parent
                                name: "trash-2"
                                size: Theme.iconSizeMedium
                                color: parent.enabled ? Theme.textSecondary : Theme.textMuted
                            }
                        }
                    }
                }
            }

            LLOLabel {
                Layout.fillWidth: true
                visible: ModelInstaller.installedCount === 0
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.footnoteSize
                color: Theme.helpColor
                text: qsTr("Downloaded models will appear here — pick a quantization in the rows above and save.")
            }
        }
    }

    Dialog {
        id: missingDialog

        property var items: []

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: false
        standardButtons: Dialog.NoButton
        title: qsTr("Models to download")

        ColumnLayout {
            spacing: Theme.spacingSmall

            Repeater {
                model: missingDialog.items

                delegate: LLOLabel {
                    required property var modelData
                    Layout.fillWidth: true
                    text: RoleProfiles.choiceTitles(modelData) + " — "
                          + modelData.model + " · " + modelData.quantId
                          + (modelData.size > 0 ? " (" + taskTable.fmtBytes(modelData.size) + ")" : "")
                }
            }
        }

        footer: DialogButtonBox {
            LLOButton {
                text: qsTr("Cancel")
                flat: true
                onClicked: missingDialog.close()
            }
            LLOButton {
                text: qsTr("Download")
                emphasis: true
                enabled: !ModelInstaller.busy
                onClicked: {
                    ModelInstaller.queueTaskInstalls(missingDialog.items)
                    missingDialog.close()
                }
            }
        }
    }

    Dialog {
        id: deleteFileDialog

        property string entryId: ""
        property string subject: ""
        property int usageCount: 0

        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 420
        title: qsTr("Delete model file")

        onOpened: deleteFileButton.forceActiveFocus()

        contentItem: LLOLabel {
            width: deleteFileDialog.availableWidth
            wrapMode: Text.WordWrap
            font.pixelSize: Theme.captionSize
            color: Theme.textSecondary
            text: deleteFileDialog.usageCount > 0
                  ? qsTr("%1 is used by %2 task(s). The task rows keep their choice, but the model is gone from disk until you download it again.").arg(deleteFileDialog.subject).arg(deleteFileDialog.usageCount)
                  : qsTr("Delete %1 from disk?").arg(deleteFileDialog.subject)
        }

        footer: Item {
            implicitHeight: buttons.implicitHeight + Theme.spacingLarge

            RowLayout {
                id: buttons
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.spacingLarge
                anchors.rightMargin: Theme.spacingLarge
                anchors.bottomMargin: Theme.spacing
                spacing: Theme.spacing

                Item { Layout.fillWidth: true }
                LLOButton {
                    text: qsTr("Cancel")
                    onClicked: deleteFileDialog.close()
                }
                LLOButton {
                    id: deleteFileButton
                    text: qsTr("Delete")
                    emphasis: true
                    Keys.onEscapePressed: deleteFileDialog.close()
                    onClicked: {
                        const err = ModelInstaller.removeInstalledEntry(deleteFileDialog.entryId)
                        deleteFileDialog.close()
                        filesErrorLabel.text = err
                    }
                }
            }
        }
    }
}
```

- [ ] **Step 3: Собрать** — `cmake --build build -j 8` — без ошибок.

- [ ] **Step 4: Полный прогон** — `scripts/check.sh --configure` — зелёный.

---

### Task 4: Окно настроек

**Files:**
- Create: `src/qml/Settings/SettingsWindow.qml`
- Modify: `src/qml/Main.qml` (загрузчик нового окна вместо пяти старых — старые загрузчики удаляются в T6; здесь добавляется новый)

**Interfaces:**
- Consumes: `SettingsSectionNav`, `InterfaceSection`, `OutputSection`, `VerificationSection`, `RuntimeSection`, `ModelsSection`, `SettingsFooter`, `SettingsSurface`, `Theme`.
- Produces: `SettingsWindow` — `bringToFront()`, `showSection(string sectionId)`, `loadAll()`, `saveAll()`, `cancelAll()`, `property bool dirty`, сигнал `wizardRequested()` (T7).

- [ ] **Step 1: Написать `src/qml/Settings/SettingsWindow.qml`**:

```qml
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LLocr
import "../Common"

// One window for every setting (ADR 297): the sections on the left, the
// content of the picked section on the right, one Save and one Cancel for
// all of them.
ApplicationWindow {
    id: window

    title: qsTr("Settings")
    width: 1040
    height: 700
    minimumWidth: 880
    minimumHeight: 560
    modality: Qt.NonModal

    // The user changed something in one of the sections.
    property bool dirty: false
    property bool forceClose: false

    // The wizard is a modal dialog of the main window: the settings window
    // steps aside while it is open.
    signal wizardRequested()

    readonly property var sections: [
        { id: "interface", title: qsTr("Interface"), icon: "monitor" },
        { id: "output", title: qsTr("Output"), icon: "file-text" },
        { id: "verification", title: qsTr("Verification"), icon: "circle-check" },
        { id: "runtime", title: qsTr("Runtime"), icon: "server" },
        { id: "models", title: qsTr("Models"), icon: "box" }
    ]

    function bringToFront() {
        if (visibility === Window.Minimized)
            showNormal()
        show()
        raise()
        requestActivate()
    }

    function showSection(sectionId) {
        for (let i = 0; i < window.sections.length; ++i) {
            if (window.sections[i].id === sectionId) {
                nav.currentIndex = i
                return
            }
        }
    }

    function loadAll() {
        interfaceSection.loadValues()
        outputSection.loadValues()
        verificationSection.loadValues()
        runtimeSection.loadValues()
        modelsSection.loadValues()
        window.dirty = false
    }

    // The models section answers false when a model is not on disk: the window
    // stays open and the download dialog is up.
    function saveAll() {
        if (modelsSection.saveValues() === false)
            return false
        outputSection.saveValues()
        verificationSection.saveValues()
        runtimeSection.saveValues()
        Settings.forceSave()
        window.dirty = false
        return true
    }

    function cancelAll() {
        interfaceSection.cancelValues()
        outputSection.cancelValues()
        verificationSection.cancelValues()
        runtimeSection.cancelValues()
        modelsSection.cancelValues()
        window.dirty = false
    }

    // The undo of the live sections is armed once per session: a hide and
    // show inside the session (the wizard round trip) must not re-arm it —
    // otherwise Cancel would have nothing to put back.
    property bool sessionStarted: false

    // The live pages take their undo snapshot here: the language and the
    // theme, the block types of the verification, the connection mode and
    // the backend.
    function armSession() {
        interfaceSection.beginSession()
        verificationSection.beginSession()
        runtimeSection.beginSession()
    }

    background: SettingsSurface {
    }

    onVisibleChanged: {
        if (!visible)
            return
        // The one-shot pass spent on a confirmed discard must not leak into
        // the next session with the window.
        window.forceClose = false
        // The values are read once per session: a reload on every show would
        // wipe the unsaved drafts of the output and the runtime sections.
        if (window.sessionStarted)
            return
        window.armSession()
        window.loadAll()
        window.sessionStarted = true
    }

    onClosing: (close) => {
        if (window.dirty && !window.forceClose) {
            close.accepted = false
            closeConfirmDialog.open()
            return
        }
        window.sessionStarted = false
        if (window.pendingWizard) {
            window.pendingWizard = false
            // The window is still visible while the closing signal runs: the
            // wizard must not start under it.
            Qt.callLater(function() { window.wizardRequested() })
        }
    }

    footer: SettingsFooter {
        LLOButton {
            subtle: true
            text: qsTr("Setup wizard")
            onClicked: window.requestWizard()
        }
        Item { Layout.fillWidth: true }
        LLOButton {
            text: qsTr("Cancel")
            onClicked: {
                window.cancelAll()
                window.close()
            }
        }
        LLOButton {
            emphasis: true
            text: qsTr("Save")
            onClicked: {
                if (window.saveAll())
                    window.close()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.paddingWindow
        spacing: Theme.spacingLarge

        SettingsSectionNav {
            id: nav

            Layout.preferredWidth: 200
            Layout.fillHeight: true
            sections: window.sections
            onSectionPicked: (sectionId) => window.showSection(sectionId)
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surfaceAlt
            border.color: Theme.border
            border.width: 1
            radius: Theme.controlRadius + 2

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacingLarge
                spacing: Theme.spacing

                LLOLabel {
                    Layout.fillWidth: true
                    text: nav.currentSectionTitle
                    font.pixelSize: Theme.subheadingSize
                    font.bold: true
                    color: Theme.textPrimary
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    InterfaceSection {
                        id: interfaceSection
                        anchors.fill: parent
                        visible: nav.currentIndex === 0
                        onEdited: window.dirty = true
                    }
                    OutputSection {
                        id: outputSection
                        anchors.fill: parent
                        visible: nav.currentIndex === 1
                        onEdited: window.dirty = true
                    }
                    VerificationSection {
                        id: verificationSection
                        anchors.fill: parent
                        visible: nav.currentIndex === 2
                        onEdited: window.dirty = true
                    }
                    RuntimeSection {
                        id: runtimeSection
                        anchors.fill: parent
                        visible: nav.currentIndex === 3
                        onEdited: window.dirty = true
                    }
                    ModelsSection {
                        id: modelsSection
                        anchors.fill: parent
                        visible: nav.currentIndex === 4
                        onEdited: window.dirty = true
                        onRuntimeSectionRequested: window.showSection("runtime")
                    }
                }
            }
        }
    }

    Dialog {
        id: closeConfirmDialog

        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 420
        title: qsTr("Discard changes?")
        standardButtons: Dialog.Yes | Dialog.No

        LLOLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
            text: qsTr("There are unsaved changes. Close without saving?")
        }

        onAccepted: {
            window.cancelAll()
            window.dirty = false
            window.forceClose = true
            window.close()
        }

        onRejected: {
            // The user kept the window: the wizard must not start later on.
            window.pendingWizard = false
        }
    }
}
```

- [ ] **Step 2: Подключить загрузчик окна в `src/qml/Main.qml`** (рядом с остальными загрузчиками; старые загрузчики удаляются в T7):

```qml
    Loader {
        id: settingsLoader
        active: false
        sourceComponent: SettingsWindow {}
    }
```

- [ ] **Step 3: Кнопка «Мастер настройки» в подвале.** Мастер применяет свои настройки — режим подключения, выбор моделей, параметры вывода, — то есть он **заменяет сеанс правки**: окно настроек закрывается, мастер идёт в своём родном окне, и никакие два модальных окна не спорят за передний план. Загрузчик окна направляет сигнал наружу:

```qml
    Loader {
        id: settingsLoader
        active: false
        sourceComponent: SettingsWindow {
            onWizardRequested: mainWindow.openSetupWizard()
        }
    }
```

`openSetupWizard()` в `src/qml/Main.qml` — если окно настроек открыто, мастер
заменяет и его: окно закрывается (с обычным вопросом о несохранённых
изменениях), иначе мастер запускается сразу:

```qml
    // The wizard applies its own settings, so it replaces the editing
    // session: the settings window closes and the wizard runs alone in the
    // main window. The window comes back only if the user opens it again —
    // with the wizard's values and a fresh undo.
    function openSetupWizard() {
        if (settingsLoader.active && settingsLoader.item && settingsLoader.item.visible) {
            settingsLoader.item.requestWizard()
            return
        }
        setupWizardLoader.active = true
        const wizard = setupWizardLoader.item as SetupWizard
        wizard.startWizard()
    }
```

В самом окне кнопка подвала просит закрыть сеанс и открыть мастер:

```qml
        LLOButton {
            subtle: true
            text: qsTr("Setup wizard")
            onClicked: window.requestWizard()
        }
```

```qml
    function requestWizard() {
        window.pendingWizard = true
        window.close()
    }
```

`property bool pendingWizard: false` рядом с `dirty`, а в `onClosing` — последнее действие принятого закрытия:

```qml
    onClosing: (close) => {
        if (window.dirty && !window.forceClose) {
            close.accepted = false
            closeConfirmDialog.open()
            return
        }
        window.sessionStarted = false
        if (window.pendingWizard) {
            window.pendingWizard = false
            // The window is still visible while the closing signal runs: the
            // wizard must not start under it.
            Qt.callLater(function() { window.wizardRequested() })
        }
    }
```

`Qt.callLater` обязателен: внутри `closing` окно ещё видимо, и прямой вызов
`wizardRequested()` привёл бы к рекурсии — `openSetupWizard()` увидел бы
видимое окно и позвал `requestWizard()` снова. Через `callLater` окно уже
скрыто, и мастер запускается один раз.

Диалог «Discard changes?» уже закрывает окно через `window.close()` — цепочка
доходит до `pendingWizard` и открывает мастер. Если пользователь ответил «No»
и оставил окно, мастер запускаться не должен — сбрось флаг в отказе диалога:

```qml
        onRejected: {
            window.pendingWizard = false
        }
```

- [ ] **Step 4: Собрать** — `cmake --build build -j 8` — без ошибок.

- [ ] **Step 5: Полный прогон** — `scripts/check.sh --configure` — зелёный.

---

### Task 5: Сброс, который не теряет установленное и не трогает секреты

**Files:**
- Modify: `src/config/SettingsStore.cpp` (`resetToDefaults()`, `resetRuntimeDefaults()`, `clearTaskConnections()`)
- Modify: `src/config/SettingsStore.h` (декларации, если понадобится)
- Modify: `src/qml/RoleProfiles.qml` (функция `resetRequestProfiles()`)
- Modify: `src/qml/Settings/SettingsWindow.qml` (кнопка сброса, диалог подтверждения, `resetAll()`)
- Modify: `tests/test_settings_store.cpp` (два новых слота)

**Interfaces:**
- Consumes: `SettingsStore::resetGroup()`, `dropSecret()`, `clearTaskConnections()`.
- Produces: `SettingsStore::resetToDefaults()` — чистит настройки, сохраняя пути установленного runtime и моделей, `runtime/setupVersion` и `runtime/setupDismissed`; `SettingsStore::resetRuntimeDefaults()` — сбрасывает группу runtime, **не** трогая OS-хранилище; `RoleProfiles::resetRequestProfiles()` — возвращает профили запроса всех задач к встроенным; `SettingsWindow::resetAll()` — сброс всех разделов.

- [ ] **Step 1: Написать падающий тест** в `tests/test_settings_store.cpp` — новый слот рядом с `theResetKeepsTheRuntimePaths()`:

```cpp
    // The paths of the models on disk point at the user's disk, not at the
    // product's schema: wiping them would make the app forget the models it
    // has installed (ADR 297).
    void theResetKeepsTheModelPaths()
    {
        SettingsStore store;
        store.setLaunchModelPathForRole(QStringLiteral("blockRecognition"), QStringLiteral("/tmp/llocr-models/m.gguf"));
        store.setLaunchModelPathForRole(QStringLiteral("decision"), QStringLiteral("/tmp/llocr-models/d.gguf"));
        store.setServerPath(QStringLiteral("/tmp/llocr-runtime/llama-server"));
        store.setInstalledBuild(QStringLiteral("b12000"));
        store.setSetupVersion(1);

        store.resetToDefaults();

        QCOMPARE(store.launchModelPathForRole(QStringLiteral("blockRecognition")), QStringLiteral("/tmp/llocr-models/m.gguf"));
        QCOMPARE(store.launchModelPathForRole(QStringLiteral("decision")), QStringLiteral("/tmp/llocr-models/d.gguf"));
        // The installed server and its build are on the user's disk too.
        QCOMPARE(store.serverPath(), QStringLiteral("/tmp/llocr-runtime/llama-server"));
        QCOMPARE(store.installedBuild(), QStringLiteral("b12000"));
        // The wizard must not come back by itself.
        QCOMPARE(store.setupVersion(), 1);
    }
```

- [ ] **Step 2: Убедиться, что тест падает** — `cmake --build build -j 8 --target test_settings_store && ctest --test-dir build -R test_settings_store` — FAIL (пути моделей теряются).

- [ ] **Step 3: Поправить существующий тест `resetRuntimeGroup()`** — сброс группы runtime больше не роняет ключ (шаг 5), поэтому проверка меняется:

```cpp
        // The key lives in the system secret store, not in the settings: a
        // reset of the settings does not throw it away (ADR 297).
        QCOMPARE(store.apiKey(), QStringLiteral("k"));
```

- [ ] **Step 4: Исправить `SettingsStore::resetToDefaults()`** в `src/config/SettingsStore.cpp`:

```cpp
void SettingsStore::resetToDefaults()
{
    // The installed runtime and the models on disk are not settings: their
    // paths survive the reset, or the app forgets what it has installed
    // (ADR 297). The wizard's own flags survive too — a reset must not
    // bring the first-run wizard back.
    const QStringList preserved = {
        QLatin1String(kRuntimeRootDir), QLatin1String(kRuntimeModelsDir),
        QLatin1String(kServerPath), QLatin1String(kInstalledBuild),
        QLatin1String(kCheckLaunchModelPath), QLatin1String(kCheckLaunchMmprojPath),
        QLatin1String(kCheckLaunchDraftPath),
        QLatin1String(kDecisionLaunchModelPath), QLatin1String(kDecisionLaunchMmprojPath),
        QLatin1String(kDecisionLaunchDraftPath),
        QLatin1String(kLayoutLaunchModelPath), QLatin1String(kLayoutLaunchMmprojPath),
        QLatin1String(kLayoutLaunchDraftPath),
        QLatin1String(kFixLaunchModelPath), QLatin1String(kFixLaunchMmprojPath),
        QLatin1String(kFixLaunchDraftPath),
        QLatin1String(kSetupVersion), QLatin1String(kSetupDismissed)
    };
    QHash<QString, QVariant> kept;
    for (const QString &key : preserved) {
        if (m_settings.contains(key))
            kept.insert(key, m_settings.value(key));
    }

    m_settings.clear();
    for (auto it = kept.constBegin(); it != kept.constEnd(); ++it)
        m_settings.setValue(it.key(), it.value());
    m_settings.setValue(kSettingsSchemaVersion, kCurrentSettingsSchemaVersion);
    m_settings.sync();
}
```

`kServerPath` (путь к установленному серверу) и `kInstalledBuild` (сборка,
которую считают активной) — тоже пути установленного, а не настройки: без
`kInstalledBuild` установщик перестаёт видеть обновление, а очистка старых
сборок отказывается работать («No active runtime build»), а без `kServerPath`
приложение не знает, где лежит установленный сервер.

- [ ] **Step 5: Исправить `SettingsStore::resetRuntimeDefaults()`** — сброс группы runtime не трогает OS-хранилище и не забывает путь к установленному серверу:

```cpp
void SettingsStore::resetRuntimeDefaults()
{
    resetGroup([](const QString &key) {
        return key == QLatin1String("provider/mode") || key == QLatin1String("provider/baseUrl") || key == QLatin1String("provider/connectionTimeoutMs") ||
               key == QLatin1String("provider/responseTimeoutMs") || key == QLatin1String("model/name") || key == QLatin1String("tasks/decision/modelName") ||
               key == QLatin1String("tasks/layout/modelName") || key == QLatin1String("tasks/fix/modelName");
    });
    // The keys live in the system secret store, not in the settings: a reset
    // of the settings does not throw them away (ADR 297).
    m_settings.remove(QString::fromLatin1(kApiKey));
    clearTaskEndpoints();
    emit secretsChanged();
}
```

Ключ `runtime/serverPath` из группы убран: это путь к установленному серверу,
а не настройка (то же правило, что и для путей моделей).

и `clearTaskConnections()` разделить на две части — в `src/config/SettingsStore.cpp`:

```cpp
// The per-task endpoints go back to the shared one; the keys stay in the
// secret store (ADR 297).
void SettingsStore::clearTaskEndpoints()
{
    bool endpointsChanged = false;
    for (const ConnectionRole role : connectionRoles) {
        const QString name = connectionRoleName(role);
        if (m_settings.contains(taskBaseUrlKey(name))) {
            m_settings.remove(taskBaseUrlKey(name));
            endpointsChanged = true;
        }
    }
    if (endpointsChanged)
        emit baseUrlChanged();
}
```

`clearTaskConnections()` (с удалением секретов) больше не нужен: он звался только
из `resetRuntimeDefaults()`, а удалять ключи из OS-хранилища сброс настроек не
должен. Поэтому функция заменяется на `clearTaskEndpoints()` — из `.h` и `.cpp`:

- [ ] **Step 6: Добавить `RoleProfiles::resetRequestProfiles()`** в `src/qml/RoleProfiles.qml`:

```qml
    // Every task's request profile goes back to the built-in one.
    function resetRequestProfiles() {
        for (const task of Object.keys(tasks)) {
            const store = entry(task).profiles()
            if (store && store.resetToDefaults)
                store.resetToDefaults()
        }
    }
```

- [ ] **Step 7: Добавить сброс в окно настроек.** В `src/qml/Settings/SettingsWindow.qml` добавить метод (рядом с `cancelAll()`):

```qml
    // The reset writes at once — like the old per-window "Restore defaults"
    // did — and the window stays open on the reset values. The settings are
    // reset once, globally; every page then brings its own widgets back to
    // the values the settings now hold. The live pages re-arm their undo
    // afterwards: a Cancel after the reset must keep the reset.
    function resetAll() {
        Settings.resetToDefaults()
        Verification.resetToDefaults()
        LaunchProfiles.resetToDefaults()
        RoleProfiles.resetRequestProfiles()
        interfaceSection.resetValues()
        outputSection.resetValues()
        verificationSection.resetValues()
        runtimeSection.resetValues()
        modelsSection.resetValues()
        window.armSession()
        window.loadAll()
    }
```

в подвал добавить кнопку (между «Мастер настройки» и разделителем):

```qml
        LLOButton {
            subtle: true
            text: qsTr("Reset all settings")
            onClicked: resetDialog.open()
        }
```

и рядом с диалогом закрытия — диалог подтверждения:

```qml
    Dialog {
        id: resetDialog

        parent: Overlay.overlay
        modal: true
        anchors.centerIn: parent
        width: 420
        title: qsTr("Reset all settings?")
        standardButtons: Dialog.Yes | Dialog.No

        LLOLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
            text: qsTr("Every setting goes back to its default, the custom verification prompts included. The installed runtime, the downloaded models and the keys in the system store stay.")
        }

        onAccepted: window.resetAll()
    }
```

- [ ] **Step 8: Прогнать** — `cmake --build build -j 8 && ctest --test-dir build -R test_settings_store` — PASS.

- [ ] **Step 9: Полный прогон** — `scripts/check.sh` — зелёный.

---

### Task 6: Меню тулбара и удаление старых окон

**Files:**
- Modify: `src/qml/MainWindow/Header.qml` (кнопка «Настройки» открывает окно; меню удаляется)
- Modify: `src/qml/Main.qml` (удалить пять загрузчиков и пять обработчиков сигналов)
- Delete: `src/qml/Settings/UiSettingsWindow.qml`, `src/qml/Settings/OutputSettingsWindow.qml`, `src/qml/Settings/VerificationSettingsWindow.qml`, `src/qml/Settings/RuntimeSettingsWindow.qml`, `src/qml/Settings/TaskModelsWindow.qml`, `src/qml/Settings/UITab.qml`

**Interfaces:**
- Consumes: `Main.qml::showWindow(loader)`.
- Produces: `Header` — сигнал `openSettingsRequested()` (вместо пяти сигналов).

- [ ] **Step 1: В `src/qml/MainWindow/Header.qml` заменить пять сигналов одним:**

```qml
    signal openSettingsRequested()
```

удалить `signal openUiSettingsRequested()`, `signal openOutputSettingsRequested()`, `signal openVerificationSettingsRequested()`, `signal openRuntimeSettingsRequested()`, `signal openTaskModelsSettingsRequested()`.

- [ ] **Step 2: Заменить кнопку «Настройки» и удалить меню.** В `Header.qml` кнопка:

```qml
                IconToolButton {
                    id: settingsButton
                    text: qsTr("Settings")
                    iconName: "settings"
                    onClicked: headerRoot.openSettingsRequested()
                }
```

удалить блок `Menu { id: settingsMenu ... }` целиком (вместе с пунктом «Setup wizard»).

- [ ] **Step 3: В `src/qml/Main.qml` заменить обработчики:**

```qml
        onOpenSettingsRequested: showWindow(settingsLoader)
```

удалить `onOpenUiSettingsRequested`, `onOpenOutputSettingsRequested`, `onOpenVerificationSettingsRequested`, `onOpenRuntimeSettingsRequested`, `onOpenTaskModelsSettingsRequested`, а также `onSetupWizardRequested` — мастер открывается из подвала окна настроек (T6), в меню тулбара его больше нет. В `src/qml/MainWindow/Header.qml` удалить `signal setupWizardRequested()`.

- [ ] **Step 4: Удалить старые окна и их загрузчики.** В `src/qml/Main.qml` удалить блоки `Loader { id: uiSettingsLoader ... }`, `outputSettingsLoader`, `verificationSettingsLoader`, `runtimeSettingsLoader`, `taskModelsSettingsLoader`; удалить файлы `src/qml/Settings/UiSettingsWindow.qml`, `OutputSettingsWindow.qml`, `VerificationSettingsWindow.qml`, `RuntimeSettingsWindow.qml`, `TaskModelsWindow.qml`, `UITab.qml`.

- [ ] **Step 5: Найти и починить остатки ссылок** — `grep -rn "UiSettingsWindow\|OutputSettingsWindow\|VerificationSettingsWindow\|RuntimeSettingsWindow\|TaskModelsWindow\|UITab" src/ tests/` — пусто (кроме `docs/`).

- [ ] **Step 6: Прогнать** — `cmake --build build -j 8 && scripts/check.sh --configure` (файлы удалены — нужен re-glob) — зелёный.

---

### Task 7: Тест окна настроек

**Files:**
- Create: `tests/test_settings_window_qml.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `SettingsWindow.qml`, `SettingsSectionNav.qml`, синглтоны `Settings`, `UiController`, `I18n`, `Verification`, `RuntimeInstaller`, `Runtime`, `ModelInstaller`, `Theme`.
- Produces: регрессия на состав разделов, переключение раздела и откат живых разделов при «Отмене».

- [ ] **Step 1: Написать `tests/test_settings_window_qml.cpp`** — по образцу `tests/test_task_models_table_qml.cpp` (регистрация синглтонов: `Settings`, `ModelInstaller`, `Runtime`, `RuntimeInstaller`, `UiController`, `I18n`, `Verification`; файловые синглтоны `Theme`, `BlockNames`, `PageStatus`, `RoleProfiles`, `TaskStatus`; `llocr_add_ui_icons`).

Проверки:

```cpp
private slots:
    void initTestCase();
    void theWindowHasFiveSections();
    void pickingASectionShowsIt();
    void theCancelRestoresTheInterface();
```

Тело:

```cpp
void TestSettingsWindowQml::theWindowHasFiveSections()
{
    std::unique_ptr<QObject> window = loadWindow();
    QVERIFY(window);
    const QVariantList sections = window->property("sections").toList();
    QCOMPARE(sections.size(), 5);
}

void TestSettingsWindowQml::pickingASectionShowsIt()
{
    std::unique_ptr<QObject> window = loadWindow();
    QVERIFY(window);
    QVERIFY(QMetaObject::invokeMethod(window.get(), "showSection", Q_ARG(QVariant, QStringLiteral("runtime"))));
    QCOMPARE(window->property("dirty").toBool(), false);
}

void TestSettingsWindowQml::theCancelRestoresTheInterface()
{
    std::unique_ptr<QObject> window = loadWindow();
    QVERIFY(window);

    // The theme is applied at once and the section keeps a snapshot.
    m_uiController->setMode(UiController::Dark);
    QVERIFY(QMetaObject::invokeMethod(window.get(), "cancelAll"));
    QCOMPARE(int(m_uiController->mode()), int(UiController::System));
}
```

Значения `UiController::System`/`Dark` и имя сеттера (`setMode`) — из `src/app/UiController.h` (`enum Mode { System = 0, Light = 1, Dark = 2 }`).

- [ ] **Step 2: Зарегистрировать таргет** в `tests/CMakeLists.txt`:

```cmake
qt_add_executable(test_settings_window_qml test_settings_window_qml.cpp)

target_link_libraries(test_settings_window_qml PRIVATE
    Qt6::Test
    Qt6::Quick
    Qt6::QuickControls2
    llocr_app
)
llocr_add_ui_icons(test_settings_window_qml)
add_test(NAME test_settings_window_qml COMMAND test_settings_window_qml)
```

- [ ] **Step 3: Прогнать** — `cmake --build build -j 8 --target test_settings_window_qml && ctest --test-dir build -R test_settings_window_qml` — PASS.

- [ ] **Step 4: Полный прогон** — `scripts/check.sh` — зелёный.

---

### Task 8: Документация и переводы

**Files:**
- Modify: `docs/07-glossary.md` (ADR 297)
- Modify: `docs/04-components.md` (каталог компонентов)
- Modify: `docs/19-testing.md` (карта тестов)
- Modify: `resources/i18n/llocr_ru.ts` (новые строки)

**Interfaces:**
- Consumes: итоговая реализация T1–T7.
- Produces: ADR 297 и переводы новых строк.

- [ ] **Step 1: Добавить ADR 297** в `docs/07-glossary.md` (в конец лога решений):

```markdown
- **297.** Единое окно настроек. Пять окон (Интерфейс, Вывод, Проверка, Runtime,
  Модели) сведены в одно: список разделов слева, содержимое справа, одна
  «Сохранить»/«Отмена» на всё окно, мастер настройки и сброс — в подвале.
  Интерфейс и Проверка применяют выбор сразу и откатываются снимком при
  «Отмене»; остальные разделы — черновик до «Сохранить». Сброс не трогает
  установленный runtime, скачанные модели и ключи в системном хранилище.
```

- [ ] **Step 2: Обновить `docs/04-components.md`** — в каталоге компонентов заменить пять окон на `SettingsWindow` / `SettingsSectionNav` / `ModelsSection` и перечислить страницы разделов.

- [ ] **Step 3: Обновить `docs/19-testing.md`** — добавить `test_settings_nav_qml` и `test_settings_window_qml` в карту тестов.

- [ ] **Step 4: Обновить переводы.** Обновить `resources/i18n/llocr_ru.ts` новыми строками: «Settings», «Setup wizard», «Reset all settings», «Reset all settings?», «Every setting goes back to its default. The installed runtime, the downloaded models and the keys in the system store stay.», «Interface», «Output», «Verification», «Runtime», «Models» (последние пять уже есть в каталоге — переиспользовать существующие переводы).

- [ ] **Step 5: Полный прогон** — `scripts/check.sh` — зелёный.

---

## Ручная проверка (после T7)

1. «Настройки» в тулбаре открывает одно окно; слева пять разделов с иконками.
2. Переключение раздела меняет заголовок и содержимое справа.
3. Смена темы видна сразу; «Отмена» возвращает прежнюю тему.
4. Переключение типа блока в разделе Проверка; «Отмена» возвращает его.
5. «Сохранить» пишет все разделы разом и закрывает окно.
6. «Сбросить все настройки» спрашивает подтверждение, после сброса значения — умолчания, а установленный runtime и скачанные модели на месте (раздел «Модели» показывает файлы).
7. «Мастер настройки» закрывает окно настроек и открывает мастер в главном окне;
   окно настроек само не возвращается — его снова открывают из тулбара, и тогда
   оно показывает значения мастера.
8. Закрытие окна с несохранёнными правками спрашивает подтверждение.
9. Кнопка «Настроить runtime…» в попапе задачи переключает список на раздел Runtime.
10. Ключ API, введённый до сброса, остаётся в системном хранилище (раздел Runtime показывает, что ключ задан).
