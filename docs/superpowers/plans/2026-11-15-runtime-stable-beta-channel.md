# Runtime Stable/Beta Channel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a stable/beta channel for the llama.cpp runtime (stable by default), resolving the stable release's `nightly-tag.txt` pointer to the beta release that actually carries the binaries.

**Architecture:** The channel is a fetch strategy in `ReleaseCatalog`: beta fetches the existing top-10 list and drops non-prerelease entries; stable composes one `ReleaseInfo` from `releases/latest` → `nightly-tag.txt` → `releases/tags/<b-tag>`, caching the composed release per channel. `RuntimeInstaller` owns the channel setting, per-channel cache dirs, and a visible beta-fallback when the stable release is unresolvable. Installs keep their b-build identity (`installedBuild()` stays `b12359`); `installedTag` records `v1.0.0` for display only.

**Tech Stack:** Qt6/C++ (QNetworkAccessManager, QJson), QML, QTest with a QTcpServer mock.

**Spec:** `docs/superpowers/specs/2026-11-15-runtime-stable-beta-channel-design.md`

## Global Constraints

- Layer targets (ADR 106): changes in `llocr_runtime` (ReleaseCatalog, RuntimeInstaller, InstalledBuildsModel), `llocr_config` (SettingsStore), QML module `LLocr`. Tests link the same targets.
- QML: sizes/fonts only via `Theme.*` (`src/qml/Theme.qml`); no hardcoded px.
- Comments only in extreme cases (AGENTS.md rule 7); no TODOs.
- Secrets untouched (rule 8).
- Build on Windows: `dev-win` preset, tree `build/win-msvc2022`, MSVC 2022 x64, Qt `C:/Qt/6.10.3/msvc2022_64`. Git Bash wrapper: `cmd //c "\"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat\" && cmake --build --preset dev-win"`. Before ctest: `export PATH="/c/Qt/6.10.3/msvc2022_64/bin:$PATH"`, then `ctest --test-dir build/win-msvc2022 -R <test> --output-on-failure`.
- `ReleaseInfo` gains a field → widely-included header: if a test exe crashes after an incremental build, wipe the affected obj dir or rebuild clean (known stale-tree hazard), not a code bug.
- Test files extended in place (`tests/test_release_catalog.cpp`, `tests/test_settings_store.cpp`, `tests/test_runtime_installer.cpp`) — no `tests/CMakeLists.txt` edits (TIMEOUT is directory-wide, ADR 99).
- Tests constructing `SettingsStore` declare `TestSettingsIsolation m_settingsIsolation;` as first member (`tests/testsettings.h`).
- Full gate before the final commit: build + `ctest --test-dir build/win-msvc2022` (all), clang-format via `scripts/check.sh --format-only` (no Qt needed), qmllint.

## Review Focus

