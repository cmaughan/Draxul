# Draxul Review Consensus

**Date:** 2026-07-15
**Reviewed snapshot:** `8fba7b8ca03a` (`ScoreView: remove the Salamander soundfont fetch — just the Yamaha`) plus the current review-file edits
**Inputs:** `review-latest.gpt.md`, `review-latest.claude.md`, and `review-latest.gemini.md`
**Tracker used:** `kanban/pending`, `kanban/done`, and `kanban/ice-box`; the `plans/work-items*` paths named by the old prompt no longer exist

## Who was in the room

- **[GPT/Codex, gpt-5.6-sol]** supplied the strongest current-tree reconciliation, found the ScoreView microphone race and pane-print state-machine gaps, and argued for a reliability-first sequence.
- **[Claude, Fable 5]** supplied the deepest module-boundary, duplication, god-host, testing, and multi-agent collision analysis. Claude also contributed most of the ScoreView-specific quality-of-life proposals.
- **[Gemini 3.5 Flash, Medium]** independently confirmed the microphone and Windows process races, emphasized callback lifetime and configuration friction, and proposed several focused workflow features.
- **[Consensus/GPT-5 Codex]** re-read all three reports, checked disputed claims against the current tree, reconciled them with the 50 existing pending cards, 450 done cards, 76 ice-box cards, active design plans, tests, CMake, and CI, and filed only uncovered work.

The room agrees on the shape of the problem. Draxul's lower layers, dependency injection, pure logic, test breadth, optional host registration, and cross-platform renderer intent are strong. Risk is concentrated where products meet the application: process and device lifetime, persistence, `App`, the three largest product hosts, manually mirrored GPU contracts, one monolithic test target, and steering documents that no longer describe the tree.

## Consensus position

This should be a reliability and integration-boundary wave:

1. Fix the new ScoreView microphone ownership race and reopen the regressed non-blocking Windows shutdown work.
2. Complete the existing network, session, renderer, callback, provider, and workspace-invariant bug cards.
3. Make ScoreView rebuilds, pane printing, and frontend device lifecycles bounded and testable.
4. Land safety nets before moving large classes: macOS session attach, render contracts, ScoreHost orchestration, worker/device stress, progress persistence, and hostile input.
5. Decompose `App`, Chrome, SatView, MegaCity, and ScoreView only behind those tests.
6. Repair the build/test/documentation sources of truth before expanding more app-wide behavior.
7. Treat the quality-of-life list as a sequenced backlog, not as permission to land features ahead of unresolved reliability roots.

Claude places modular tests and documentation especially high because they enable safe parallel work. GPT places the microphone, process, network, and persistence defects first because they can lose resources, execute unintended commands, lose state, or freeze the UI. Gemini reinforces the same lifetime/security side. The reconciled order is reliability first, then the tests that make refactoring safe, then refactors, then features.

## Findings the room accepts

### Bugs and lifecycle hazards

| Priority | Finding | Agents | Current-tree decision |
|---:|---|---|---|
| 00 | `MicPlayerInput` publishes its SDL stream after its Boolean ownership handshake, allowing destruction to race the unsynchronised `stream` read/write and leak an active capture stream. | GPT, Gemini; Claude flagged the detached opener as a lifecycle risk | **Confirmed and new.** File `00 scoreview-microphone-open-lifetime-race -bug.md`. |
| Reopen | Windows Nvim and ConPTY shutdown can synchronously wait for up to two seconds on the UI thread. | GPT; Gemini separately found the handle race | **Confirmed regression.** Reopen `done/07 shutdown-blocking-wait -bug.md`; do not create a duplicate. The handle race remains in pending item 09 and is a distinct failure mode. |
| Existing 00 | Weather and SatView execute interpolated `curl` commands through a shell; cancellation, parsing, encoding, and bounds are weak. | GPT, Claude, Gemini | **Unanimous.** Existing `00 network-shell-transport -bug.md` already has the right shared native-transport scope. |
| Existing 02/17 | Session topology and runtime metadata truncate their final files before a replacement is known good. | GPT; Gemini asked for rollback fault coverage | **Confirmed.** Existing atomic-session and rollback cards own it. |
| Existing 04-06/19/28 | Vulkan/Metal resource, shader dependency, and ABI contracts are manually mirrored and have known correctness gaps. | GPT, Claude, Gemini | **Strong agreement.** Keep the existing focused bug/test/helper cards; do not attempt a cross-backend rewrite. |
| Existing 09 | `NvimProcess::is_running()` can race handle closure on Windows. | Gemini | **Confirmed and already pending.** This is not the same as the blocking-shutdown regression. |
| Existing 10 | Platform callback slots can outlive `SdlWindow`; `InputDispatcher::connect()` also installs raw-`this` callbacks without a symmetric disconnect. | Gemini; Claude emphasized callback ownership | **Confirmed lifetime family.** Expand pending item 10 to cover the dispatcher/window callback teardown contract rather than filing another card. |
| 14 | Pane printing has no capture timeout, leaves temporary PDFs behind, can collide on names, exposes a generally available action despite a macOS-only backend, and lives directly in `App`. | GPT; Claude rated printing a high cost/benefit drag | **Confirmed and new.** File `14 pane-print-state-machine -bug.md`; the plan makes behavior explicit and functional on Windows as required by Draxul's cross-platform policy. |
| 14 | `WindowEngraver::cancel()` blocks while a Verovio job is active, and `ScoreHost::rebuild_window()` then performs more synchronous work on user-driven paths. | GPT; Claude identified ScoreHost as a collision hotspot | **Confirmed and new.** File `14 scoreview-nonblocking-rebuild -bug.md`. |

