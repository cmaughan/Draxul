# Built in Personal Assistant host and server orchestration

**Summary:** Host persistent, folder-backed personal agents in Draxul with editable
instructions, provider/model choice, visible activity, bounded scheduled runs and
single-executor ownership. The host is built in like Kanban; the server owns work.

**Priority:** P2 — Complete and validate the persistent personal-assistant workflow.

## Windows gate sweep — 2026-10-08

- Unblocked MSVC compilation of `tests/personal_agent_tests.cpp`: explicitly
  extract the JSON state as `std::string` before comparing with `string_view`.
  The prior mixed comparison has ambiguous reversed overloads on MSVC.
- The full Debug inventory built and ran; this card is not ready to close.
  `personal schedule checks wake unopened agents and coalesce behind active turns`
  raised a Windows `remove_all` exception at `personal_agent_tests.cpp:488`,
  while `personal chart paths and decoding handle local reports and bad files`
  failed the non-ASCII path comparison at `personal_assistant_host_tests.cpp:162`.
  Neither failure was waived or repaired as part of the unrelated Windows gates.
- Evidence: `build-ninja-debug/validation-logs/20261008-150658-521060/ctest-behavior.log`.
  Overall inventory: 73/79 CTest entries passed in 232.92s; five core render
  comparisons passed. A clean-profile same-cache Debug startup passed; the
  default-profile timeout remains tracked by
  `65 windows-validation-timing -test.md`.

**Design:** [Personal Assistant initial implementation plan](../../plans/personal-assistant.md)  
**Status:** Course correction implemented (2026-10-06): personal agents are persistent provider terminal conversations, opened from a sidebar pill. The prior definition editor and one-shot runner are superseded.
**Scope:** Core repository; Windows and macOS.

## Current product direction — 2026-10-06

- Click **+** and chat in the existing Codex terminal conversation.
  New conversations default to `gpt-6.1-sol` (verified in the local Codex model
  catalog); explicit profile choices and native resumed-session models survive.
  Claude remains supported through the normal profile/managed-agent seam.
- Show one persistent sidebar pill, with an optional user-assigned name. No ID,
  model, instruction or scheduling form is required to start.
- Automatically create backing data under the shared Dropbox `Vault/PA` root.
  Prefix the conversation with its backing-folder location and bootstrap guidance;
  the agent reads/initializes its instructions and durable working data there.
- Use the ordinary managed interactive provider process and its existing session
  resume mechanism. The agent follows chat instructions; Draxul is not wrapping
  every message in a five-minute read-only job, preview or execution-pin workflow.
- Preserve the selected provider/profile's normal model, authentication and
  permission behavior. Naming is optional and does not change folder identity.
- Dropbox shares backing data; it does not by itself transfer native provider
  conversations or coordinate executors across machines.
- [x] Replace the form/one-shot flow with create/open/rename pill and terminal chat.
- [x] Verify bootstrap location, stable identity, re-open and naming.
- [ ] Manually review native provider resume across a server restart and the populated sidebar in two GUIs.
- [x] Record aggregate, startup and relevant render evidence for the correction.

The earlier slice evidence below records what was built and tested, not the
accepted UX. Scheduling and automatic handoff need a fresh design review against
this conversational model before implementation.

## Context-menu follow-up — 2026-10-06

- [x] Verify the anchored menu in an actual Metal render capture. A temporary
  render-test probe displayed the title and Delete row; the probe was removed.
- [x] Support macOS Control-click as a secondary click on personal pills. Previously
  this took the primary-click path and opened the conversation instead.
- [x] Include renderer padding in menu dimensions and pointer row calculation;
  the bottom row is no longer clipped and click targets match drawn rows.
- [x] Exercise a complete 2x-scale input sequence through the dispatcher and real
  palette host: open, release, draw, select Delete, create confirmation, confirm.
  Both secondary-button and macOS Control-click paths pass.
- [ ] Confirm the user's original right-click symptom is resolved on their running
  client. A literal right-click failure has not been reproduced locally; the
  platform and exact symptom were requested during this follow-up.
- [x] Record post-pull aggregate and same-cache startup validation.

