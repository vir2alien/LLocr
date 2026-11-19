# Per-task Runtime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let each of the four tasks (`layout`, `blockRecognition`, `decision`, `fix`) pick its own runtime — the managed llama.cpp server or an external OpenAI-compatible endpoint — instead of sharing one global switch.

**Architecture:** A per-task key `tasks/<role>/runtimeMode` (`managed` | `external`) lands in `QSettings`; the global `provider/mode` survives as the default for new tasks and as the migration source. `RuntimeController` reads the mode per role (`modeForRole`) instead of globally, so resolve, availability, config validity and the server lifecycle all become per-task. The managed tasks keep sharing one local llama-server (it already switches models per role via `serverRunsRole`/`beginRoleSwitch`); external tasks use their own endpoint/key/alias, which already exist per task. The QML gets a runtime column in the task table, the task popup gains a "Connection" tab, the Runtime section loses the global switch and shows both runtime cards side by side.

**Tech Stack:** Qt6 / C++ (QtCore, QtNetwork), QML (QtQuick Controls 2), CMake, QtTest.

**Spec:** `docs/superpowers/specs/2026-11-19-per-task-runtime-design.md`

## Global Constraints

- Layer targets (ADR 106): `llocr_core` → `llocr_config` → {`llocr_models`, `llocr_parsers`} → `llocr_runtime` → `llocr_app` → `llocr`. A layer may only use what it links. `ConnectionRole.h` lives in `llocr_core`; the include root is `src/`, so an include's spelling names its layer.
- Include root is `src/` — write `#include "config/SettingsStore.h"`, `#include "core/ConnectionRole.h"`, `#include "runtime/RuntimeController.h"`.
- QML: sizes and fonts only via `Theme.*` (`src/qml/Theme.qml`); never hardcode px or a font.
- Comments only in extreme cases (AGENTS.md rule 7): no restating the code, no section banners, no TODOs, no commented-out code.
- Secrets never go into `QSettings` (rule 8): API keys live in the OS secret store via `SecretStore`; write them through `SettingsStore::setApiKey` / `setApiKeyForRole`. A stored key never reaches the UI as text, a log, a command line or the project file.
- Layered architecture (rule 2): the UI must not touch the network or the filesystem directly.
- New models are added through Settings, not hardcoded (rule 3).
- Build system is CMake (rule 4).
- Every decision change goes into `docs/07-glossary.md` (rule 5) — new ADRs start at **318**.
- Docs are bilingual: every `docs/NN-name.md` has a `docs/NN-name.ru.md` mirror; update both.
- Tests constructing `SettingsStore` declare `TestSettingsIsolation m_settingsIsolation;` as the **first** member (`tests/testsettings.h`) — otherwise the test overwrites the user's real profile.
- Tests needing symlinks are `#ifdef Q_OS_UNIX`-guarded.
- Windows: MSVC 2022 x64, Qt `C:/Qt/6.10.3/msvc2022_64`, tree `build/win-msvc2022`, preset `dev-win`. Build: `cmd //c "\"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat\" && cmake --build --preset dev-win"`. Before ctest: `export PATH="/c/Qt/6.10.3/msvc2022_64/bin:$PATH"`, then `ctest --test-dir build/win-msvc2022 -R <test> --output-on-failure`.
- `LLOCR_MOCK_SERVER` is injected as `$<TARGET_FILE:mock_llama_server>` — never hardcode a path to the binary.
- Path comparisons normalize `/` vs `\` (`QDir::fromNativeSeparators` + `QDir::cleanPath`).
- Full gate before the final commit: build + full `ctest --test-dir build/win-msvc2022` + `scripts/check.sh --format-only` (clang-format, no Qt needed) + qmllint.
- Do not re-configure the build tree from scratch; reuse `build/win-msvc2022`.

## File Map

**Created:**
- `src/qml/Settings/TaskConnectionTab.qml` — the "Connection" tab of the task popup: endpoint, API key, model alias for one task. Replaces the per-task rows that leave the Runtime section.

**Modified:**
- `src/config/SettingsStore.h` / `.cpp` — the per-task runtime mode key, its API, the migration, the reset.
- `src/runtime/RuntimeController.h` / `.cpp` — `modeForRole(role)` and the seven per-task decision points; manual start picks the first managed role.
- `src/runtime/RuntimeLog.h` / `.cpp` — two log sources instead of one selected by the global mode.
- `src/runtime/SelfTestController.cpp` — the self-test is available when a managed task exists.
- `src/qml/Settings/RuntimeTab.qml` — the global switch is gone; both runtime cards are visible.
- `src/qml/Settings/RuntimeTabExternal.qml` — keeps the shared server card only; the per-task card moves out.
- `src/qml/Settings/RuntimeSection.qml` — the snapshot no longer carries `connectionMode`.
- `src/qml/Settings/ModelsSection.qml` — per-task modes instead of one global mode; the counter and the summary line.
- `src/qml/Common/TaskModelsTable.qml` — the runtime column, per-row columns, the "Connection" tab.
- `src/qml/Settings/LaunchTab.qml` — the mode comes from the task, not from the global setting.
- `src/qml/MainWindow/Footer.qml` — the mixed-mode badge; the Start/Stop button follows the managed tasks.
- `src/qml/ServerLogWindow.qml` — the split view: server output on top, request journal below.
- `src/qml/Setup/StepWelcome.qml` — the choice applies to all four tasks.
- `tests/test_settings_store.cpp` — the per-task mode, the migration, the reset.
- `tests/test_ensure_connection.cpp` — the per-task resolve; the existing global-mode tests switch to the per-task API.
- `tests/test_task_models_table_qml.cpp` — the runtime column and the Connection tab.
- `tests/test_runtime_tab_external_qml.cpp` — the shared server card stays; the task rows move.
- `tests/test_launch_tab_qml.cpp` — the launch tab follows the task's mode.
- `tests/test_footer_banner_qml.cpp` — the mixed badge.
- `tests/test_setup_wizard_qml.cpp` — the choice reaches all four tasks.
- `docs/07-glossary.md` + `.ru.md` — ADR 318–320.
- `docs/03-architecture.md` + `.ru.md`, `docs/09-tasks-and-verification.md` + `.ru.md`, `docs/10-external-runtime.md` + `.ru.md`, `docs/19-testing.md` + `.ru.md`.

## Review Focus

1. **A task moved from external to managed must not keep its old resolved connection.** `m_resolvedCache` is per-role; the `taskRuntimeModesChanged` signal must invalidate it, or the task keeps answering from the external endpoint. Pinned: `switchingTheModeDropsTheResolvedConnection` (Task 2).
2. **The migration must run once and be idempotent.** A second start must not rewrite the keys and must not reset a per-task choice the user made. Pinned: `theMigrationIsIdempotent` (Task 1).
3. **`roleConfigError` checks the server path first.** In per-task mode an external task would get the false error "Managed server is not configured". Pinned: `anExternalTaskAsksForNoServer` (Task 2).
4. **A task with no managed sibling must not start the local server.** The Start/Stop button is hidden and the manual start is refused when no managed task exists. Pinned: `withoutAManagedTaskThereIsNoServerToStart` (Task 2).
5. **The task table's columns must not jump between rows.** The quantization column and the alias column keep their places; only the content of the row changes with its mode. Pinned by the QML test in Task 4.
6. **The global mode must not hide the shared server URL any more.** `setConnectionMode` used to stash the URL in `provider/lastExternalBaseUrl` and restore it — that dance is removed, or the user's endpoint is lost when the default mode changes. Pinned: `theSharedUrlSurvivesTheDefaultMode` (Task 1).

---

### Task 1: SettingsStore — the per-task runtime mode, the migration, the reset

**Files:**
- Modify: `src/config/SettingsStore.h` (the public API, the key helper, the signal)
- Modify: `src/config/SettingsStore.cpp` (the implementation, the reset, `setConnectionMode`)
- Test: `tests/test_settings_store.cpp`

**Interfaces:**
- Consumes: `connectionRoles` / `connectionRoleName` / `connectionRoleIds` (`core/ConnectionRole.h`), `taskKeyName(role)` (existing private helper), `connectionMode()` / `setConnectionMode()` (existing).
- Produces:
  ```cpp
  // SettingsStore.h — public
  Q_INVOKABLE QString runtimeModeForRole(const QString &role) const;
  Q_INVOKABLE void setRuntimeModeForRole(const QString &role, const QString &mode);
  Q_INVOKABLE void setRuntimeModeForAllRoles(const QString &mode);
  Q_INVOKABLE QStringList managedRoles() const;
  Q_INVOKABLE QStringList externalRoles() const;
  bool isManagedRole(const QString &role) const;

  // SettingsStore.h — signals
  void taskRuntimeModesChanged();

  // SettingsStore.h — private
  static QString taskRuntimeModeKey(const QString &role);
  ```
  The key is `tasks/<taskKeyName(role)>/runtimeMode` — the same prefix family as `tasks/<role>/baseUrl`, so `blockRecognition` lands under `tasks/check/runtimeMode`.
  Values: `"managed"` and `"external"`. An unknown value reads as the global default.

- [ ] **Step 1: Write the failing tests** (in `tests/test_settings_store.cpp`, inside `private slots:`)

  ```cpp
  void theRuntimeModeIsReadPerTask()
  {
      SettingsStore store;
      store.setRuntimeModeForRole(QStringLiteral("decision"), QStringLiteral("managed"));
      store.setRuntimeModeForRole(QStringLiteral("fix"), QStringLiteral("external"));
      QCOMPARE(store.runtimeModeForRole(QStringLiteral("decision")), QStringLiteral("managed"));
      QCOMPARE(store.runtimeModeForRole(QStringLiteral("fix")), QStringLiteral("external"));
      // The two lists partition the four tasks by their effective mode.
      QCOMPARE(store.managedRoles(), QStringList({QStringLiteral("decision")}));
      QCOMPARE(store.externalRoles(),
               QStringList({QStringLiteral("layout"), QStringLiteral("blockRecognition"), QStringLiteral("fix")}));
  }

  void theGlobalModeBecomesThePerTaskMode()
  {
      SettingsStore store;
      store.setConnectionMode(QStringLiteral("managed"));
      QCOMPARE(store.runtimeModeForRole(QStringLiteral("layout")), QStringLiteral("managed"));
      QCOMPARE(store.runtimeModeForRole(QStringLiteral("blockRecognition")), QStringLiteral("managed"));
      QCOMPARE(store.managedRoles().size(), 4);
      QCOMPARE(store.externalRoles().size(), 0);
  }

  void theChoiceOfATaskSurvivesTheChangeOfTheGlobalMode()
  {
      SettingsStore store;
      store.setConnectionMode(QStringLiteral("managed"));
      store.setRuntimeModeForRole(QStringLiteral("fix"), QStringLiteral("external"));
      store.setConnectionMode(QStringLiteral("external"));
      // The task chose for itself: the global mode does not touch it.
      QCOMPARE(store.runtimeModeForRole(QStringLiteral("fix")), QStringLiteral("external"));
      // The task that follows the global mode follows it.
      QCOMPARE(store.runtimeModeForRole(QStringLiteral("layout")), QStringLiteral("external"));
  }

  void theOldInstallKeepsItsMode()
  {
      // The file of an old install: the global mode and no task of its own.
      QSettings raw;
      raw.setValue(QLatin1String(SettingsStore::kSettingsSchemaVersion), SettingsStore::kCurrentSettingsSchemaVersion);
      raw.setValue(QStringLiteral("provider/mode"), QStringLiteral("managed"));
      raw.sync();

      SettingsStore store;
      for (const QString &role : connectionRoleIds())
          QCOMPARE(store.runtimeModeForRole(role), QStringLiteral("managed"));
  }

  void theBlockRecognitionTaskKeepsTheCheckPrefix()
  {
      QSettings raw;
      raw.setValue(QLatin1String(SettingsStore::kSettingsSchemaVersion), SettingsStore::kCurrentSettingsSchemaVersion);
      raw.setValue(QStringLiteral("provider/mode"), QStringLiteral("managed"));
      raw.setValue(QStringLiteral("tasks/check/runtimeMode"), QStringLiteral("external"));
      raw.sync();

      SettingsStore store;
      // The key of the task's own group wins over the global default.
      QCOMPARE(store.runtimeModeForRole(QStringLiteral("blockRecognition")), QStringLiteral("external"));
      // The other tasks follow the global one.
      QCOMPARE(store.runtimeModeForRole(QStringLiteral("decision")), QStringLiteral("managed"));
  }

  void allTheTasksGetTheModeAtOnce()
  {
      SettingsStore store;
      store.setRuntimeModeForAllRoles(QStringLiteral("managed"));
      for (const QString &role : connectionRoleIds())
          QCOMPARE(store.runtimeModeForRole(role), QStringLiteral("managed"));
      QCOMPARE(store.managedRoles().size(), 4);
  }

  void theRuntimeModeResetGoesWithTheRuntime()
  {
      SettingsStore store;
      store.setRuntimeModeForRole(QStringLiteral("decision"), QStringLiteral("managed"));
      store.resetRuntimeDefaults();
      QCOMPARE(store.runtimeModeForRole(QStringLiteral("decision")), QStringLiteral("external"));
  }

  void theSharedUrlSurvivesTheDefaultMode()
  {
      SettingsStore store;
      store.setBaseUrl(QStringLiteral("http://my-server:8080"));
      store.setConnectionMode(QStringLiteral("managed"));
      QCOMPARE(store.baseUrl(), QStringLiteral("http://my-server:8080"));
      store.setConnectionMode(QStringLiteral("external"));
      QCOMPARE(store.baseUrl(), QStringLiteral("http://my-server:8080"));
      // The mode switch does not stash the URL any more.
      QVERIFY(!store.contains(QStringLiteral("provider/lastExternalBaseUrl")));
  }
  ```

  The raw `QSettings` writes in the two tests read the same file the store
  reads: `SettingsStore::makeSettings()` returns a plain `QSettings()` with
  the app's identity, and the test class sets organization `llocr_test` and
  application `test_settings` in `initTestCase`. The **key** is what matters
  there — `tasks/check/runtimeMode`, not a file path.

  Every test clears the settings file first (`cleanup()` does it), so the
  tests do not depend on their order.

- [ ] **Step 2: Run the test, verify FAIL**

  Run: `ctest --test-dir build/win-msvc2022 -R test_settings_store --output-on-failure` (build first: `cmd //c "\"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat\" && cmake --build --preset dev-win --target test_settings_store"`).
  Expected: FAIL — `runtimeModeForRole` does not exist (compile error).