### Architecture and multi-agent workability

| Theme | Agents | Consensus |
|---|---|---|
| `App`/Chrome/session concentration | GPT, Claude, Gemini | The 2,975-line `App`, 1,543-line `ChromeHost`, and 955-line `main.cpp` remain the central merge hotspots. Pending items 22-25 are correctly scoped and should precede more app-wide features. |
| Product god hosts | GPT, Claude; Gemini noted module edit tax | SatView (5,284 lines), MegaCity (2,992), and ScoreHost (2,778) serialize otherwise independent work. SatView and MegaCity are already pending 26/27. ScoreHost needs its own decomposition card after host-level tests. |
| Dual-backend duplication | GPT, Claude, Gemini | Shared scene models, ABI declarations, and proven resource helpers are the right boundary. Backend implementations should remain backend-specific. Existing 19 and 28 own this. |
| Configuration ritual | GPT, Claude, Gemini | Existing 07/21 should first restore correctness, then introduce one declarative schema. Do not mix a broad schema migration into a bug fix. |
| Host/foundation leakage | GPT, Claude; Gemini noted `draxul-types` creep | Existing 12/31 should add provider metadata, declare the real `draxul-host -> draxul-nvim` edge, reduce `runtime-support` propagation, and revisit SDL key vocabulary through the original completed SDL-boundary cards rather than a synonym card. |
| GUI/UI contracts | GPT, Claude | Existing 29 remains the right place to clarify `draxul-gui` versus `draxul-ui`; renaming is optional, an explicit ownership contract is not. |
| Test structure | GPT, Claude; Gemini praised broad coverage | Sharding improved execution, but one executable still links every enabled product and reaches into private module sources. Existing 35 should include a ScoreView-host target and reopen the completed private-boundary card where necessary. |
| Documentation and tracker drift | GPT, Claude, Gemini | Existing 34 owns canonical feature/docs hygiene; several completed steering-doc and board-navigation cards need reopening. Product detail should move out of enormous single-line feature-table cells. |
| Performance probes | GPT, Claude, Gemini | `PERF_MEASURE()` pays timestamp and mutex costs in hot functions even when disabled. Existing 33 owns measurement and simplification. |

### ScoreView test gaps accepted as new work

| Priority | Gap | Agents | Decision |
|---:|---|---|---|
| 15 | The actual `draxul-scoreview-host` target is not linked into tests; host initialization, mode changes, window installation, resize, drawing, and shutdown are untested. | GPT, Claude | File `15 scoreview-host-orchestration -test.md`. |
| 16 | Opener/engraver/MIDI/audio interleavings, RtMidi constructor/destructor throws, multi-instance devices, queue bounds, and instrument switching lack one deterministic stress suite. | GPT, Claude, Gemini | File `16 scoreview-worker-device-stress -test.md`. Mic ownership remains tested in its owning bug card. |
| 17 | Progress persistence has only happy-path tmp+rename coverage, not injected create/write/flush/replace failures or last-good durability. | Claude | File `17 scoreview-progress-crash-safety -test.md`. |
| 18 | Verovio `.mxl` loading has a valid archive case and generic garbage case, but no bounded hostile ZIP corpus. | Claude; Gemini asked for corrupted notation input | File `18 hostile-mxl-inputs -test.md`. |
| 19 | `SourceSlicer` has one strong Grieg equivalence regression, but not randomized windows across a varied corpus. | Claude | File `19 source-slicer-corpus-equivalence -test.md`. |