Follow-up validation costs (macOS, same Release Make cache): configure/generate
16.3s (14.8s + 1.5s); compilation passed for `draxul-tests-core` (separate duration
not retained). Focused diagnosis before integration: 2 input/menu cases passed
in 0.02s, including both macOS context-click sections; that iteration was repeated
by the aggregate after pulling upstream. Aggregate: 56/56 CTest entries passed
in 22.19s. Release app build/startup passed in 4.32s combined. Personal Assistant
render smoke: 1/1 passed in 1.40s. The earlier temporary Metal menu capture proved
the menu's visibility; the final render smoke covers the entry screen. Windows
and remote CI were not run locally. `git diff --check` passed. No debug probe
remains in production sources.

## Pill interaction refinement — implemented

- [x] Inline double-click rename using the shared Space/tab/pane editor.
- [x] Right-click menu with Delete and an explicit confirmation; stop the agent
  and remove its pill, retaining backing files in the collection's `.deleted/`
  folder so removal is recoverable. The confirmation explains this behavior.
- [x] Open new conversations in a new tab; re-open existing conversations in place.
- [x] Render Add as a compact `+` button separated from the last agent by a blank
  row, with a hit target limited to the button.
- [x] Validate input/layout, confirmed removal and new-tab routing, then record
  one aggregate and same-cache startup/render checks.

Evidence for this refinement:

- Personal pills use `RenameEditor::PersonalAgent` with stable identity, inline
  text/caret and the same Enter/Escape handling as Space/tab/pane renaming. The
  former naming popup is removed. The + button has no index/status text, sits one
  blank row below the agents and uses only its own rectangle as its hit region.
- Right-click opens an anchored menu. Delete opens a second pointer/keyboard menu
  with Cancel selected initially; dismissing it does not submit a mutation.
  Confirmed removal closes matching local terminals and atomically moves the
  backing folder into `.deleted/<id>`. The scanner recognizes that removal rather
  than retaining a ghost pill. Repeated request IDs return the stored result.
- Personal launch creates a single-terminal tab directly, without a temporary
  shell/split, and the client focuses it after projection arrives. Reopening a
  pill still focuses its existing terminal. Deleting the final tab creates a
  normal terminal tab first, preserving a usable Session.
- Server integration verifies separate tab creation, no extra pane, unconfirmed
  deletion leaves the process/files alone, confirmed deletion removes only the
  agent and preserves backing files, result replay, and the final-tab case.
  App tests cover inline rename/cancel, caret and + geometry/hit bounds,
  right-click routing, menu pointer selection, outside dismissal and Cancel as
  the default confirmation choice.

Validation costs (same macOS Release Make cache):

| Check | Result |
|---|---|
| Configure/generate | No reconfigure needed; existing cache reused. |
| Compilation | Initial app target passed; separate timing not retained. Final `draxul-tests-core` aggregate build passed in 18.05s including build overhead. |
| Focused diagnosis | Server personal selection: 7 cases / 194 assertions passed in 2.65s, before adding final-tab and UI menu coverage. |
| Aggregate | `python3 do.py test release`: 56/56 CTest entries passed in 24.10s, including final-tab removal and the complete UI tests. One aggregate pass. |
| Release startup | `python3 do.py run release --console -- --smoke-test`: passed in 7.11s including the incremental app build. Includes the final help-text copy update after the aggregate. |
| Render | Personal Assistant entry scenario 1/1 passed in 1.40s; inspected its exported Metal image. Populated pills and menus are exercised through layout/input/render-cell tests rather than this disconnected entry screenshot. |
| Other | `git diff --check` passed. No live provider or Windows/remote CI run; standard cross-platform CI remains applicable. No overlap between the final aggregate, startup and render commands. |

The default live server was not restarted. Existing manual two-GUI/native resume
and broader future-slice gates remain pending; these interaction changes are ready
for review with the rebuilt app/server.

## Course correction validation — 2026-10-06

- **+ Add agent** creates a Codex-backed folder and starts its normal interactive
  terminal immediately. Default model is `gpt-6.1-sol`, confirmed in this machine's
  `/Users/cmaughan/.codex/models_cache.json`. Explicit profile model arguments
  take precedence, and native resumed sessions retain their own model.
- Optional pill naming is a double-click action. Existing conversations focus by
  stable personal identity; an attempted duplicate in the same Session reuses its
  terminal, and another Session receives an explicit already-open result.
- Removed the definition form, manual-run service, execution pin, job history and
  special read-only provider adapter. The same managed terminal infrastructure
  owns personal and ordinary agents. Bootstrap gives the absolute Dropbox path,
  instructs the agent to read/initialize missing instructions and state, preserve
  data, update useful checkpoints and follow chat requests.