- [ ] **Step 3: Implement in `SettingsStore.h`**

  Add next to the other per-task accessors (after `modelNameForRole` / `setModelNameForRole`):

  ```cpp
  Q_INVOKABLE QString runtimeModeForRole(const QString &role) const;
  Q_INVOKABLE void setRuntimeModeForRole(const QString &role, const QString &mode);
  Q_INVOKABLE void setRuntimeModeForAllRoles(const QString &mode);
  Q_INVOKABLE QStringList managedRoles() const;
  Q_INVOKABLE QStringList externalRoles() const;
  Q_INVOKABLE bool isManagedRole(const QString &role) const;
  ```

  Add the signal next to `connectionModeChanged()`:

  ```cpp
  void taskRuntimeModesChanged();
  ```

  Add the key helper next to `taskBaseUrlKey`:

  ```cpp
  static QString taskRuntimeModeKey(const QString &role);
  ```

  Add the mode constants next to the other private constants:

  ```cpp
  static constexpr const char *kModeManaged = "managed";
  static constexpr const char *kModeExternal = "external";
  ```

- [ ] **Step 4: Implement in `SettingsStore.cpp`**

  - `taskRuntimeModeKey`:
    ```cpp
    QString SettingsStore::taskRuntimeModeKey(const QString &role)
    {
        return QStringLiteral("tasks/") + taskKeyName(role) + QStringLiteral("/runtimeMode");
    }
    ```
  - `runtimeModeForRole` — the **effective** mode of a task: its own key when
    it has one, the global mode otherwise. The key is an explicit choice, so
    a task without a key follows the global:
    ```cpp
    QString SettingsStore::runtimeModeForRole(const QString &role) const
    {
        const QString stored = m_settings.value(taskRuntimeModeKey(role)).toString();
        if (stored == QLatin1String(kModeManaged) || stored == QLatin1String(kModeExternal))
            return stored;
        return connectionMode();
    }
    ```
    The reader writes nothing: the migration of an old install is exactly this
    fallback — the tasks of an old install have no key, so they follow the
    global mode the user chose for the app as a whole. The fallback is
    idempotent by construction and cannot overwrite a choice made later.
  - `managedRoles()` / `externalRoles()` — the tasks by their **effective**
    mode, so the two lists always partition the four tasks:
    ```cpp
    QStringList SettingsStore::managedRoles() const
    {
        QStringList out;
        for (const ConnectionRole role : connectionRoles) {
            const QString name = connectionRoleName(role);
            if (runtimeModeForRole(name) == QLatin1String(kModeManaged))
                out << name;
        }
        return out;
    }

    QStringList SettingsStore::externalRoles() const
    {
        QStringList out;
        for (const ConnectionRole role : connectionRoles) {
            const QString name = connectionRoleName(role);
            if (runtimeModeForRole(name) == QLatin1String(kModeExternal))
                out << name;
        }
        return out;
    }
    ```
    A task whose stored value is neither `managed` nor `external` (a
    hand-edited file) falls back to the global mode and therefore lands in one
    of the two lists — the partition never breaks.

  - `setRuntimeModeForRole`:
    ```cpp
    void SettingsStore::setRuntimeModeForRole(const QString &role, const QString &mode)
    {
        if (mode != QLatin1String(kModeManaged) && mode != QLatin1String(kModeExternal)) {
            qWarning("Ignoring invalid runtime mode %s", qPrintable(mode));
            return;
        }
        const QString key = taskRuntimeModeKey(role);
        if (m_settings.value(key).toString() == mode)
            return;
        m_settings.setValue(key, mode);
        emit taskRuntimeModesChanged();
    }
    ```
  - `setRuntimeModeForAllRoles`:
    ```cpp
    void SettingsStore::setRuntimeModeForAllRoles(const QString &mode)
    {
        if (mode != QLatin1String(kModeManaged) && mode != QLatin1String(kModeExternal)) {
            qWarning("Ignoring invalid runtime mode %s", qPrintable(mode));
            return;
        }
        bool changed = false;
        for (const ConnectionRole role : connectionRoles) {
            const QString name = connectionRoleName(role);
            const QString key = taskRuntimeModeKey(name);
            if (m_settings.value(key).toString() == mode)
                continue;
            m_settings.setValue(key, mode);
            changed = true;
        }
        if (!changed)
            return;
        m_settings.sync();
        emit taskRuntimeModesChanged();
    }
    ```
  - `isManagedRole`:
    ```cpp
    bool SettingsStore::isManagedRole(const QString &role) const
    {
        return runtimeModeForRole(role) == QLatin1String(kModeManaged);
    }
    ```
  - `setConnectionMode` — drop the URL stash/restore dance; the shared URL is always visible now. The global mode changes the tasks that follow it and **only** those: a task with its own key keeps its choice, because the key is an explicit choice:
    ```cpp
    void SettingsStore::setConnectionMode(const QString &mode)
    {
        if (mode != QLatin1String(kModeExternal) && mode != QLatin1String(kModeManaged)) {
            qWarning("Ignoring invalid connection mode %s", qPrintable(mode));
            return;
        }
        if (connectionMode() == mode)
            return;
        m_settings.setValue(kConnectionMode, mode);
        emit connectionModeChanged();
        // The tasks without a key of their own follow the global mode, and
        // their effective mode changed with it.
        bool followersChanged = false;
        for (const ConnectionRole role : connectionRoles) {
            const QString name = connectionRoleName(role);
            if (m_settings.value(taskRuntimeModeKey(name)).toString().isEmpty())
                followersChanged = true;
        }
        if (followersChanged)
            emit taskRuntimeModesChanged();
    }
    ```
    `provider/lastExternalBaseUrl` stays in the settings file (old installs have it) but is no longer written or read.
  - `resetRuntimeDefaults` — the per-task runtime modes go with the runtime:
    ```cpp
    void SettingsStore::resetRuntimeDefaults()
    {
        resetGroup([](const QString &key) {
            return key == QLatin1String("provider/mode") || key == QLatin1String("provider/baseUrl") || key == QLatin1String("provider/connectionTimeoutMs") ||
                   key == QLatin1String("provider/responseTimeoutMs") || key == QLatin1String("model/name") || key == QLatin1String("tasks/decision/modelName") ||
                   key == QLatin1String("tasks/layout/modelName") || key == QLatin1String("tasks/fix/modelName");
        });
        for (const ConnectionRole role : connectionRoles)
            m_settings.remove(taskRuntimeModeKey(connectionRoleName(role)));
        // The keys live in the system secret store, not in the settings: a reset
        // of the settings does not throw them away (ADR 297).
        m_settings.remove(QString::fromLatin1(kApiKey));
        clearTaskEndpoints();
        emit secretsChanged();
        emit taskRuntimeModesChanged();
    }
    ```
    `resetGroup` walks the **defaults table**, and the per-task runtime keys are not in it (they are per role), so they are removed explicitly — that is why the loop is there and not a table entry.