1. **Right after a stable release ships, it tops the beta list** (fewer than 10 newer betas): beta fetch must drop non-prerelease entries or the wizard installs a release with no binaries and the update badge goes blind. Pinned: `betaFetchDropsStableReleases` (Task 1).
2. **Legacy cache files without the `prerelease` field** must keep working as betas (missing field reads as prerelease=true), or every existing cache empties on upgrade. Pinned: `prereleaseFlagDefaultsToBeta` (Task 1).
3. **A stable release whose pointer has no build for this platform** (e.g. pointer to a tag with no assets): user must see the existing per-platform error naming the release, not a silent no-op. Pinned: existing `pickAsset` error path asserted in `stableChannelInstallsFromCachedStableRelease` (Task 4 asserts the label; the error path is exercised by Task 2's compose test dropping no-os assets).
4. **Channel switch mid-install** must not reset the release list the running install uses: `setChannel` ignores the call while `busy()` (QML also disables the combo). Covered by a guard, not a test — do not remove the guard.
5. **Stale stable cache after upstream publishes v1.0.1** (6 h TTL): "Check for updates" must bypass the cache when stale — the existing TTL in `loadCache` applies per channel dir; `channelSwitchRefetches` (Task 4) pins that each channel reads its own dir.

---

### Task 1: ReleaseCatalog — prerelease flag, build recovery, beta filter, loopback test seam

**Files:**
- Modify: `src/runtime/ReleaseAsset.h` (ReleaseInfo)
- Modify: `src/runtime/ReleaseCatalog.h` / `.cpp`
- Test: `tests/test_release_catalog.cpp`

**Interfaces:**
- Consumes: existing `parseReleasesJson`, `fetchReleasesLocal(nam, cacheDir, error, timeoutMs, apiUrl)`, `StatusServer` test helper.
- Produces: `ReleaseInfo::prerelease` (bool, default true); `fetchReleasesLocal` returns only prerelease entries; internal `catalogGet` helper used by Task 2.

- [ ] **Step 1: Write the failing tests** (in `tests/test_release_catalog.cpp`)

  - `prereleaseFlagDefaultsToBeta`: `parseReleasesJson(buildReleases(), err)` — every returned release has `prerelease == true` (fixtures carry no `prerelease` field).
  - `buildNumberRecoveredFromAssets`: fixture release `tag_name: "v1.0.0"`, `prerelease: false`, assets `[llama-b12359-bin-win-cpu-x64.zip]` → parsed `build == 12359`, `prerelease == false`.
  - `betaFetchDropsStableReleases`: StatusServer serving HTTP 200 with body = JSON array `[stableRelease, betaRelease]` (stable: `tag_name v1.0.0`, `prerelease: false`, no binary assets; beta: `tag_name b9999`, `prerelease: true`, platform asset as in `writeCachedReleases`) → `fetchReleasesLocal(&nam, dir, error, 5000, server.url())` returns exactly 1 release, `tagName == "b9999"`; `error` empty.

- [ ] **Step 2: Run tests, verify FAIL**

  Run: `ctest --test-dir build/win-msvc2022 -R test_release_catalog --output-on-failure` (after building the test target).
  Expected: new tests FAIL (no `prerelease` member; stable not dropped).

- [ ] **Step 3: Implement**

  - `ReleaseInfo`: add `bool prerelease = true;` (missing field = legacy cache = beta).
  - `parseReleasesJson`: `r.prerelease = o.contains("prerelease") ? o.value("prerelease").toBool() : true;`; after the asset loop, when `r.build < 0`, set it from the first asset whose `extractBuildNumberFromTag(asset.build)` is positive.
  - `ReleaseCatalog.cpp` anonymous namespace: `HttpClient::Response catalogGet(QNetworkAccessManager *nam, const QUrl &url, const HttpClient::Options &options)` — passes `allowInsecure = (host is 127.0.0.1 | ::1 | localhost)` to `HttpClient::get`; replace the direct `HttpClient::get` call in `fetchReleasesLocal`. (Existing StatusServer tests only reach the failure path today because plain-http is refused; this seam makes the mocks real without weakening prod URLs, which are always https.)
  - `fetchReleasesLocal`: drop `!prerelease` entries from every list it returns — the `loadCache` result, the `readCacheFile` offline fallback, and the freshly parsed list — before the empty-result error check. The cache file still stores the raw GitHub array.

- [ ] **Step 4: Run tests, verify PASS** (same command; all existing tests in the file stay green — the served-failure tests keep their assertions)

- [ ] **Step 5: Commit** `feat(runtime): beta catalog drops stable releases; build number recovered from assets`

### Task 2: ReleaseCatalog — stable fetch with nightly-tag resolution and beta fallback

**Files:**
- Modify: `src/runtime/ReleaseCatalog.h` / `.cpp`
- Test: `tests/test_release_catalog.cpp`

**Interfaces:**
- Consumes: `catalogGet` (Task 1), `parseReleasesJson` with prerelease/build recovery (Task 1), `fetchReleasesLocal` (existing).
- Produces:
  ```cpp
  static constexpr const char *kStableApiBase = "https://api.github.com/repos/ggml-org/llama.cpp";
  struct ReleaseFetch {
      QList<ReleaseInfo> releases;
      QString error;  // hard failure; releases empty
      QString note;   // non-empty: releases is the beta fallback; explains why
  };
  static ReleaseFetch fetchStableReleaseLocal(QNetworkAccessManager *nam,
      const QString &stableCacheDir, const QString &betaCacheDir,
      int timeoutMs = kRequestTimeoutMs, const QString &apiBase = QString());
  static QString parseNightlyTag(const QString &content);   // trimmed "b<N>" or empty
  static ReleaseInfo composeStableRelease(const ReleaseInfo &latest, const QString &pointerTag, const ReleaseInfo &resolved);
  ```

- [ ] **Step 1: Write the failing tests**

  - `parseNightlyTagTrimsAndValidates`: `"b12359\n"` → `"b12359"`; `" b12400 "` → `"b12400"`; `""`, `"v1.0.0"`, `"b12ab"` → empty.
  - `composeStableReleaseMergesResolvedBuild`: latest `{tagName v1.0.0, publishedAt, one no-os asset (nightly-tag.txt)}`, pointer `"b12359"`, resolved `{tagName b12359, prerelease true, assets [win-cpu asset, a no-os "mystery" asset]}` → result: `tagName == "v1.0.0"`, `build == 12359`, `prerelease == false`, assets == only the win-cpu asset, that asset's `build == "b12359"`.
  - `stableFetchResolvesPointerAndCaches`: StatusServer routes — `/releases/latest` → 200 stable JSON (`nightly-tag.txt` asset whose `browser_download_url` points at the mock `/download/nightly-tag.txt`, `digest` present on binaries), `/download/nightly-tag.txt` → `b12359\n`, `/releases/tags/b12359` → 200 beta JSON with platform assets + `digest` fields. `fetchStableReleaseLocal(&nam, stableDir, betaDir, 5000, server.baseUrl())` → `releases.size() == 1`, `tagName v1.0.0`, `build 12359`, asset `sha256` from the API digest; `error`/`note` empty; `stableDir/releases.json` written with `sourceUrl == base + "/releases/latest"`; a second call with the server stopped returns the same list from cache.
  - `stableFetchFallsBackToBetaWithNote`: route `/releases/latest` → 500; `betaCacheDir` pre-written (fresh) with a cache whose `sourceUrl == base + "/releases?per_page=10"` and one b9999 release → result releases == the beta list, `note` non-empty, `error` empty.

- [ ] **Step 2: Run tests, verify FAIL**

- [ ] **Step 3: Implement** in `ReleaseCatalog.cpp`:

  - Extend `StatusServer` (test helper) with `void setRoute(const QString &pathSuffix, const QByteArray &response)` (matched against the request path before the canned status) and `QString baseUrl()` — keeps existing single-response tests untouched.
  - `parseNightlyTag`: trim; match `^b\d+$`; return the trimmed tag or empty.
  - `composeStableRelease`: tagName/name/publishedAt from `latest`; `build = extractBuildNumberFromTag(pointerTag)`; `prerelease = false`; `body` from `resolved`; assets = `resolved.assets` minus those with empty `os`.
  - `fetchStableReleaseLocal`: `latestUrl = apiBase.isEmpty() ? kStableApiBase : apiBase`, then `latestUrl + "/releases/latest"`; `loadCache(stableCacheDir, latestUrl)` fresh → return it (no note); stale/absent → `readCacheFile` offline fallback on failure (existing pattern); `catalogGet` the latest URL → parse as one-element array; find the `nightly-tag.txt` asset, `catalogGet` its `browser_download_url`, `parseNightlyTag` (empty → hard error "the stable release does not point at a build"); `catalogGet` `latestUrl` with `/releases/latest` replaced by `/releases/tags/<tag>` → parse; compose; cache write `{"sourceUrl": latestUrl, "releases": [latest-object with "assets" replaced by the resolved release's assets array and "prerelease" set to false]}` (re-parse of that object yields build via the asset-name recovery — Task 1). Any hard failure with an empty stable cache → call `fetchReleasesLocal(&nam, betaCacheDir, error, timeoutMs, apiBase.isEmpty() ? QString() : apiBase + "/releases?per_page=10")`; on a non-empty beta list return it with `note` set (message text assembled in Task 4, keep the reason in the note); beta failure → propagate as `error`.

- [ ] **Step 4: Run tests, verify PASS**

- [ ] **Step 5: Commit** `feat(runtime): stable release resolves its nightly build pointer`

### Task 3: SettingsStore — runtime channel and installed tag

**Files:**
- Modify: `src/config/SettingsStore.h` / `.cpp`
- Test: `tests/test_settings_store.cpp`

**Interfaces:**
- Produces: `Q_PROPERTY(QString runtimeChannel READ runtimeChannel WRITE setRuntimeChannel NOTIFY runtimeChannelChanged)`, `Q_PROPERTY(QString installedTag READ installedTag WRITE setInstalledTag NOTIFY installedTagChanged)`; keys `kRuntimeChannel = "runtime/channel"` (default `"stable"`), `kInstalledTag = "runtime/installedTag"` (default empty); both keys added to `resetToDefaults()` `preserved`.

- [ ] **Step 1: Write the failing tests** (patterns follow the existing `runtimeBackend`/`installedBuild` tests in the file)

  - `runtimeChannelDefaultsToStable`: fresh store → `"stable"`.
  - `runtimeChannelAndInstalledTagRoundtrip`: setters flip values and emit; re-constructed store reads them back.
  - `resetPreservesChannelAndTag`: set channel `"beta"`, tag `"v1.0.0"` → `resetToDefaults()` → both survive (mirror the existing preserved-keys test).

- [ ] **Step 2: Run, verify FAIL** — `ctest --test-dir build/win-msvc2022 -R test_settings_store --output-on-failure`

- [ ] **Step 3: Implement** — key constants next to `kRuntimeBackend`/`kInstalledBuild` (~line 497), defaults-table entries next to lines 119–120, property/getter/setter/signal mirroring `runtimeBackend` (lines 60, 232, 361, 857–867: setter skips and does not emit when the value is equal), `preserved` list at lines 239–243 gains `kRuntimeChannel` and `kInstalledTag`.

- [ ] **Step 4: Run, verify PASS**

- [ ] **Step 5: Commit** `feat(config): runtime channel and installed tag settings`

### Task 4: RuntimeInstaller — channel switch, orchestration, update logic, installed tag

**Files:**
- Modify: `src/runtime/RuntimeInstaller.h` / `.cpp`
- Test: `tests/test_runtime_installer.cpp`

**Interfaces:**
- Consumes: `ReleaseCatalog::fetchStableReleaseLocal`/`kStableApiBase` (Task 2), `SettingsStore::runtimeChannel`/`installedTag` (Task 3), `fetchReleasesLocal` (existing).
- Produces: `Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY channelChanged)` (values `"stable"`/`"beta"`, normalized; default from settings); ctor gains trailing optional param `const QString &catalogApiBase = QString()` (test seam; empty = real GitHub); `onCatalogLoaded` handles the fallback note; `releaseLabel`/status carry the resolved build; `activateBuild` clears `installedTag`; install sets/clears it.

- [ ] **Step 1: Write the failing tests** (a local single-response/routed StatusServer clone of the Task 2 helper lives in this file for the fallback test)

  - Update `theInstallFetchesTheReleaseListItNeeds`: `writeCachedReleases` writes into `<root>/app/cache/releases-beta/releases.json` and the test calls `settings.setRuntimeChannel("beta")` — same assertions otherwise.
  - `stableChannelInstallsFromCachedStableRelease`: write a fresh stable cache (`<root>/app/cache/releases-stable/releases.json`, `sourceUrl` = real `kStableApiBase + "/releases/latest"`, one composed `v1.0.0` object: `prerelease: false`, platform asset `llama-b12359-bin-…` with a 64-hex `digest`) → installer (default ctor) → `startDownloadAndInstall()` → reaches `Ready` with `releaseCount == 1`, `releaseLabel(0)` contains `"v1.0.0"` and `"(b12359)"`, then reaches `Downloading`.
  - `channelSwitchRefetches`: fresh caches in both channel dirs → `checkForUpdates()` → label contains `"v1.0.0"`; `installer.setProperty("channel", "beta")` → after the catalog reloads, label contains `"b9999"` and `settings.runtimeChannel() == "beta"`; back to `"stable"` → `"v1.0.0"` again.
  - `stableChannelFallsBackToCachedBeta`: fresh beta cache whose `sourceUrl` is the mock `base + "/releases?per_page=10"` (the fallback fetch binds its cache to that URL — mismatch would leave the fallback empty); nothing in the stable dir; installer constructed with `catalogApiBase = server.baseUrl()`; mock route `/releases/latest` → 500 → `startDownloadAndInstall()` → reaches `Ready`, status contains `"beta"`, `releaseCount == 1` with the cached b9999, then `Downloading`.
  - `hasUpdateAcrossChannels`: with the stable cache installed build `b12400` → after check, `hasUpdate == false`; `installedBuild` `b100` → `true`; on the beta channel (b9999 cache) `b12400` → `false`, `b100` → `true`.

- [ ] **Step 2: Run, verify FAIL** — `ctest --test-dir build/win-msvc2022 -R test_runtime_installer --output-on-failure`

- [ ] **Step 3: Implement**

  - Header: `Q_PROPERTY(QString channel …)`, getter, private `setChannel`, `signal channelChanged()`, member `QString m_channel` (ctor: settings value, normalized — anything but `"beta"` reads `"stable"`), member `const QString m_catalogApiBase` (trailing optional ctor param).
  - `setChannel`: normalize; no-op when equal or `busy()`; save via `setRuntimeChannel`; emit; `m_state == State::Idle` → return; else clear `m_releases`, `setSelectedRelease(0)`, `emit catalogChanged()`, `startCatalogFetch()`.
  - `startCatalogFetch`: `base = m_catalogApiBase.isEmpty() ? kStableApiBase : m_catalogApiBase`; per-channel dirs `QDir(cacheDir).filePath("releases-stable"/"releases-beta")`; worker returns `struct CatalogLoad { QList<ReleaseInfo> releases; QString error; QString note; }` — beta channel: `fetchReleasesLocal(&nam, betaDir, error, kRequestTimeoutMs, base + "/releases?per_page=10")`; stable: `fetchStableReleaseLocal(&nam, stableDir, betaDir, kRequestTimeoutMs, base)` mapping `ReleaseFetch` → `CatalogLoad` (note assembled with `tr("Stable release unavailable (%1) — showing beta builds")`, reason inside).
  - `onCatalogLoaded(CatalogLoad)`: non-empty `note` → `setStatusMessage(note)`, treat the list as fresh (`m_lastCatalogAt` updated); otherwise existing branches, and the "Latest release" message names the resolved build — shared anonymous-ns helper `taggedName(const ReleaseInfo &)` returning `tagName` plus `" (b<N>)"` when `build > 0 && tagName != "b<N>"`; `releaseLabel` uses the same helper; `updateBuild()` stays `tagName`.
  - `onInstallFinished`: after `setInstalledBuild(out.build)` → `const ReleaseInfo &rel = m_releases.at(m_selectedRelease); m_settings.setInstalledTag(rel.prerelease ? QString() : rel.tagName);`
  - `activateBuild`: `m_settings.setInstalledTag(QString());` next to `setInstalledBuild`.

- [ ] **Step 4: Run, verify PASS**

- [ ] **Step 5: Commit** `feat(runtime): stable/beta channel switch in the runtime installer`

### Task 5: UI — channel combo, resolved-version display

**Files:**
- Modify: `src/runtime/InstalledBuildsModel.h` / `.cpp`
- Modify: `src/qml/Settings/RuntimeTabInternal.qml`
- Modify: `src/qml/Common/RuntimeBuildsList.qml`
- Test: `tests/test_runtime_installer.cpp` (model role), qmllint

**Interfaces:**
- Consumes: `RuntimeInstaller::channel` (Task 4), `SettingsStore::installedTag` (Task 3).
- Produces: `InstalledBuildsModel` role `DisplayBuildRole` → role name `"displayBuild"`; QML combo writes `RuntimeInstaller.channel`.

- [ ] **Step 1: Write the failing test** (`test_runtime_installer.cpp`)

  `displayBuildRoleShowsResolvedTag`: fresh `SettingsStore` (isolation member); `setServerPath` to a known path; model `setBuilds({InstalledBuildInfo{tag "llama.cpp-b12359-cpu", build "b12359", backend "cpu", serverPath <same path>}})`; `data(index, DisplayBuildRole)` → `"b12359"`; `settings.setInstalledTag("v1.0.0")` + `settingsChanged()` → `"v1.0.0 (b12359)"`.

- [ ] **Step 2: Run, verify FAIL**

- [ ] **Step 3: Implement**

  - `InstalledBuildsModel`: `DisplayBuildRole` in the enum, role name, `data()` case — active row with non-empty `installedTag()` and non-empty `build` → `"%1 (%2)"`, else `build`; `settingsChanged` re-emits `{ActiveRole, DisplayBuildRole}`.
  - `RuntimeTabInternal.qml`: in the install `RowLayout`, before the Backend label/combo — `LLOLabel { text: qsTr("Channel"); font: Theme.bodySmall }` and an `LLOComboBox` with `model: [qsTr("Stable"), qsTr("Beta")]`, `enabled: !RuntimeInstaller.busy`, `onActivated: { RuntimeInstaller.channel = currentIndex === 1 ? "beta" : "stable"; root.edited() }`; `syncChannelBox()` (index from `RuntimeInstaller.channel`) called in `buildInstallOptions` and from a new `onChannelChanged` handler in the `Connections` block.
  - `RuntimeBuildsList.qml`: delegate gains `required property string displayBuild`; the primary label shows `displayBuild.length ? displayBuild : tag`.

- [ ] **Step 4: Verify** — `ctest -R test_runtime_installer` PASS; qmllint clean (run via the full check in Task 6 or `scripts/check.sh --format-only` plus the lint step of a full `check.sh` on macOS; on Windows qmllint runs from the Qt bin dir: `C:/Qt/6.10.3/msvc2022_64/bin/qmllint.exe` per `.qmllint.ini`)

- [ ] **Step 5: Commit** `feat(ui): channel selector and resolved-version display`

### Task 6: Docs, ADR, full gate

**Files:**
- Modify: `docs/07-glossary.md` (+ `.ru.md`)
- Modify: `docs/04-components.md` (+ `.ru.md`) — runtime install sections (§4.16–4.17)

- [ ] **Step 1: ADR** in `docs/07-glossary.md` ADR log (next number): runtime release channels — stable/beta, stable composes via `nightly-tag.txt`, b-build remains the install identity, per-channel cache, beta fetch filters non-prerelease, fallback to beta with a visible note. Mirror in the ru file.
- [ ] **Step 2: Component docs** — `docs/04-components.md` runtime-install section: channel combo, default stable, pointer resolution, `installedTag` display. Mirror in the ru file.
- [ ] **Step 3: Verify the `--version` assumption** (spec): the code assumes stable `llama-server --version` keeps `build: <N>` in its output (`ServerCapabilities::extractBuildNumber` handles it; an unreadable tag cannot refuse a start). Probe once against a downloaded stable binary when convenient; if the output shape differs, record the finding in the ADR — no code change unless probing shows a regression in capability detection.
- [ ] **Step 4: Full gate** — build all, run every test (`ctest --test-dir build/win-msvc2022`), clang-format (`scripts/check.sh --format-only` works without Qt), qmllint. Known environmental failure to ignore: `test_export_renderer` GPU abort on this box.
- [ ] **Step 5: Commit** `docs: ADR for the runtime release channel`
