# 10. External runtime: per-task endpoints and secrets

How the external mode is wired: one shared server plus a per-task row with
its own endpoint, API key and model alias, and the keys in the OS secret
store. The decision records are ADR 226–235 in `docs/07-glossary.md`.

## 1. Connections

In external mode the app talks to OpenAI-compatible servers it does not own
and does not start. The model mirrors the managed runtime (ADR 226): the same
four tasks (`core/ConnectionRole.h` — `layout`, `blockRecognition`,
`decision`, `fix`), and every task carries its own endpoint, its own API key
and its own model alias.

| What | Where | Fallback |
|------|-------|----------|
| Shared base URL | `provider/baseUrl` (`SettingsStore`) | — |
| Task endpoint | `tasks/<role>/baseUrl` | empty → the shared URL |
| Model alias | `model/name`, `tasks/<role>/modelName` | — |
| Shared API key | secret `provider/apiKey` | — |
| Task API key | secret `provider/apiKey/<role>` | empty → the shared key |

The rule is **empty means shared**, and the interface reads it literally: an
empty endpoint cell shows the shared URL in its hint, an empty key cell says
the shared key is in use. There is no reverse rule — a task with its own key
never falls back to the shared one (`SecretStore::readFirst` reads the task's
own id first, the shared one after it).

The request path is not configurable — it belongs to the protocol:
`/v1/chat/completions` for every task, `/v1/systemone` for the decision task
(`LlamaClient::endpointUrl`). A provider with a non-standard prefix expresses
it in the base URL itself.

### Where it is configured

**Settings → Runtime → External** (`src/qml/Settings/RuntimeTabExternal.qml`):
a shared-server block (base URL + key) and one `Settings/TaskConnectionRow.qml`
per task — endpoint, key, alias. The first-run wizard's External step
(`Setup/StepExternal.qml`) shows the shared server only, in a compact mode;
the per-task table lives in the settings. The **Models** window shows the same
four tasks in external mode, but its alias cell is read-only there — the alias
belongs to the connection and is edited in one place (ADR 226).

### Resolution

`RuntimeController::resolveExternal(role)` builds the static part: the task's
effective base URL and its alias from the settings. The key read is
**asynchronous by design** — a locked keychain asks the user and would freeze
the interface — so the first resolve per role reads the secrets in a worker
thread and delivers the answer to the GUI thread; the same pass fetches the
server's context window (§3). Resolved connections are cached per role
(`m_resolvedCache`) and cleared on any settings change or server state
transition. A key read that ends in `Refused` is not cached: the error
travels in `ResolvedConnection::error` and the request does not go out (§2).

## 2. Secrets

API keys and the Hugging Face token live in the OS secret store
(`src/config/SecretStore.*`, layer `llocr_config`; ADR 227) — never in
`QSettings`, never on a command line, never in a log, never in a `.llocr`
project. One interface, three backends, no new dependencies:

| Platform | Backend | API |
|----------|---------|-----|
| macOS | Keychain, generic passwords, service `org.llocr.app` | Security.framework (`SecItemAdd` / `SecItemCopyMatching` / `SecItemDelete`) |
| Windows | Credential Manager, target `LLocr:<id>` | `CredWriteW` / `CredReadW` / `CredDeleteW` |
| Linux | Secret Service, `org.freedesktop.secrets` | D-Bus through `Qt6::DBus` (Linux only) |

### Missing and refused are different answers (ADR 233)

`SecretStore::Status` tells the kinds apart: `Ok`, `NotFound`, `Refused`,
`Unavailable`, `Failed`.

- **`NotFound`** — there is no key. That is not a failure: a server without
  auth needs none, and the request goes out.
- **`Refused`** — the item is there but belongs to another build (on macOS
  the Keychain binds an item to the code that wrote it). The request does
  **not** go out — it would fail `401 API key required` — and the refusal
  travels with the connection in `ResolvedConnection::error`, naming the
  reason and the way out (answer the Keychain prompt with "Always Allow", or
  delete the item in Keychain Access and enter the key again). Removing a key
  the store refuses reports the refusal instead of staying silent. A server
  `401` is translated into the app's own words; the server's text stays in
  the message.

The External tab polls the store state (`RuntimeTabExternal.refreshSecretState()`),
so the reason is visible where the key is edited.

### macOS: a rebuild loses the key (ADR 232)

The identity of an ad-hoc signed dev build is its code hash, so a rebuild
loses every key it wrote. `scripts/macos-dev-sign.sh --create` signs the dev
build once with a self-signed "LLocr Dev" certificate; a failed read or write
reaches the UI, never an empty field.

### Storage rules

- When the store is missing or refuses a write, the key lives in the process
  memory until the app exits, and the UI says exactly that. There is no
  encrypted-file fallback — a hand-rolled cipher would look like protection
  without being any (the ADR 212 refusal).
- Secrets are read once per session and cached in the process: the request
  path needs the key on every call and a store read can block. Writes and
  removals refresh the cache (`SecretStore::forgetAllCached`).