The shared text-atlas Unicode/overflow/budget gaps remain acceptance work in `plans/satview-observatory-horizon-constellation-boundaries.md`; creating a second card would split one active plan. The new builder should return dropped keys and reasons, cap memory/counts, and add flags, ZWJ, combining, malformed UTF-8, elision, and overflow cases before that plan is called complete.

### New refactor accepted after tests

| Priority | Refactor | Agents | Boundary |
|---:|---|---|---|
| 21 | Decompose ScoreHost into session, stream, audio, view-model, and presentation responsibilities. | GPT, Claude | File `21 scoreview-host-decomposition -refactor.md`. It follows items 00, 14, 15, and 16 and must preserve the public provider/host contract. |

## Where the reviews disagreed or needed correction

- **CI triggers:** Gemini says CI is manual-only. That is stale. `.github/workflows/build.yml` currently runs on pushes and pull requests to `main`, with `workflow_dispatch` as an additional trigger. GPT and Claude describe the current state correctly. No CI-trigger card is created.
- **ScoreView architecture:** Claude praises the lower-level split and tests; GPT calls ScoreHost a god host. Both are correct. `FlowController`, `StreamComposer`, `PlayerModel`, `SourceSlicer`, and `WindowEngraver` are useful units, while the host still integrates too many policies in one file.
- **SoundFont coverage:** Claude says `SoundfontSynth` has no tests. The current tree has a real YDP soundfont test covering scheduled strike, release damping, silence, and load failure. Click-free switching and host integration are still missing; those go into items 15/16 rather than a duplicate golden-WAV card.
- **SatView math coverage:** Gemini proposes pole, meridian-wrap, zero-altitude, and finiteness tests. Current SatView geodetic, propagation, camera, map, solar-system, and Sun tests already cover these families. No duplicate test card is filed.
- **InputDispatcher callback lifetime:** Gemini's concern is plausible but belongs to the same registration/unregistration contract as pending item 10. Current shutdown sequencing reduces exposure; it does not justify leaving callbacks that capture a destroyed dispatcher.
- **“Worst features” lists:** The agents disagree in taste about Weather, persistent sessions, printing, BioView, SatView expansions, Kanban, and ScoreView. The consensus treats these as maintenance-risk signals, not a deletion mandate. Stabilize risky features; do not remove product scope from a review alone.
- **`do.py` monolith:** Claude's observation is fair, but pending item 14 already extracts review orchestration and existing design plans cover recent cleanup. Broader decomposition should be evidence-led after those changes, so no new general script-refactor card is filed.

## Completed work that must be reopened, not duplicated

The user's exclusion rule means these retain their original cards:

- `done/07 shutdown-blocking-wait -bug.md`: Windows Nvim/ConPTY synchronous waits are present again.
- `done/19 render-test-extraction-refactor.md` and related render-test extraction cards: production still links `draxul-render-test` and registers the NanoVG demo provider.
- `done/111 docs-design-stale-repair -refactor.md`, `done/12 claude-md-documentation-errors -bug.md`, and `done/01 stale-docs-navigation-paths-bug.md`: architecture and tracker paths are stale again.
- `done/14 test-module-boundary-violations -refactor.md`: tests again include optional-module private `src/` directories.
- `done/134 tsan-validation-and-ci-wiring -feature.md`: the preset remains, but the current workflow has no TSan job.
- `done/15 appconfig-sdl-decoupling -refactor.md` / `done/03 appconfig-sdl3-coupling-bug.md`: the config-specific leak improved, but SDL keycodes still form the cross-layer event vocabulary. Reopen the original boundary work if platform-neutral keys are still desired.
- `done/15 ihost-interface-width -refactor.md` / `done/40 ihost-interface-split -refactor.md`: `IHost` has grown new product/capability queries. Reopen rather than file another interface-width synonym.