- Folder metadata is current schema 3: ID, display name, provider profile and
  revision. Renaming leaves the agent-owned `instructions.md`, `state.md` and
  `data/` alone. Updated the empty live collection manifest and verified it from
  an isolated server; no demonstration agent was added to the user's collection.
- Interactive fake-provider integration covers Codex default/explicit models and
  Claude arguments, bootstrap paths, ordinary chat input, one terminal on repeated
  open, a second observing client, and invalid identity rejection. Store tests
  cover atomic creation, idempotency, stale rename rejection and missing-file
  bootstrap. Host/input tests cover form-free entry, pill clicks and renaming.
- Native resume remains the existing managed Session mechanism, conditional on
  its configured policy and an official provider session reference. Explicitly
  closing a terminal ends its process; later opening can start a fresh chat from
  backing data. No new distributed scheduler or conversation transfer is claimed.

Validation cost for this correction (same macOS Release Make cache):

| Check | Evidence |
|---|---|
| Configure/generate | 19.6s / 2.2s, successful; later commands reused the cache. |
| Focused iteration | Personal host: 2 cases, 16 assertions, 0.02s. Server/store/chat: 7 cases, 117 assertions, 2.50s before the Add/default-model refinement. |
| Compilation | Focused host/server targets built; app build passed in 35.60s. Final aggregate build `draxul-tests-core` passed in 41.81s; timings include incremental build overhead, not only compiler CPU time. |
| Aggregate | `python3 do.py test release`: 56/56 CTest entries passed in 29.24s, including the final Codex default-model and pill-input cases. One aggregate pass for this correction. |
| Final Release/startup | `python3 do.py run release --console -- --smoke-test`: app build and same-cache startup passed in 11.09s combined. Overlapped the aggregate's test phase, not its build phase. |
| Render | Personal Assistant entry scenario 1/1 passed in 1.45s; inspected exported Metal image, with no definition fields. Superseded editor scenario was removed. |
| Real configured root | Fresh isolated server loaded schema 3, `cmaughan-personal-assistant`, the exact `Vault/PA` path and an empty error in 0.34s. Test server shut down afterward. |
| Other | `git diff --check` passed. Live interactive provider/model execution and Windows/remote CI were not rerun for this correction. Earlier one-shot live-provider evidence does not validate this new terminal path. |

The existing default server was left running. Restart it with the rebuilt binary
to load the new root/capability and review Add → chat → rename. Keep this card
pending for the explicit manual gates and the redesign of remaining slices.

## Earlier decisions and constraints (superseded where above differs)

- A definition is independent of a run, pane, tab and Session. Closing a view must
  not stop orchestration. Active runs also appear in the existing Agents section.
- Use a user-selected collection root, potentially in Dropbox. Synced files hold
  durable definitions and results, not credentials or distributed execution locks.
- Preserve provider choice through supported adapters; no implicit model fallback.
  This is hosting/orchestration, not a new reasoning framework or Outlook connector.
- One executor owns the collection. Coordinate cross-machine claims through an
  atomic lease service, not a heartbeat file. Graceful handoff verifies process
  exit and checkpoint availability; expiry is not proof an old worker stopped.
- Coordinator backend/authentication remain an implementation design gate. Pinned
  single-machine execution is only an intermediate milestone. No unsafe forced
  takeover or claims of exactly-once external actions.
- Existing launch is pane/topology-backed and agent projection Session-scoped;
  extract only the narrow run seam needed, preserving current interactive agents.

## Delivery checklist

- [x] Inspect current feature inventory, agent/server boundaries and Kanban host pattern.
- [x] Write and index the initial plan with ownership risks and vertical slices.
- [x] Slice 1: strict store, server collection projection, read-only host and persistent rail entries.
- [x] Slice 2: creation preview, revision-safe instruction edits, runtime/model selection, manual runs and active Agents projection.
- [ ] Slice 3: intervals, durable messages, pause/cancel, budgets, retries and crash/sleep recovery.
- [ ] Resolve and record the real coordinator backend, provisioning and authentication decision.
- [ ] Slice 4: automatic unowned acquisition, owner display, graceful handoff and fail-closed uncertainty.
- [ ] Slice 5: finish host/CLI UX, render coverage and shipped feature/module-map documentation.

## Acceptance and validation