- A stored key is never shown back: the field carries the mask as its own
  text, a single "Show" switch in the table header reveals what was typed
  (never what is stored), and an **empty field removes the key** — there is
  no delete button (ADR 283).
- The one-time settings reset (ADR 253) does not touch the OS store: keys
  written there survive it. There is no plaintext-key migration — a key that
  was still in the settings file when the schema changed is gone and is
  entered again.
- Test and tool builds write under their own namespace
  (`SettingsStore::setSecretNamespace`), so tests never touch the app's
  keychain items.

## 3. Wire behaviour

- **The request goes to the task's endpoint** — the shared URL or the task's
  override — with the task's key in the `Authorization` header and the task's
  alias as the model name.
- **The System One request names the model that answers** (ADR 229): the
  `model` field is part of the `/v1/systemone` protocol — llama.cpp answers
  with the loaded model's name or alias, a router uses the field for
  routing, and a server that requires it answers `422` without it.
- **The external server's context window is read from `/v1/models`**
  (`max_model_len`, ADR 231): one request per server at resolve time, cached
  per server, carried in `ResolvedConnection::contextSize`. The generation
  budget (`core/RequestBudget`) clips `max_tokens` to the window. The server
  publishes no image-token budget, so the prompt estimate takes the shipped
  models' visual floor (70–1120 tokens; 1024 is used). A request that does
  not fit the window is dangerous: some servers (omlx) answer **HTTP 200**
  with an error body and no `choices` after unloading the model — which is
  why the budget is clipped in advance.
- **A server that ignores the reasoning budget gets the fix request once
  more without thinking** (ADR 230): `reasoning_budget_tokens` (ADR 193) is a
  llama.cpp parameter; omlx ignores it, the thoughts eat the whole
  `max_tokens`, and the reply comes back with no `content`. The fix task
  retries that request with thinking off, says so in the footer, and the
  following requests to the same server skip thinking outright. A reply with
  no choices names its cause (server error, spent budget, empty content)
  instead of a bare "no choices".
- **The client keeps its own request journal** (`core/RequestJournal`,
  ADR 228): one record per HTTP call — endpoint, model, request and response
  sizes, HTTP status, duration and the head of the reply (capped excerpt).
  The log window shows it in external mode, where there is no server process
  to read output from. The journal lives in memory (the last 500 records —
  a page is the user's document) and the API key never enters it.

## 4. Appendix: model repositories for external use

Repos that were listed for serving the tasks from an external server
(`hf download <repo> --local-dir <dir>`, then give the task the endpoint and
the alias the server knows the model under — e.g. `http://127.0.0.1:8000/v1`):

| Repository | Tasks | Note |
|------------|-------|------|
| `usermma/MinerU2.5-Pro-2605-1.2B-mlx-fp16` | layout + blockRecognition | MLX build; its Cyrillic tokenization is broken — the layout reply comes back transliterated, use a llama.cpp GGUF build of MinerU for Russian documents (`docs/13-model-mineru.md`) |
| `mlx-community/GLM-OCR-bf16` | blockRecognition | MLX build |
| `djrsystemservices/gemma-4-12B-it-qat-oQ4e-mtp` | fix | |
| `Jundot/d3-mini-oQ8e` | decision | **cut** (ADR 251) — the decision task speaks the visual-diff protocol now; the profile lives in `archive/models/profiles/` |

## 5. Design decisions

- **ADR 226** — the external runtime mirrors the managed one: per-task
  endpoint, key and alias; empty field = the shared value; one editor for the
  alias.
- **ADR 227** — secrets live in the OS secret store, not the settings file;
  no encrypted fallback, no plaintext migration path.
- **ADR 228** — the external server keeps no log, so the client keeps its own
  request journal: in memory, the key stays out.
- **ADR 229** — the System One request carries the `model` field: part of the
  protocol, not an option.
- **ADR 230** — a server that ignores the reasoning budget is retried once
  without thinking, with a footer note; a reply without choices names its
  cause.
- **ADR 231** — the context window comes from `/v1/models` (`max_model_len`)
  and the response budget is clipped to it.
- **ADR 232** — the macOS Keychain binds a key to the build that wrote it;
  dev builds need a stable signing identity.
- **ADR 233** — `NotFound` and `Refused` are different statuses; a refusal
  travels with the connection and stops the request.
- **ADR 235** — the decision task on `/v1/systemone` speaks the visual-diff
  protocol `systemone_visual_diff_v1` (two images, P(consistency), no
  instructions).
- **ADR 283** — the key field is the key's editor and its delete button: the
  mask is the field's text, an empty field removes the key, one "Show"
  switch per table.

## 6. Links

- Glossary and decision record: `docs/07-glossary.md` (ADR 226–235, 283).
- The fix task's models: `docs/16-model-qwen3.5.md`.
- The decision model: `docs/17-model-minicpm-jev.md`.
- Russian mirror: `docs/10-external-runtime.ru.md`.