## Existing pending work reinforced by this review

- **Bugs 00-14:** network transport, atomic sessions, render manifest/resource/shader correctness, config reload, Unicode overlays, Windows process handles, callback lifetimes, glyph errors, provider availability, workspace invariants, and safe review automation remain valid.
- **Tests 15-20:** Apple attach, render comparison, App rollback, SatView host smoke, shader ABI, and overlay failure tests are still the right safety-net layer.
- **Refactors 21-35:** declarative config, App/Chrome/session controllers, product boundaries, GPU helpers, GUI/UI contracts, font/foundation/catalog/perf/docs cleanup, and modular tests all survive reconciliation.
- **Features 36-49:** the prior future-feature cards remain behind the reliability wave. Nothing in this review promotes them ahead of bugs/tests/refactors.

## New feature backlog accepted from the room

These ideas are not in done or ice-box and are filed after the existing feature queue:

| Priority | Feature | Raised by | Card |
|---:|---|---|---|
| 50 | Move a live pane between workspaces without restarting its host. | GPT | `50 pane-workspace-move -feature.md` |
| 51 | Duplicate a workspace topology and launch descriptors. | GPT | `51 duplicate-workspace -feature.md` |
| 52 | Type-aware file-drop routing for Nvim, Markdown, ScoreView, directories, and safely quoted shell paths. | GPT, Claude | `52 type-aware-file-drop -feature.md` |
| 53 | Normal, single-line, and shell-escaped paste transformations. | GPT | `53 paste-transformations -feature.md` |
| 54 | Explicit selected-terminal input broadcast with conspicuous safety state. | GPT | `54 selected-pane-input-broadcast -feature.md` |
| 55 | Built-in split-layout presets plus named export/import. | GPT, Gemini | `55 layout-presets-and-templates -feature.md` |
| 56 | Export the focused pane as PNG through the pane-capture path. | GPT | `56 export-pane-png -feature.md` |
| 57 | Workspace/pane locks against accidental close, restart, or replacement. | GPT | `57 workspace-pane-lock -feature.md` |
| 58 | Structured command-palette filters and typed argument completion. | GPT, Gemini | `58 command-palette-structured-query -feature.md` |
| 59 | User-defined workspace/pane colors and short tags. | GPT | `59 workspace-pane-color-labels -feature.md` |
| 60 | Cross-platform trackpad pinch-to-zoom. | Gemini | `60 pinch-to-zoom -feature.md` |
| 61 | Drag-and-drop workspace-tab reordering using the existing reorder operation. | Gemini | `61 workspace-tab-drag-reorder -feature.md` |
| 62 | `--validate-config` without graphics initialization. | Gemini | `62 validate-config-cli -feature.md` |
| 63 | Terminal selection auto-scroll while dragging beyond the viewport. | Gemini | `63 terminal-selection-auto-scroll -feature.md` |
| 64 | ScoreView recent-piece library with progress summaries. | Claude | `64 scoreview-piece-library -feature.md` |
| 65 | ScoreView bar-range practice loops. | Claude | `65 scoreview-practice-loop -feature.md` |
| 66 | Configurable ScoreView metronome count-in. | Claude | `66 scoreview-count-in -feature.md` |
| 67 | Manual left/right-hand practice isolation. | Claude | `67 scoreview-hand-practice -feature.md` |
| 68 | End-of-session ScoreView recap. | Claude | `68 scoreview-session-recap -feature.md` |
| 69 | “Split with host…” palette/launcher flow driven by provider metadata. | Claude, Gemini | `69 split-with-host-launcher -feature.md` |
| 70 | ScoreView audio-output device picker with hot-unplug fallback. | Claude | `70 scoreview-audio-output-device -feature.md` |
| 71 | ScoreView MIDI auto-reconnect by stable port identity. | Claude | `71 scoreview-midi-auto-reconnect -feature.md` |
| 72 | Opt-in following of macOS/Windows system appearance. | Claude | `72 follow-system-appearance -feature.md` |

Gemini's interactive theme customizer is a subset of the ice-boxed configuration GUI, and toast history overlaps the ice-boxed integrated log viewer. Pane drag/reorder remains ice-boxed; workspace-tab reordering is accepted because `App::move_workspace()` already provides the operation and the new work is a contained Chrome gesture. The proposed terminal scrollbar click policy is not filed because terminal panes currently have no scrollbar to configure; it needs a separate product design before it can be an implementation item.