- [ ] **Step 5: Run the test, verify PASS**

  Run: `ctest --test-dir build/win-msvc2022 -R test_settings_store --output-on-failure`.
  Expected: PASS — all tests in the file, the seven new ones included.

- [ ] **Step 6: Commit**

  ```bash
  git add src/config/SettingsStore.h src/config/SettingsStore.cpp tests/test_settings_store.cpp
  git commit -m "feat(config): the runtime mode belongs to the task"
  ```

### Task 2: RuntimeController — the mode is read per role

**Files:**
- Modify: `src/runtime/RuntimeController.h` (the API: `modeForRole`, `hasManagedTasks`, `firstManagedRole`)
- Modify: `src/runtime/RuntimeController.cpp` (the seven decision points, the manual start, the cache invalidation)
- Modify: `src/runtime/SelfTestController.cpp` (the self-test's availability)
- Test: `tests/test_ensure_connection.cpp`

**Interfaces:**
- Consumes: `SettingsStore::runtimeModeForRole`, `SettingsStore::managedRoles`, `SettingsStore::externalRoles`, `SettingsStore::isManagedRole` (Task 1), `connectionRoles` / `connectionRoleName` / `connectionRoleFromName` (`core/ConnectionRole.h`).
- Produces:
  ```cpp
  // RuntimeController.h — public
  static ConnectionMode modeForRole(const SettingsStore &settings, ConnectionRole role);
  Q_INVOKABLE bool hasManagedTasks() const;
  Q_INVOKABLE QString firstManagedRole() const;   // "" when there is none

  // RuntimeController.h — private
  ConnectionMode modeForRole(ConnectionRole role) const;
  ```
  `modeFromSettings(settings)` stays as the **default** mode reader (the global value) — the wizard and the docs use it; the per-role reader is the new one.

- [ ] **Step 1: Write the failing tests** (in `tests/test_ensure_connection.cpp`)

  The file's existing tests set the global mode with `settings.setConnectionMode("managed")` / `("external")`. Those calls stay valid — the global mode is now the **default**, and a task with no explicit mode follows it. The new tests set the mode per task:

  ```cpp
  void theTaskModeDecidesThePath()
  {
      QTemporaryDir dir;
      SettingsStore settings;
      LaunchProfileStore launchProfiles(settings, writeTestLaunchCatalog(dir));
      settings.setRuntimeModeForRole(QStringLiteral("blockRecognition"), QStringLiteral("external"));
      settings.setRuntimeModeForRole(QStringLiteral("decision"), QStringLiteral("managed"));
      settings.setBaseUrl(QStringLiteral("http://shared:8080"));
      settings.setModelNameForRole(QStringLiteral("blockRecognition"), QStringLiteral("ocr-model"));
      settings.setDecisionModelName(QStringLiteral("decision-model"));

      RuntimeController runtime(settings, launchProfiles);
      QCOMPARE(runtime.taskModelAvailable(QStringLiteral("blockRecognition")), true);
      QCOMPARE(runtime.taskModelAvailable(QStringLiteral("decision")), false);
  }

  void anExternalTaskDoesNotNeedTheLocalServer()
  {
      SettingsStore settings;
      LaunchProfileStore launchProfiles(settings, QString());
      settings.setRuntimeModeForRole(QStringLiteral("blockRecognition"), QStringLiteral("external"));
      settings.setModelNameForRole(QStringLiteral("blockRecognition"), QStringLiteral("ocr-model"));
      // No server path at all: a managed task would be refused here.
      settings.setServerPath(QString());

      RuntimeController runtime(settings, launchProfiles);
      QCOMPARE(runtime.taskModelAvailable(QStringLiteral("blockRecognition")), true);
      QCOMPARE(runtime.configValid(), true);
  }
  ```

  `configValid` must be true: with no managed task, no server and no model file are required. That is the observable difference from the managed path, where `configValid` is false without a server. The error path of `roleConfigError` is private, so the test observes it through this public effect.

  ```cpp
  void withoutAManagedTaskThereIsNoServerToStart()
  {
      SettingsStore settings;
      LaunchProfileStore launchProfiles(settings, QString());
      for (const QString &role : connectionRoleIds())
          settings.setRuntimeModeForRole(role, QStringLiteral("external"));

      RuntimeController runtime(settings, launchProfiles);
      QCOMPARE(runtime.hasManagedTasks(), false);
      QCOMPARE(runtime.firstManagedRole(), QString());
      QVERIFY(runtime.launchCommandPreview().isEmpty());
  }

  void theFirstManagedRoleIsTheOneThatStartsTheServer()
  {
      SettingsStore settings;
      LaunchProfileStore launchProfiles(settings, QString());
      settings.setRuntimeModeForRole(QStringLiteral("layout"), QStringLiteral("managed"));
      settings.setRuntimeModeForRole(QStringLiteral("blockRecognition"), QStringLiteral("external"));
      settings.setRuntimeModeForRole(QStringLiteral("decision"), QStringLiteral("external"));
      settings.setRuntimeModeForRole(QStringLiteral("fix"), QStringLiteral("external"));

      RuntimeController runtime(settings, launchProfiles);
      QCOMPARE(runtime.hasManagedTasks(), true);
      QCOMPARE(runtime.firstManagedRole(), QStringLiteral("layout"));
  }

  void switchingTheModeDropsTheResolvedConnection()
  {
      QTemporaryDir dir;
      SettingsStore settings;
      LaunchProfileStore launchProfiles(settings, writeTestLaunchCatalog(dir));
      settings.setRuntimeModeForRole(QStringLiteral("blockRecognition"), QStringLiteral("external"));
      settings.setBaseUrl(QStringLiteral("http://shared:8080"));
      settings.setModelNameForRole(QStringLiteral("blockRecognition"), QStringLiteral("ocr-model"));

      RuntimeController runtime(settings, launchProfiles);
      QSignalSpy changedSpy(&settings, &SettingsStore::taskRuntimeModesChanged);
      settings.setRuntimeModeForRole(QStringLiteral("blockRecognition"), QStringLiteral("managed"));
      QVERIFY(!changedSpy.isEmpty());
      // The managed task now needs a model on disk.
      QCOMPARE(runtime.taskModelAvailable(QStringLiteral("blockRecognition")), false);
  }
  ```

- [ ] **Step 2: Run the tests, verify FAIL**

  Run: `ctest --test-dir build/win-msvc2022 -R test_ensure_connection --output-on-failure`.
  Expected: FAIL — `hasManagedTasks` / `firstManagedRole` do not exist, and `taskModelAvailable` still follows the global mode.

- [ ] **Step 3: Implement in `RuntimeController.h`**

  Replace the static reader with the pair:

  ```cpp
  // The default mode: the value a task with no explicit mode follows.
  static ConnectionMode modeFromSettings(const SettingsStore &settings);
  // The mode of one task: managed or external.
  static ConnectionMode modeForRole(const SettingsStore &settings, ConnectionRole role);
  ```

  Add the public helpers:

  ```cpp
  Q_INVOKABLE bool hasManagedTasks() const;
  Q_INVOKABLE QString firstManagedRole() const;
  ```

  Add the private member:

  ```cpp
  ConnectionMode modeForRole(ConnectionRole role) const;
  ```

- [ ] **Step 4: Implement in `RuntimeController.cpp`**

  - The readers:
    ```cpp
    ConnectionMode RuntimeController::modeFromSettings(const SettingsStore &settings)
    {
        return settings.mode();
    }

    ConnectionMode RuntimeController::modeForRole(const SettingsStore &settings, ConnectionRole role)
    {
        return settings.isManagedRole(connectionRoleName(role)) ? ConnectionMode::Managed : ConnectionMode::External;
    }

    ConnectionMode RuntimeController::modeForRole(ConnectionRole role) const
    {
        return modeForRole(m_settings, role);
    }
    ```
  - `ensureConnectionReady` — the branch is per role:
    ```cpp
    void RuntimeController::ensureConnectionReady(QObject *context, ConnectionRole role, const std::function<void(const ResolvedConnection &)> &onResolved)
    {
        if (modeForRole(role) == ConnectionMode::External) {
            resolveExternalReady(context, role, onResolved);
            return;
        }
        // ... the existing managed path, unchanged
    }
    ```
  - `taskModelAvailable`:
    ```cpp
    bool RuntimeController::taskModelAvailable(ConnectionRole role) const
    {
        if (modeForRole(role) == ConnectionMode::Managed)
            return QFileInfo::exists(m_settings.launchModelPathForRole(connectionRoleName(role)));
        return !m_settings.modelNameForRole(connectionRoleName(role)).trimmed().isEmpty();
    }
    ```
  - `roleConfigError` — the mode check comes **first**, before the server path:
    ```cpp
    QString RuntimeController::roleConfigError(ConnectionRole role, CheckFailure *failure) const
    {
        const auto note = [failure](CheckFailure kind) {
            if (failure)
                *failure = kind;
        };

        if (modeForRole(role) != ConnectionMode::Managed)
            return QString();

        const QString program = m_settings.serverPath().trimmed();
        if (program.isEmpty()) {
            note(CheckFailure::RuntimeMissing);
            return tr("Managed server is not configured");
        }
        if (!QFileInfo(program).isFile()) {
            note(CheckFailure::RuntimeMissing);
            return tr("File not found: %1").arg(program);
        }
        // ... the existing per-role model checks
    }
    ```
  - `canRecognize`:
    ```cpp
    bool RuntimeController::canRecognize(bool documentLoaded) const
    {
        if (!documentLoaded)
            return false;
        if (!hasManagedTasks())
            return true;
        if (m_state == RuntimeState::Ready)
            return true;
        return m_state == RuntimeState::Stopped && m_configValid && (m_settings.autoStart() || m_settings.startOnDemand());
    }
    ```
  - `recomputeConfigValid` — the model check walks the managed tasks:
    ```cpp
    void RuntimeController::recomputeConfigValid()
    {
        const QString program = m_settings.serverPath().trimmed();
        const bool serverOk = !program.isEmpty() && QFileInfo(program).isFile();
        const bool modelOk = managedModelPathValid();
        const bool valid = serverOk && (modelOk || !hasManagedTasks());
        // ... the rest of the existing body
    }
    ```
    with the helper:
    ```cpp
    bool RuntimeController::managedModelPathValid() const
    {
        for (const ConnectionRole role : connectionRoles) {
            if (modeForRole(role) != ConnectionMode::Managed)
                continue;
            const QString model = m_settings.launchModelPathForRole(connectionRoleName(role)).trimmed();
            if (model.isEmpty() || !QFileInfo(model).isFile())
                return false;
        }
        return true;
    }
    ```
    `m_modelPathValid` keeps its meaning (the models of the managed tasks are on disk). The old single check of `checkLaunchModelPath` becomes the managed walk.
  - `launchCommandPreview`:
    ```cpp
    QString RuntimeController::launchCommandPreview()
    {
        if (!hasManagedTasks())
            return QString();
        // ... the existing body
    }
    ```
  - `cancelPendingStart`:
    ```cpp
    void RuntimeController::cancelPendingStart()
    {
        if (!hasManagedTasks())
            return;
        // ... the existing body
    }
    ```
  - `startServer()` without a role — the first managed task:
    ```cpp
    QString RuntimeController::startServer()
    {
        const QString role = firstManagedRole();
        if (role.isEmpty())
            return tr("No task uses the managed runtime");
        return startServer(connectionRoleFromName(role));
    }
    ```
  - The public helpers:
    ```cpp
    bool RuntimeController::hasManagedTasks() const
    {
        return !m_settings.managedRoles().isEmpty();
    }

    QString RuntimeController::firstManagedRole() const
    {
        const QStringList roles = m_settings.managedRoles();
        return roles.isEmpty() ? QString() : roles.first();
    }
    ```
    `managedRoles()` returns the roles in the order of `connectionRoles` (`layout`, `blockRecognition`, `decision`, `fix`), so "first" is the canonical order, not the insertion order.
  - The cache invalidation — the new signal must drop the resolved connections:
    ```cpp
    connectEveryChangeSignal(&m_settings, this, "invalidateResolvedCache");
    connectEveryChangeSignal(&m_settings, this, "recomputeTaskModels");
    connect(&m_settings, &SettingsStore::taskRuntimeModesChanged, this, &RuntimeController::invalidateResolvedCache);
    connect(&m_settings, &SettingsStore::taskRuntimeModesChanged, this, &RuntimeController::recomputeTaskModels);
    ```
    `connectEveryChangeSignal` already connects every parameterless signal of `SettingsStore` to the given slot, so `taskRuntimeModesChanged` is picked up automatically by the two existing calls — the explicit `connect` lines are therefore **redundant**. Keep only the existing `connectEveryChangeSignal` calls and verify that `taskRuntimeModesChanged` is parameterless (it is), so the cache is invalidated. If the verification shows it is not connected, add the explicit `connect` lines.
  - `SelfTestController.cpp` — the self-test needs a managed runtime:
    ```cpp
    if (m_settings.managedRoles().isEmpty()) {
        SelfTestResult r;
        r.error = tr("Self-test is available only when a task uses the managed runtime");
        promise->reportResult(std::move(r));
        promise->reportFinished();
        return promise->future();
    }
    ```
    The same replacement applies to the QML path in `runSelfTestQml()`. The old check `RuntimeController::modeFromSettings(m_settings) == ConnectionMode::External` becomes `m_settings.managedRoles().isEmpty()`.

- [ ] **Step 5: Run the tests, verify PASS**

  Run: `ctest --test-dir build/win-msvc2022 -R test_ensure_connection --output-on-failure`.
  Expected: PASS — the new tests and every existing test in the file (the global mode still drives the tasks that have no explicit mode).

- [ ] **Step 6: Commit**

  ```bash
  git add src/runtime/RuntimeController.h src/runtime/RuntimeController.cpp src/runtime/SelfTestController.cpp tests/test_ensure_connection.cpp
  git commit -m "feat(runtime): the task decides its runtime"
  ```

### Task 3: RuntimeLog — both sources in one window

**Files:**
- Modify: `src/runtime/RuntimeLog.h` / `.cpp`
- Modify: `src/qml/ServerLogWindow.qml`
- Test: `tests/test_runtime_log.cpp` (new — see the CMake note below)

**Interfaces:**
- Consumes: `RequestJournal::instance().lines()` / `.count()` / `.clear()` (`core/RequestJournal.h`), the server's log lines (`m_lines`), `LogDocumentSync`.
- Produces:
  ```cpp
  // RuntimeLog.h — public
  Q_INVOKABLE QString serverLog() const;      // the server's output (unchanged meaning)
  Q_INVOKABLE QString journalLog() const;      // the request journal
  Q_INVOKABLE int journalLineCount() const;
  Q_INVOKABLE void attachJournalView(QQuickTextDocument *view);
  ```
  `serverLog()` stops switching on the global mode: it always returns the server's own output. `journalLog()` returns the request journal.

- [ ] **Step 1: Write the failing test**

  There is no `tests/test_runtime_log.cpp` today. Create it:

  ```cpp
  #include <QQuickTextDocument>
  #include <QtTest>

  #include "core/RequestJournal.h"
  #include "runtime/RuntimeLog.h"

  using namespace llocr;

  class TestRuntimeLog : public QObject
  {
      Q_OBJECT

  private slots:
      void initTestCase()
      {
          QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
          QCoreApplication::setApplicationName(QStringLiteral("test_runtime_log"));
      }

      void cleanup()
      {
          RequestJournal::instance().clear();
      }

      void theTwoSourcesAreSeparate()
      {
          SettingsStore settings;
          RuntimeLog log(settings);

          RequestJournal::Record record;
          record.at = QDateTime::currentDateTime();
          record.endpoint = QStringLiteral("http://example.test/v1/chat/completions");
          record.model = QStringLiteral("m");
          RequestJournal::instance().record(record);

          QVERIFY(log.journalLog().contains(QStringLiteral("example.test")));
          QVERIFY(log.journalLineCount() > 0);
          // The server has never run: its own output is empty.
          QVERIFY(log.serverLog().isEmpty());
      }
  };

  QTEST_MAIN(TestRuntimeLog)
  #include "test_runtime_log.moc"
  ```

  The test needs `llocr_runtime` and `llocr_config` (for `SettingsStore`) — and `RequestJournal` lives in `llocr_core`.

  **CMake note:** `tests/CMakeLists.txt` needs a new target. Follow the pattern of `test_runtime_tab_external_qml`:
  ```cmake
  qt_add_executable(test_runtime_log test_runtime_log.cpp)

  target_link_libraries(test_runtime_log PRIVATE
      Qt6::Test
      Qt6::Quick
      llocr_runtime
  )
  add_test(NAME test_runtime_log COMMAND test_runtime_log)
  ```
  Place it next to the other runtime tests. The directory-wide `TIMEOUT` (ADR 99) covers the new target — no per-test timeout needed.

- [ ] **Step 2: Run the test, verify FAIL**

  Run: `ctest --test-dir build/win-msvc2022 -R test_runtime_log --output-on-failure`.
  Expected: FAIL — `journalLog` does not exist.

- [ ] **Step 3: Implement in `RuntimeLog.h`**

  Replace `externalMode()` with the two accessors:

  ```cpp
  Q_INVOKABLE QString serverLog() const;
  Q_INVOKABLE QString journalLog() const;
  Q_INVOKABLE int journalLineCount() const;
  Q_INVOKABLE void attachJournalView(QQuickTextDocument *view);
  ```

  In the private section:

  ```cpp
  void flushJournalToView();
  LogDocumentSync m_journalSync;
  ```

  `attachLogView` stays for the server's output.

- [ ] **Step 4: Implement in `RuntimeLog.cpp`**

  - `serverLog()` returns the server's lines only:
    ```cpp
    QString RuntimeLog::serverLog() const
    {
        return m_lines.join(QLatin1Char('\n'));
    }
    ```
  - The new accessors:
    ```cpp
    QString RuntimeLog::journalLog() const
    {
        return RequestJournal::instance().lines().join(QLatin1Char('\n'));
    }

    int RuntimeLog::journalLineCount() const
    {
        return RequestJournal::instance().count();
    }
    ```
  - `attachJournalView` mirrors `attachLogView` — `LogDocumentSync::attach` takes a
    `QTextDocument *`, and a `QQuickTextDocument` hands it over:
    ```cpp
    void RuntimeLog::attachJournalView(QQuickTextDocument *view)
    {
        m_journalSync.attach(view ? view->textDocument() : nullptr);
        flushJournalToView();
    }
    ```
  - `flushToView` writes the server's lines; the journal has its own flush:
    ```cpp
    void RuntimeLog::flushToView()
    {
        m_sync.apply(m_lines, m_pendingDropped, QString());
        m_pendingDropped = 0;
        emit serverLogChanged();
    }

    void RuntimeLog::flushJournalToView()
    {
        m_journalSync.apply(RequestJournal::instance().lines(), 0, QString());
        emit serverLogChanged();
    }
    ```
    The constructor connects the journal's change signal to `flushJournalToView` (it connects to `flushToView` today).
  - `clearServerLog()` clears both:
    ```cpp
    void RuntimeLog::clearServerLog()
    {
        RequestJournal::instance().clear();
        if (m_server) {
            m_server->clearLog();
            m_lines.clear();
            flushToView();
            return;
        }
        m_lines.clear();
        flushToView();
    }
    ```
  - `copyServerLog()` copies both, separated by a blank line:
    ```cpp
    void RuntimeLog::copyServerLog()
    {
        const QString journal = journalLog();
        const QString server = serverLog();
        const QString text = server.isEmpty() ? journal
                                              : (journal.isEmpty() ? server : server + QLatin1String("\n\n") + journal);
        QGuiApplication::clipboard()->setText(text);
    }
    ```
  - `serverLogPath()` returns the server's log file path — it no longer returns an empty string for the external mode, because the server log exists independently of the mode. `serverLogDir()` likewise.

- [ ] **Step 5: Rewrite `ServerLogWindow.qml` as a split view**

  The window shows the server's output on top and the request journal below:

  ```qml
  ApplicationWindow {
      id: root

      title: qsTr("Runtime log")
      width: 720
      height: 560
      modality: Qt.NonModal

      function bringToFront() { ... unchanged ... }

      onVisibleChanged: {
          RuntimeLog.setLogViewVisible(visible)
          if (visible) {
              logScroll.autoScroll = true
              logScroll.scrollToBottom()
              journalScroll.autoScroll = true
              journalScroll.scrollToBottom()
          }
      }
      // ... the background and the footer stay
  }
  ```

  The body becomes two blocks. The server's block (a label + a `Flickable` + a `TextArea` bound to `RuntimeLog.attachLogView`) and the journal's block (a label + a `Flickable` + a `TextArea` bound to `RuntimeLog.attachJournalView`). Both use `Theme.surfaceSunken` for the well and `Theme.captionSize` for the monospace text — the same values the current single view uses.

  The footer's "Open directory" button becomes visible only when a server log file exists (`RuntimeLog.serverLogPath().length > 0`), because in a fully external mode there is no file to open.

  The empty-state label (`RuntimeLog.lineCount === 0`) becomes two: "No log output yet." for the server's block and "No requests yet." for the journal's block.

- [ ] **Step 6: Run the test, verify PASS**

  Run: `ctest --test-dir build/win-msvc2022 -R test_runtime_log --output-on-failure`.
  Expected: PASS.

- [ ] **Step 7: Commit**

  ```bash
  git add src/runtime/RuntimeLog.h src/runtime/RuntimeLog.cpp src/qml/ServerLogWindow.qml tests/test_runtime_log.cpp tests/CMakeLists.txt
  git commit -m "feat(log): the server output and the request journal in one window"
  ```

### Task 4: The task table — a runtime column and a Connection tab

**Files:**
- Create: `src/qml/Settings/TaskConnectionTab.qml`
- Modify: `src/qml/Common/TaskModelsTable.qml`
- Modify: `src/qml/Settings/ModelsSection.qml`
- Test: `tests/test_task_models_table_qml.cpp`

**Interfaces:**
- Consumes: `Settings.runtimeModeForRole(role)`, `Settings.setRuntimeModeForRole(role, mode)`, `Settings.managedRoles()`, `Settings.externalRoles()`, `Settings.taskRoles` (all from Task 1), `Settings.modelNameForRole`, `Settings.baseUrlForRole`, `Settings.setBaseUrlForRole`, `Settings.apiKeySetForRole`, `Settings.setApiKeyForRole` (existing).
- Produces:
  ```qml
  // Settings/TaskConnectionTab.qml
  Item {
      property string task: ""
      signal edited()
  }
  ```
  The table's draft entry gains a `runtimeMode` property; `TaskModelsTable` drops its `externalMode` property.

- [ ] **Step 1: Write the failing QML tests** (in `tests/test_task_models_table_qml.cpp`)

  ```cpp
  void theTableShowsARuntimeColumnPerTask()
  {
      const std::unique_ptr<QObject> table = loadTable();
      QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
      // Four rows, each with its own runtime switch.
      QCOMPARE(evaluate(table.get(), QStringLiteral("draft.length")).toInt(), 4);
      QVERIFY(evaluate(table.get(), QStringLiteral("draft[0].runtimeMode")).toString().size() > 0);
  }

  void theRuntimeColumnSwitchesTheTask()
  {
      const std::unique_ptr<QObject> table = loadTable();
      QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
      evaluate(table.get(), QStringLiteral("setRuntimeModeForTask('decision','managed')"));
      QCoreApplication::processEvents();
      QCOMPARE(Settings.runtimeModeForRole(QStringLiteral("decision")), QStringLiteral("managed"));
  }
  ```

  The helpers the file already has: `loadTable()` (the table in its default
  state), `loadTable(bool externalMode, QQuickItem *host)` (the table in a host
  of its own, for the width measurements), `evaluate(scope, expression)`,
  `draftEntry(table, task)`, `itemsByType(root, typeName)`.

  The table exposes two functions for the row's binding and for the test:
  ```qml
  function runtimeModeForTask(task) { return Settings.runtimeModeForRole(task) }
  function setRuntimeModeForTask(task, mode) { Settings.setRuntimeModeForRole(task, mode) }
  ```
  The test drives the real path through `evaluate` (the same helper the file
  already uses for QML expressions).

  The column presence per task:
  ```cpp
  void theTableShowsARuntimeColumnPerTask()
  {
      const std::unique_ptr<QObject> table = loadTable();
      QVERIFY2(table, qPrintable(m_warnings.join(QStringLiteral("; "))));
      QCOMPARE(evaluate(table.get(), QStringLiteral("draft.length")).toInt(), 4);
      for (const QString &role : connectionRoleIds()) {
          const QString mode = evaluate(table.get(), QStringLiteral("runtimeModeForTask('%1')").arg(role)).toString();
          QVERIFY2(!mode.isEmpty(), qPrintable(QStringLiteral("no runtime mode for %1").arg(role)));
      }
  }
  ```

  The test file needs `core/ConnectionRole.h` for `connectionRoleIds()` — include it and link `llocr_core` (it comes with `llocr_config`).

- [ ] **Step 2: Run the test, verify FAIL**

  Run: `ctest --test-dir build/win-msvc2022 -R test_task_models_table_qml --output-on-failure`.
  Expected: FAIL — `runtimeModeForTask` does not exist.

- [ ] **Step 3: The runtime column uses the existing `SegmentedControl`**

  The app already has a switch of this kind — `src/qml/Common/SegmentedControl.qml`
  draws a row of exclusive segments, is driven from the outside (`currentIndex`
  bound, `activated(index)` reported) and is keyboard-navigable. The runtime
  column is exactly that: two segments, «Управляемый» and «Внешний». No new
  component is needed.

  In the table's row:

  ```qml
  SegmentedControl {
      Layout.preferredWidth: table.runtimeColumnWidth
      options: [
          { label: qsTr("Управляемый") },
          { label: qsTr("Внешний") }
      ]
      currentIndex: taskRow.entry.runtimeMode === "external" ? 1 : 0
      onActivated: (index) => {
          const mode = index === 1 ? "external" : "managed"
          taskRow.entry.runtimeMode = mode
          taskRow.entry.runtimeModeEdited = true
          table.edited()
      }
  }
  ```

  `SegmentedControl` never writes `currentIndex` itself, so the binding to the
  draft entry survives the click and the Cancel path restores the saved mode.

  The column width follows the existing columns' pattern:

  ```qml
  readonly property int runtimeColumnWidth: 200
  ```

  and the header row gets the label:

  ```qml
  LLOLabel {
      Layout.preferredWidth: table.runtimeColumnWidth
      font.pixelSize: Theme.footnoteSize
      color: Theme.textMuted
      text: qsTr("Рантайм")
  }
  ```

- [ ] **Step 4: Create `src/qml/Settings/TaskConnectionTab.qml`**

  The connection of one task: the endpoint, the key and the model alias. It is the content of the popup's "Connection" tab.

  ```qml
  pragma ComponentBehavior: Bound

  import QtQuick
  import QtQuick.Controls
  import QtQuick.Layouts

  import LLocr
  import "../Common"

  // The connection of one task: its endpoint, its key and its model alias.
  // An empty endpoint or an empty key means the shared value.
  ColumnLayout {
      id: root

      property string task: ""

      signal edited()

      spacing: Theme.spacingSmall

      // The load fills the fields from the settings: that write is not the
      // user's, so it must not mark the settings window dirty.
      property bool loading: false
      property bool revealed: false
      property string aliasDraft: ""
      property string endpointDraft: ""
      property string keyDraft: ""
      property bool keyEdited: false
      property bool keyPresent: false

      function loadValues() {
          root.loading = true
          root.aliasDraft = Settings.modelNameForRole(root.task)
          root.endpointDraft = Settings.baseUrlForRole(root.task)
          root.keyPresent = Settings.apiKeySetForRole(root.task)
          root.keyDraft = ""
          root.keyEdited = false
          aliasField.text = root.aliasDraft
          root.loading = false
      }

      function saveValues() {
          Settings.setModelNameForRole(root.task, root.aliasDraft)
          Settings.setBaseUrlForRole(root.task, root.endpointDraft)
          if (root.keyEdited)
              Settings.setApiKeyForRole(root.task, root.keyDraft)
      }

      onTaskChanged: root.loadValues()

      LLOLabel {
          text: qsTr("Model alias")
          font.pixelSize: Theme.bodySmallSize
          color: Theme.textPrimary
      }

      TextField {
          id: aliasField

          Layout.fillWidth: true
          implicitHeight: Theme.controlHeight
          font.pixelSize: Theme.bodySmallSize
          selectByMouse: true
          placeholderText: qsTr("the model id")
          onTextEdited: {
              root.aliasDraft = aliasField.text
              root.edited()
          }
      }

      LLOLabel {
          Layout.fillWidth: true
          wrapMode: Text.WordWrap
          font.pixelSize: Theme.footnoteSize
          color: Theme.helpColor
          text: qsTr("An empty endpoint and an empty key take the shared server from Settings → Runtime.")
      }

      TaskEndpointForm {
          Layout.fillWidth: true
          taskTitle: qsTr("Endpoint and key")
          revealed: root.revealed
          endpointDraft: root.endpointDraft
          keyDraft: root.keyDraft
          keyEdited: root.keyEdited
          keyPresent: root.keyPresent
          onEdited: (endpoint, key, keyIsEdited) => {
              root.endpointDraft = endpoint
              root.keyDraft = key
              root.keyEdited = keyIsEdited
              root.edited()
          }
      }
  }
  ```

  `TaskEndpointForm` (`src/qml/Settings/TaskEndpointForm.qml`) already draws the endpoint field, the key field and the plaintext warning — reuse it instead of writing the fields again.

  The alias is written **on save** (`saveValues`), not on every keystroke: the popup's Cancel must throw the edit away like every other draft in the table.

- [ ] **Step 5: Modify `src/qml/Common/TaskModelsTable.qml`**

  - Drop the `externalMode` property (the mode is per row now).
  - Add to the draft entry:
    ```qml
    property string runtimeMode: "managed"
    ```
  - `syncDraft()` refreshes the mode when the user did not touch it:
    ```qml
    if (!entry.runtimeModeEdited)
        entry.runtimeMode = Settings.runtimeModeForRole(task)
    ```
    and the entry carries `property bool runtimeModeEdited: false`.
  - `resetDraft()` sets `entry.runtimeModeEdited = false` and refreshes the mode.
  - The columns: add a runtime column between the task and the model.
    ```qml
    readonly property int runtimeColumnWidth: 200
    ```
    The header row gets an `LLOLabel` with `text: qsTr("Рантайм")` at that width, and each row gets the `SegmentedControl` (see Step 3):
    ```qml
    SegmentedControl {
        Layout.preferredWidth: table.runtimeColumnWidth
        options: [
            { label: qsTr("Управляемый") },
            { label: qsTr("Внешний") }
        ]
        currentIndex: taskRow.entry.runtimeMode === "external" ? 1 : 0
        onActivated: (index) => {
            taskRow.entry.runtimeMode = index === 1 ? "external" : "managed"
            taskRow.entry.runtimeModeEdited = true
            table.edited()
        }
    }
    ```
  - The row's model and quantization columns follow the row's mode:
    - the quantization combo is visible when the row is managed:
      ```qml
      visible: taskRow.entry.runtimeMode !== "external"
      ```
    - the alias field is visible when the row is external and **read-only** (the alias is edited in the Connection tab):
      ```qml
      visible: taskRow.entry.runtimeMode === "external"
      readOnly: true
      ```
      The current field is already `readOnly: true` with an `onTextEdited` handler — the handler goes away, the field shows `Settings.modelNameForRole(task)`.
  - `applyExternalDraft()` is replaced by a per-mode apply. The save path writes the modes:
    ```qml
    function applyRuntimeModes() {
        for (const task of table.taskIds) {
            const entry = table.draftEntry(task)
            if (entry)
                Settings.setRuntimeModeForRole(task, entry.runtimeMode)
        }
        Settings.forceSave()
    }
    ```
  - The popup gains the third page. The popup keeps **two** tabs, and their
    meaning follows the task's mode:
    - managed → [Запуск, Запрос]
    - external → [Подключение, Запрос]

    The pages live in one `StackLayout` in the fixed order Launch (0), Request
    (1), Connection (2), and the popup maps the tab index onto the page:

    ```qml
    property string taskMode: "managed"

    function rebuildTabs() {
        taskSettingsPopup.taskMode = table.runtimeModeForTask(taskSettingsPopup.task)
    }
    ```

    ```qml
    SettingsTabBar {
        id: popupTabs
        Layout.fillWidth: true
        titles: taskSettingsPopup.taskMode === "external"
                 ? [qsTr("Подключение"), qsTr("Запрос")]
                 : [qsTr("Запуск"), qsTr("Запрос")]
    }
    ```

    ```qml
    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: taskSettingsPopup.taskMode === "external"
                      ? (popupTabs.currentIndex === 0 ? 2 : 1)
                      : popupTabs.currentIndex

        LaunchTab {
            id: launchTab
            role: taskSettingsPopup.task
            mode: taskSettingsPopup.taskMode
            hostTable: table
            runtimeSectionAvailable: table.runtimeSectionAvailable
            onEdited: table.edited()
        }

        RequestTab {
            id: requestTab
            role: taskSettingsPopup.task
            onEdited: table.edited()
        }

        TaskConnectionTab {
            id: connectionTab
            task: taskSettingsPopup.task
            onEdited: table.edited()
        }
    }
    ```

    The popup's height follows the same mapping: it measures the page the
    current tab points at.

    ```qml
    readonly property real currentContentHeight: {
        const index = taskSettingsPopup.taskMode === "external"
                      ? (popupTabs.currentIndex === 0 ? 2 : 1)
                      : popupTabs.currentIndex
        return index === 0 ? launchTab.contentHeight
             : index === 1 ? requestTab.contentHeight
             : connectionTab.contentHeight
    }
    ```

    The popup's `onAboutToShow` loads the pages:
    ```qml
    onAboutToShow: {
        rebuildTabs()
        launchTab.loadValues()
        requestTab.loadValues()
        connectionTab.loadValues()
    }
    ```
  - `saveTabs()` saves all three pages; the inapplicable ones write nothing meaningful (the Launch tab writes the launch profile draft — for an external task that draft is not used, but writing it is harmless and keeps the flow simple):
    ```qml
    function saveTabs() {
        const launchSaved = launchTab.saveValues()
        const requestSaved = requestTab.saveValues()
        connectionTab.saveValues()
        return launchSaved && requestSaved
    }
    ```
    The Connection tab's `saveValues` writes the endpoint, the key and the alias — it must **not** be skipped for an external task.

- [ ] **Step 6: Modify `src/qml/Settings/ModelsSection.qml`**

  - The `externalMode` property goes; the section asks the settings for the per-task modes:
    ```qml
    readonly property int managedCount: Settings.managedRoles().length
    readonly property int externalCount: Settings.externalRoles().length
    ```
  - The save path writes the modes before the model choices:
    ```qml
    function saveValues() {
        if (!taskTable.saveTabs())
            return false
        taskTable.applyRuntimeModes()
        const missing = ModelInstaller.applyTaskChoices(taskTable.buildChoices())
        taskTable.markDraftSaved()
        if (missing.length === 0)
            return true
        missingDialog.items = missing
        missingDialog.open()
        return false
    }
    ```
    `applyExternalDraft()` disappears: the per-task modes are written by `applyRuntimeModes()`, and the request profile pick (`RoleProfiles.setRequestProfileId`) is written for every task regardless of the mode — the profile is the request's shape, not the runtime's.
  - The counter above the table and the summary below it:
    ```qml
    LLOLabel {
        Layout.fillWidth: true
        font.pixelSize: Theme.footnoteSize
        color: Theme.helpColor
        text: qsTr("Локально: %1 · Внешний API: %2").arg(root.managedCount).arg(root.externalCount)
    }
    ```
    and under the table:
    ```qml
    LLOLabel {
        Layout.fillWidth: true
        visible: root.externalCount > 0
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.footnoteSize
        color: Theme.helpColor
        text: qsTr("Внешний API получат данные задачи: %1.").arg(root.externalTaskTitles())
    }
    ```
    with:
    ```qml
    function externalTaskTitles() {
        const titles = []
        for (const role of Settings.externalRoles())
            titles.push(RoleProfiles.title(role))
        return titles.join(", ")
    }
    ```
    and a button that opens the Runtime section:
    ```qml
    LLOButton {
        text: qsTr("Настроить рантаймы")
        flat: true
        onClicked: root.runtimeSectionRequested()
    }
    ```
    The signal already exists (`runtimeSectionRequested`); the window wires it to the Runtime section (ADR 297) — check `SettingsWindow.qml` for the existing connection and reuse it.

- [ ] **Step 7: Run the tests, verify PASS**

  Run: `ctest --test-dir build/win-msvc2022 -R test_task_models_table_qml --output-on-failure`.
  Expected: PASS — the new tests and the existing ones (an unsaved edit survives a sync, Cancel throws it away).

- [ ] **Step 8: Commit**

  ```bash
  git add src/qml/Settings/TaskConnectionTab.qml src/qml/Common/TaskModelsTable.qml src/qml/Settings/ModelsSection.qml tests/test_task_models_table_qml.cpp
  git commit -m "feat(ui): the task table picks a runtime per task"
  ```

### Task 5: The Runtime section — two cards, no switch

**Files:**
- Modify: `src/qml/Settings/RuntimeTab.qml`
- Modify: `src/qml/Settings/RuntimeTabExternal.qml`
- Modify: `src/qml/Settings/RuntimeSection.qml`
- Modify: `src/qml/Settings/LaunchTab.qml`
- Test: `tests/test_runtime_tab_external_qml.cpp`, `tests/test_launch_tab_qml.cpp`

**Interfaces:**
- Consumes: `Settings.runtimeModeForRole(role)` (Task 1), `RuntimeTabInternal` (existing), the shared server card of `RuntimeTabExternal` (existing).
- Produces:
  - `RuntimeTabExternal` keeps the shared server card and **loses** the per-task card (`compact` becomes the only switch: the wizard's compact mode shows the shared server alone).
  - `LaunchTab` gains `property string mode: "managed"` — the task's mode, set by the table's popup.

- [ ] **Step 1: Write the failing QML tests**

  In `tests/test_runtime_tab_external_qml.cpp` — the per-task rows are gone from
  the tab. The file's helpers are `loadTab()`, `evaluate(scope, expression)`,
  `rowAt(tab, index)`, `rowForTask(tab, task)` and `sharedKeyField(tab)`:
  ```cpp
  void theTabHasNoTaskRows()
  {
      const std::unique_ptr<QObject> tab = loadTab();
      QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
      QVERIFY2(rowForTask(tab.get(), QStringLiteral("decision")) == nullptr,
               "the external tab still carries the per-task rows");
  }
  ```
  The per-task tests move to the new home — the Connection tab of the task
  popup (Task 4). **Move them** to `tests/test_task_models_table_qml.cpp` and
  point them at `Settings/TaskConnectionTab.qml`:
  ```cpp
  void theTaskEndpointAndAliasAreSaved()
  {
      const std::unique_ptr<QObject> tab = loadConnectionTab(QStringLiteral("decision"));
      QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
      evaluate(tab.get(), QStringLiteral("aliasDraft = 'my-model'"));
      evaluate(tab.get(), QStringLiteral("endpointDraft = 'http://decision:9000'"));
      evaluate(tab.get(), QStringLiteral("saveValues()"));
      QCOMPARE(Settings.modelNameForRole(QStringLiteral("decision")), QStringLiteral("my-model"));
      QCOMPARE(Settings.baseUrlForRole(QStringLiteral("decision")), QStringLiteral("http://decision:9000"));
  }
  ```
  The new helper in that test file loads the Connection tab for a task:
  ```cpp
  std::unique_ptr<QObject> TestTaskModelsTableQml::loadConnectionTab(const QString &task)
  {
      m_warnings.clear();
      const QString path = QDir(m_qmlRoot).filePath(QStringLiteral("Settings/TaskConnectionTab.qml"));
      QQmlComponent component(m_engine.get(), QUrl::fromLocalFile(path));
      if (!component.isReady()) {
          m_warnings.append(component.errorString());
          return nullptr;
      }
      std::unique_ptr<QObject> object(component.create());
      if (object)
          object->setProperty("task", task);
      return object;
  }
  ```
  The plaintext warning test moves the same way.

  In `tests/test_launch_tab_qml.cpp` — the launch tab follows the task's mode.
  The file's helper is `loadTab()`:
  ```cpp
  void theLaunchTabFollowsTheTaskMode()
  {
      const std::unique_ptr<QObject> tab = loadTab();
      QVERIFY2(tab, qPrintable(m_warnings.join(QStringLiteral("; "))));
      tab->setProperty("role", QStringLiteral("decision"));
      tab->setProperty("mode", QStringLiteral("external"));
      QCoreApplication::processEvents();
      QCOMPARE(tab->property("externalMode").toBool(), true);
      tab->setProperty("mode", QStringLiteral("managed"));
      QCoreApplication::processEvents();
      QCOMPARE(tab->property("externalMode").toBool(), false);
  }
  ```

- [ ] **Step 2: Run the tests, verify FAIL**

  Run: `ctest --test-dir build/win-msvc2022 -R "test_runtime_tab_external_qml|test_launch_tab_qml" --output-on-failure`.
  Expected: FAIL — the Connection tab does not exist; the Launch tab has no `mode`.

- [ ] **Step 3: Modify `src/qml/Settings/RuntimeTab.qml`**

  - The global switch and its explanation go:
    ```qml
    // The mode row and the mode description are gone: the runtime is chosen
    // per task in Settings → Models.
    ```
    Remove the `SegmentedControl` block, the `externalMode` property and the help label that reads the global mode.
  - Both cards are visible:
    ```qml
    RuntimeTabInternal {
        id: rtInternal
        Layout.fillWidth: true
        onEdited: runtimeLayout.edited()
    }

    RuntimeTabExternal {
        id: rtExternal
        Layout.fillWidth: true
        onEdited: runtimeLayout.edited()
    }
    ```
    (the `visible:` bindings go away).
  - `loadValues()` / `saveValues()` keep calling both tabs — they already do.

- [ ] **Step 4: Modify `src/qml/Settings/RuntimeTabExternal.qml`**

  - The per-task card (the `tasksCard` rectangle with the `Repeater` of `TaskConnectionRow`) is removed.
  - The `taskIds` property goes.
  - The `compact` property goes with it (the tab is now only the shared server).
  - The card's header gains the note from the mockup:
    ```qml
    LLOLabel {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        font.pixelSize: Theme.footnoteSize
        color: Theme.helpColor
        text: qsTr("Этот адрес используют только внешние задачи. Другой сервер можно указать в параметрах задачи.")
    }
    ```

- [ ] **Step 5: Modify `src/qml/Settings/RuntimeSection.qml`**

  The snapshot no longer carries the global mode:
  ```qml
  function takeApplySnapshot() {
      root.applySnapshot = {
          backend: RuntimeInstaller.backend,
          selectedRelease: RuntimeInstaller.selectedRelease
      }
  }

  function restoreApplySnapshot() {
      if (!root.applySnapshot)
          return
      RuntimeInstaller.backend = root.applySnapshot.backend
      RuntimeInstaller.selectedRelease = root.applySnapshot.selectedRelease
  }
  ```

- [ ] **Step 6: Modify `src/qml/Settings/LaunchTab.qml`**

  - The mode comes from the task:
    ```qml
    // The runtime of the task this tab belongs to.
    property string mode: "managed"

    readonly property bool externalMode: root.mode === "external"
    ```
    The property `externalMode` stays (the tab's body uses it), but its source changes from the global setting to the task's mode.
  - The table's popup sets it:
    ```qml
    LaunchTab {
        id: launchTab
        role: taskSettingsPopup.task
        mode: taskSettingsPopup.taskMode
        hostTable: table
        runtimeSectionAvailable: table.runtimeSectionAvailable
        onEdited: table.edited()
    }
    ```

- [ ] **Step 7: Run the tests, verify PASS**

  Run: `ctest --test-dir build/win-msvc2022 -R "test_runtime_tab_external_qml|test_launch_tab_qml|test_task_models_table_qml" --output-on-failure`.
  Expected: PASS — all three.

- [ ] **Step 8: Commit**

  ```bash
  git add src/qml/Settings/RuntimeTab.qml src/qml/Settings/RuntimeTabExternal.qml src/qml/Settings/RuntimeSection.qml src/qml/Settings/LaunchTab.qml tests/test_runtime_tab_external_qml.cpp tests/test_launch_tab_qml.cpp tests/test_task_models_table_qml.cpp
  git commit -m "feat(ui): the runtime section shows both runtimes, the switch is gone"
  ```

### Task 6: The wizard and the footer

**Files:**
- Modify: `src/qml/Setup/StepWelcome.qml`
- Modify: `src/qml/MainWindow/Footer.qml`
- Test: `tests/test_setup_wizard_qml.cpp`, `tests/test_footer_banner_qml.cpp`

**Interfaces:**
- Consumes: `Settings.setRuntimeModeForAllRoles(mode)`, `Settings.connectionMode` (the default), `Settings.managedRoles()`, `Settings.externalRoles()`, `Runtime.state`, `Runtime.configValid` (Task 1).
- Produces:
  ```qml
  // StepWelcome.qml
  function applyChoice() {
      const mode = (root.choice === 1) ? "external" : "managed"
      Settings.setRuntimeModeForAllRoles(mode)
      Settings.connectionMode = mode
  }
  ```
  ```qml
  // Footer.qml
  readonly property bool anyManagedTask: Settings.managedRoles().length > 0
  readonly property bool anyExternalTask: Settings.externalRoles().length > 0
  ```

- [ ] **Step 1: Write the failing tests**

  In `tests/test_setup_wizard_qml.cpp`:
  ```cpp
  void theChoiceReachesEveryTask()
  {
      const std::unique_ptr<QObject> step = loadFile(QStringLiteral("Setup/StepWelcome.qml"));
      QVERIFY2(step, qPrintable(m_warnings.join(QStringLiteral("; "))));
      step->setProperty("choice", 1);
      QCoreApplication::processEvents();
      QCOMPARE(Settings.runtimeModeForRole(QStringLiteral("layout")), QStringLiteral("external"));
      QCOMPARE(Settings.runtimeModeForRole(QStringLiteral("blockRecognition")), QStringLiteral("external"));
      QCOMPARE(Settings.runtimeModeForRole(QStringLiteral("decision")), QStringLiteral("external"));
      QCOMPARE(Settings.runtimeModeForRole(QStringLiteral("fix")), QStringLiteral("external"));
  }
  ```
  The existing test `theExternalModeHasNoHardwareStep` keeps working — the wizard's step list still follows the choice.

  In `tests/test_footer_banner_qml.cpp` — the file's helper is `loadFooter()`:
  ```cpp
  void theBadgeSaysWhenTheRuntimesAreMixed()
  {
      const std::unique_ptr<QObject> footer = loadFooter();
      QVERIFY2(footer, qPrintable(m_warnings.join(QStringLiteral("; "))));
      Settings.setRuntimeModeForRole(QStringLiteral("decision"), QStringLiteral("managed"));
      Settings.setRuntimeModeForRole(QStringLiteral("fix"), QStringLiteral("external"));
      QCoreApplication::processEvents();
      QCOMPARE(footer->property("anyManagedTask").toBool(), true);
      QCOMPARE(footer->property("anyExternalTask").toBool(), true);
  }
  ```
  The badge's own text is a function of the runtime's state — the mixed case
  is pinned through the two properties the footer exposes.

- [ ] **Step 2: Run the tests, verify FAIL**

  Run: `ctest --test-dir build/win-msvc2022 -R "test_setup_wizard_qml|test_footer_banner_qml" --output-on-failure`.
  Expected: FAIL — the choice does not reach the tasks; the badge has no mixed state.

- [ ] **Step 3: Modify `src/qml/Setup/StepWelcome.qml`**

  ```qml
  function applyChoice() {
      const mode = (root.choice === 1) ? "external" : "managed"
      Settings.setRuntimeModeForAllRoles(mode)
      Settings.connectionMode = mode
  }
  ```

- [ ] **Step 4: Modify `src/qml/MainWindow/Footer.qml`**

  - The badge's text:
    ```qml
    readonly property bool anyManagedTask: Settings.managedRoles().length > 0
    readonly property bool anyExternalTask: Settings.externalRoles().length > 0

    function stateText() {
        if (root.anyManagedTask && root.anyExternalTask)
            return qsTr("Mixed")
        if (!root.anyManagedTask)
            return qsTr("External")
        const why = Runtime.statusMessage
        switch (Runtime.state) {
        case Runtime.NotConfigured: return qsTr("Runtime: not configured")
        case Runtime.Stopped:       return why.length ? why
                                        : qsTr("Runtime: stopped")
        case Runtime.Starting:      return qsTr("Runtime: starting…")
        case Runtime.Ready:         return qsTr("Runtime: ready")
        case Runtime.Stopping:      return qsTr("Runtime: stopping…")
        case Runtime.Failed:        return why.length ? why
                                        : qsTr("Runtime: failed")
        }
        return ""
    }
    ```
    (copy the existing `switch` verbatim — the cases and the texts are already in the file — and add the mixed branch above it.)
  - The Start/Stop button follows the managed tasks:
    ```qml
    visible: root.anyManagedTask
    ```
  - The badge's tooltip names the log window:
    ```qml
    text: qsTr("Runtime log — click to open. %1").arg(root.stateText())
    ```
    (the window shows both sources now, so it has no mode-dependent name).

- [ ] **Step 5: Run the tests, verify PASS**

  Run: `ctest --test-dir build/win-msvc2022 -R "test_setup_wizard_qml|test_footer_banner_qml" --output-on-failure`.
  Expected: PASS.

- [ ] **Step 6: Commit**

  ```bash
  git add src/qml/Setup/StepWelcome.qml src/qml/MainWindow/Footer.qml tests/test_setup_wizard_qml.cpp tests/test_footer_banner_qml.cpp
  git commit -m "feat(ui): the wizard picks the runtime for every task"
  ```

### Task 7: The documents and the ADRs

**Files:**
- Modify: `docs/07-glossary.md` and `docs/07-glossary.ru.md`
- Modify: `docs/03-architecture.md` and `docs/03-architecture.ru.md`
- Modify: `docs/09-tasks-and-verification.md` and `docs/09-tasks-and-verification.ru.md`
- Modify: `docs/10-external-runtime.md` and `docs/10-external-runtime.ru.md`
- Modify: `docs/19-testing.md` and `docs/19-testing.md`'s ru mirror

**Interfaces:**
- Consumes: nothing from the code — the documents describe what Tasks 1–6 built.
- Produces: ADR 318, 319, 320 in the adopted-decisions log.

- [ ] **Step 1: Write the ADRs** in `docs/07-glossary.md` (the log's last number is 317 — the new ones continue from 318), and their Russian mirrors:

  - **318.** The runtime belongs to the task, not to the app. The key `tasks/<role>/runtimeMode` (`managed`/`external`) is the mode of one task; the global `provider/mode` survives as the default for a task without an explicit mode and as the source of the migration that writes the four keys once. The switch is gone from the Runtime section: the task table carries it per row, and the per-task endpoint, key and alias moved into the task's own settings.
  - **319.** One managed server, several tasks. The managed tasks share the local llama-server, which switches its model when the task changes (`serverRunsRole`/`beginRoleSwitch`) — the mechanism is unchanged, but in a mixed setup it is exercised more often. With no managed task there is no server: the manual start is refused, the Start/Stop button is hidden, and the config needs neither a binary nor a model file.
  - **320.** The log window has two sources. The server's output and the request journal are separate things and both are shown: the server's output on top, the request journal below. The journal records every request the client made — to the local server and to the external endpoints alike; the old rule (the global mode picks one source) is gone, because in a mixed setup both are live.

- [ ] **Step 2: Update `docs/10-external-runtime.md`** — the external runtime is no longer a mode of the app but the runtime of a task: the shared server card in the Runtime section is the default for every external task, and the task's own endpoint, key and alias live in the task's settings (the "Подключение" tab). The per-task rows that the section used to carry are gone.

- [ ] **Step 3: Update `docs/03-architecture.md`** — the runtime layer reads the mode per task; the config layer owns the per-task key and the migration; the UI asks the settings for a task's mode.

- [ ] **Step 4: Update `docs/09-tasks-and-verification.md`** — a short note that each of the four tasks names its own runtime and what that means for the verification and the fix (an external decision task needs an alias; a managed one needs the model on disk).

- [ ] **Step 5: Update `docs/19-testing.md`** — the new test `test_runtime_log` in the map, and the per-task tests in the runtime and settings domains.

- [ ] **Step 6: Run the format gate**

  Run: `scripts/check.sh --format-only` (clang-format only, no Qt needed).
  Expected: no diff. The markdown files are not touched by clang-format.

- [ ] **Step 7: Commit**

  ```bash
  git add docs/07-glossary.md docs/07-glossary.ru.md docs/03-architecture.md docs/03-architecture.ru.md docs/09-tasks-and-verification.md docs/09-tasks-and-verification.ru.md docs/10-external-runtime.md docs/10-external-runtime.ru.md docs/19-testing.md docs/19-testing.ru.md
  git commit -m "docs: the runtime belongs to the task"
  ```

### Task 8: The whole tree green

**Files:**
- No source changes — this task runs the gates.

**Interfaces:**
- Consumes: everything the tasks above built.
- Produces: a green tree.

- [ ] **Step 1: Build everything**

  Run: `cmd //c "\"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat\" && cmake --build --preset dev-win"`.
  Expected: no errors.

- [ ] **Step 2: Run the whole test suite**

  Run: `export PATH="/c/Qt/6.10.3/msvc2022_64/bin:$PATH"; ctest --test-dir build/win-msvc2022 -j 4 --output-on-failure`.
  Expected: every test passes. The tests that read the global mode still pass — it is the default for a task without an explicit mode.

- [ ] **Step 3: Run the full gate**

  Run: `scripts/check.sh` (configure → build → ctest → clang-format → qmllint).
  Expected: everything green. The qmllint pass is the one that catches the new QML file: `TaskConnectionTab.qml` must satisfy `.qmllint.ini`.

- [ ] **Step 4: Commit any fixes the gate found**

  ```bash
  git add -A
  git commit -m "fix: the gate's findings"
  ```
  (Skip when the gate found nothing.)