- [x] Two local clients in different Sessions see consistent definitions and live run status; closing all GUIs leaves server work running.
- [x] Instructions/model changes survive server restart and affect only the intended run revision; invalid/conflicted files cannot silently launch work.
- [x] A supported live provider completes a harmless local task with bounded inspectable output; absent authentication is actionable.
- [ ] Scheduling and messaging integration tests cover duplicates, overlap, limits, timeouts, loops and ambiguous interrupted runs without blind replay.
- [ ] Ownership tests cover simultaneous claims, stale generations, renewal loss, delayed sync, interrupted handoff and resume from sleep.
- [ ] Perform a real two-machine Windows/macOS graceful handoff; verify no overlapping runs and correct checkpoint transfer using the selected coordinator.
- [ ] Record one core aggregate and same-cache smoke for each completed slice, with relevant UI render checks and normal cross-platform CI.
- [ ] Complete final Release startup confirmation and record remaining limitations.

## Evidence and next step

### Slice 2 implementation — 2026-10-06

- User explicitly requested Slice 2, authorizing work beyond the earlier review
  boundary. Initially no live collection was configured on this Mac; checked-in definitions
  are under `examples/personal-assistant/agents/news/`.
- User subsequently selected Dropbox `Vault/PA` for every machine. Initialized
  `/Users/cmaughan/Dropbox/Vault/PA` with shared identity
  `cmaughan-personal-assistant`, an empty `agents/`, and current schema 2; configured
  this Mac's `personal_root`. Recorded the location convention in `CLAUDE.md`.
  Other machines must point their local setting to the same synced hierarchy.
- Delivered creation preview/acceptance, revision-safe editing, explicit provider
  and model selection, manual run/cancel and inspectable history through the host.
- Execution requires an explicit local pin for each server lifetime, guarded
  by a machine-local collection lock. No synced setting authorizes another machine.
  Scheduling and distributed ownership remain disabled until their later slices.
- Definition publication must be atomic across settings and instructions, detect
  external edits even without a revision bump, and preserve immutable run input.
- Keep provider execution and filesystem work off the server state/UI threads;
  bound runs and output, surface authentication/model errors, and preserve user
  model selection without fallback. Validate with isolated collections/runtimes.

Planning inspected `docs/features.md`, `docs/module-map.md`, the server's agent
runtime projection, topology-backed managed launch, and the built-in Kanban module.
No equivalent personal collection feature was found in the inspected feature
inventory. The first implementation review point is a read-only collection visible
in two clients. Store, protocol, server scan service, asynchronous client polling,
read-only grid host, built-in registration and persistent sidebar integration are
implemented in Slice 1. Execution remained disabled at that boundary. A partially applied patch initially left
the host outside the build; this wiring is now complete. See
`docs/personal-assistant.md` and `examples/personal-assistant/` for setup.

### Slice 2 validation and handoff — 2026-10-06

- Implemented schema 2 publication, complete-definition compare-and-save, immutable
  run revisions, explicit preview/save/run confirmation, provider/model selection,
  local execution pin, cancellation/history, async client command/results, headless
  CLI and active-run rail projection. Worker-owned filesystem/process work does not
  block server state or UI threads. Supported adapters are Codex exec and Claude
  print; no provider fallback, automatic schedule or cross-machine election.
- Real server integration uses authenticated clients in two Sessions, observes a
  shared edit, closes the initiating client, and observes completion from the
  second. Service-only tests also run with no GUI. Restart reloads saved model and
  history with execution disabled; edits during a run retain its original revision.
  These are automated client/host checks, not a manual two-GUI acceptance review.
- Opt-in live Codex test used the configured `gpt-6-astra` model and obtained
  `PERSONAL_ASSISTANT_OK`. Temporary collection and local output were removed by
  the fixture. Fake-provider coverage verifies cancellation, overlap rejection,
  nonzero failure and missing-final-result rejection; errors identify authentication,
  model access and local logs. Live Claude and Windows execution were not run here;
  normal cross-platform CI remains applicable.
- Fresh isolated server loaded the real `/Users/cmaughan/Dropbox/Vault/PA` root,
  shared collection identity, both provider profiles, zero definitions/runs, no
  errors and execution disabled. Shut that server down after inspection. The
  pre-existing default server remains running; it needs a deliberate restart with
  the new binary to load the configured root. Other machines still need their
  local absolute `personal_root` set to the same Dropbox `Vault/PA` hierarchy.

Validation costs (one existing macOS Release Make cache throughout):