## Interdependencies and recommended execution order

```text
Immediate reliability
  00 microphone race ──────────────┬──> 15 ScoreHost orchestration tests
                                   ├──> 16 worker/device stress
                                   └──> 21 ScoreHost decomposition
  reopen Windows shutdown ────────────> reopen TSan CI
  existing 00 network transport ─────> existing 49 privacy controls
  existing 02 atomic sessions ───────> 17 progress crash safety (shared storage lessons)
  14 nonblocking Score rebuild ──────> 15/16 ──> 21
  14 pane print state machine ───────> 56 export pane PNG

Safety nets before structural moves
  existing 15 Apple attach tests ────> existing 25 session split
  existing 17 App rollback tests ────> existing 22 workspace/session controllers
  existing 18 SatView smoke ─────────> existing 26 SatView split
  existing 19 shader ABI ────────────> existing 27/28 renderer refactors
  15 + 16 ScoreView tests ───────────> 21 ScoreHost split
  18 hostile MXL ────────────────────> 52 type-aware drop + 64 piece library
  19 slicer corpus ──────────────────> 65 loops + 67 hand practice

Application feature foundations
  existing 12 provider availability ─> 52 type-aware drop + 69 split-with-host
  existing 13 workspace invariant ───> existing 22 controller
  existing 22 controller ────────────> 50/51/55/57/59/61
  existing 23 Chrome split ──────────> 59/61/72
  existing 07 then 21 config schema ─> 62 validate-config + persisted feature settings
  existing 10 callback lifetime ─────> 60 pinch + 72 system appearance

ScoreView feature foundations
  17 durable progress ───────────────> 64 piece library + 68 recap
  21 ScoreHost decomposition ────────> 64-71 (separate session/stream/audio/presentation owners)
  65 practice loop ──────────────────> 66 count-in and 67 hand practice can remain independent
  70 output-device ownership ────────> 71 MIDI reconnect only at the shared AudioController seam
```

Additional coordination rules:

- Do not assign two agents concurrently to `app/app.cpp`, `app/chrome_host.cpp`, `app/main.cpp`, `score_host.cpp`, `satview_host.cpp`, or a platform renderer. Give one agent integration ownership and delegate lower-library/test seams.
- Fix the microphone and rebuild state machines before extracting ScoreView controllers; otherwise the refactor will fossilize bad lifetime semantics.
- Add ScoreHost tests before the ScoreHost split. A test agent can build the fixture while the bug owner works in lower-level injected seams, but both should avoid editing `score_host.cpp` simultaneously.
- Complete provider metadata before file-drop/host-launcher work so those features do not add another hardcoded host list.
- Complete workspace invariants/controller extraction before workspace features. This creates a stable API that separate feature agents can consume.
- Keep the pane-print bug card focused on correct capture/print lifecycle. PNG export is a separate user feature built on the resulting capture controller.
- Let the active SatView text-atlas plan finish before changing the same atlas types through GPU/foundation refactors.

## Where sub-agents make sense

- A **ScoreView reliability lane** can split into a lower-level mic/engraver owner and a test-fixture owner, followed by one ScoreHost integration owner.
- A **session/process lane** can reopen Windows shutdown/TSan while another agent handles existing atomic persistence and Apple attach tests; `main.cpp` still needs one integration owner.
- A **rendering lane** can isolate shader ABI/tests from platform resource fixes, but Vulkan and Metal changes for one feature must share a single design contract.
- A **tooling/docs lane** can repair tracker/docs/test-target wiring independently of runtime fixes.
- After `WorkspaceController` exists, pane move, workspace clone, layout templates, locks, and labels are good separate-agent tasks because they can target the controller API instead of colliding in `App`.
- After `ScoreAudioController` exists, output-device and MIDI reconnect work can be separated; before then they both collide in `score_host.cpp`.

## Filing decision

The prior consensus already created priorities 00-49 in `kanban/pending`; those files are preserved and referenced rather than regenerated. New files reuse the appropriate category bands so lexical priority remains bugs, tests, refactors, features. Duplicate priority numbers indicate the same urgency band and avoid renaming fifty active cards merely to insert new findings.

<model>GPT-5 Codex</model>