| Check | Result and elapsed time |
|---|---|
| Configure/generate | Successful final configure 14.1s / 1.5s. One earlier configure rejected an unregistered render scenario; fixed the manifest. Later runs reused the cache. |
| Compilation | App, server tests, personal-host tests and fake provider built successfully; final aggregate target `draxul-tests-core`, final startup target `draxul`. Separate compilation timings were not retained. An initial test compile assertion mismatch and invalid hidden-test tag were corrected before passing runs. |
| Focused iteration | Server 8 cases / 112 assertions, 3.93s; host 3 cases / 29 assertions, 0.02s. Later server + opt-in live provider 10 cases / 141 assertions, 45.78s. These preceded final fixes and aggregate validation. |
| Core aggregate | `python3 do.py test release`: final 56/56 CTest entries passed, 22.72s. Two earlier full passes (39.74s and 45.75s) were repeated after discovering global-paste routing and clipped editor fields. |
| Startup/smoke | `python3 do.py run release --console -- --smoke-test`: passed against the same Release cache, Metal initialized and process exited successfully. Repeated once after the editor layout fix; separate smoke timings were not retained. Startup/build work overlapped aggregate test execution. |
| Render | `ctest --test-dir build -C Release --parallel 4 -R '^draxul-render-personal-assistant.*smoke$' --output-on-failure`: final 2/2 passed, 2.76s; inspected both exported images. Initial 2/2 pass also took 2.76s but visual review found clipped fields, prompting the layout correction and repeat. Render checks briefly overlapped aggregate execution. |
| Other | Isolated real-root CLI check passed in 0.26s; `git diff --check` passed. No remote CI run requested. |

Remaining: Slice 3 scheduling/messages/recovery, coordinator decision and Slice 4
handoff, then Slice 5 polished UX. The editor is intentionally basic (append,
backspace, clear, paste); full scrolling/selection and richer editing remain UI
polish. Manual two-GUI review and real two-machine handoff remain outstanding.
The global validation checklist stays pending because it applies to all slices.

### Slice 1 validation and review boundary

- Windows Ninja Debug, `py do.py test debug`: compilation passed; latest aggregate
  passed 53/56 CTest entries in 57.28 seconds. New server/store/protocol,
  personal-host, configuration/CLI and sidebar coverage passed. The real-server
  test uses two authenticated clients with different Session parameters, verifies
  shared updates, rejection of execution and no additional terminals, and checks
  retained client data after disconnect. The host test covers selection and
  closing/reopening a view. These are not a substitute for a manual two-GUI review.
- Remaining aggregate failures: joined family emoji (existing card 63), Windows
  test-scope fixture (existing card 64), and the concurrently developed flashcards
  product-scope expectation in `tests/do_py_tests.py`. The flashcards chat was
  notified; unrelated code was not changed to force a green aggregate.
- Latest configure/generate: 75.4s / 2.2s. Combined configure/build: 93.70s for
  the core aggregate target (20 Ninja steps); compilation/other build overhead
  accounts for the remaining approximately 16.1s. No separate focused test pass.
- Earlier aggregate: 51/56 in 66.22s; fixed two new fixture assumptions (a sidebar
  needs a Space, and the server already has its bootstrap terminal). This repeat
  is intentional. Earlier attempts also included an incomplete include dependency,
  a concurrent missing flashcard source at configure (~74s), and one lease refusal
  (~0.7s). A separate workflow's successful shared build is not counted as our test.
- `py do.py smoke --skip-build`: passed against the same executable; no rebuild.
  It briefly overlapped the end of the aggregate; separate elapsed timing was
  not captured. The wrapper bounds execution to 30s.
- `ctest --test-dir build-ninja-debug -C Debug --parallel 4 -R
  '^draxul-render-personal-assistant-smoke$' --output-on-failure`: 1/1 passed in
  2.79s. Inspected the exported Windows bitmap: title, controls and disconnected
  waiting state render legibly. Populated content is covered by host tests, not
  by this disconnected render scenario.
- Scoped `git diff --check` passed. No macOS/remote CI run or final Release build
  was performed in this slice. Normal cross-platform CI remains applicable.
- The existing default Release server was left running and user configuration
  was not changed. A new client may require that server to be deliberately
  restarted with the new executable after setting `[agents].personal_root`.
- Slice 1 review requested at that time: configure a collection, open Personal Assistant in two real GUIs,
  edit instructions externally and verify convergence/diagnostics. Then review
  before starting Slice 2 (creation/editing/manual execution).

Keep this card pending until every delivery and validation box is checked. Record
slice decisions, commands, costs and results here as work proceeds.
