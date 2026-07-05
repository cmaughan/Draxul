# Draxul Review Consensus

**Date:** 2026-07-05
**Reviewed snapshot:** `codex/satview-ground-projections` at `7da5dc48`, including the current uncommitted SatView/text-atlas work
**Inputs:** `review-latest.gpt.md`, `review-latest.claude.md`, and `review-latest.gemini.md`

## Who was in the room

- **[GPT/Codex]** supplied the broad repository review, the most explicit current-tree reconciliation, and the security/lifecycle-first ordering.
- **[Claude/Sonnet 4.6]** supplied the most detailed duplication and merge-hotspot inventory and argued for cheap documentation/hygiene repairs before large refactors.
- **[Gemini/Antigravity 3.5 Flash]** independently confirmed the two critical launch/network bugs and emphasized lifetime, persistence, and end-user failure reporting.
- **[Consensus/GPT-5 Codex]** re-read the three reports, checked the claims against the moving working tree, `docs/features.md`, the active SatView plans, and the canonical `kanban/done` and `kanban/ice-box` inventories.

The three reviewers agree much more than they disagree. Draxul has strong low-level layering, unusually broad tests, disciplined Neovim/RPC code, and genuinely cross-platform renderer intent. Its current risk is concentrated in integration seams that grew after earlier cleanup waves: app/session orchestration, shell-based network access, snapshot registration, large product hosts, and manually mirrored GPU contracts.

## The room's shared conclusion

The next wave should be a reliability wave, not a speculative rewrite:

1. Remove command-shell network execution and make shutdown cancellable.
2. Fix the two new macOS app self-launch paths and the remaining Metal/NanoVG correctness defects.
3. Make persistence and validation truthful: atomic sessions, an enforced render manifest, and restored macOS attach coverage.
4. Repair small but real correctness gaps in config reload, Unicode overlays, process handles, platform callbacks, and provider availability.
5. Put focused tests around those seams.
6. Only then split the large integration classes along the responsibility boundaries already visible in the code.

Claude's preference to fix docs and hygiene first is sensible for low-risk parallel work, but GPT and Gemini are right that the network, launch, GPU, and persistence defects must lead the critical path. These are sequencing differences, not disagreements about the findings.

## Current findings and decisions

### Bugs and reliability

| Priority | Finding | Agents | Current-tree decision |
|---:|---|---|---|
| 00 | Weather turns `weather_location` into a shell command; SatView has two more `popen("curl")` transports and all three have weak cancellation. | GPT, Claude, Gemini | **Unanimous and confirmed.** One shared, injected, cancellable transport should replace all three. Card `00 network-shell-transport -bug.md`. |
| 01 | `app/main.cpp` and `app/session_picker_host.cpp` allocate C++ objects after `fork()` on macOS. | GPT, Gemini; Claude also identified it in the recommended reliability pass | **Confirmed.** This is a new app-level variant, not the already-completed lower-level Neovim/PTY card. Card `01 macos-app-self-launch -bug.md`. |
| 02 | Session topology and metadata overwrite final TOML files with `std::ios::trunc`. | GPT, Gemini | **Confirmed.** Card `02 atomic-session-persistence -bug.md`. |
| 03 | Render scenarios are split across CMake, `do.py`, files, references, and docs; missing required files are silently skipped. | GPT, Claude, Gemini | **Unanimous and confirmed.** Card `03 render-scenario-manifest -bug.md`. This repairs the system rather than duplicating the completed wide-character scenario card. |
| 04 | NanoVG Vulkan samples textures without complete layout/visibility handling, maps memory without a proven flush policy, and hardcodes three frames in flight. | Claude, supported by GPT's wider GPU-contract concern | **Confirmed by source inspection.** Card `04 nanovg-vulkan-resource-sync -bug.md`. |
| 05 | Metal grid upload now retries allocation correctly, but `draw_grid_handle_now()` still draws the new instance count after a failed growth upload, using the previous smaller buffer. | Claude, GPT | **Confirmed residual bug.** The completed card fixed permanent darkness, not this current-frame out-of-bounds risk. Card `05 metal-grid-upload-abort -bug.md`. |
| 06 | `grid.metallib` does not depend on `quad_offsets_shared.h`. | Claude; GPT noted incomplete shader dependency wiring | **Confirmed.** Card `06 metal-shader-dependency-closure -bug.md`. |
| 07 | Config parsing/merging has hand-synchronized key lists, duplicate terminal-key parsing, incomplete chrome merge behavior, and external services do not participate in reload. | GPT, Claude; Gemini asked for transactional reload | **Agreement on the defect, different scope.** Land narrow correctness fixes first in `07 config-schema-reload-correctness -bug.md`; follow with the declarative refactor in item 21. |
| 08 | Palette, toast, and tooltip rendering treat UTF-8 bytes as clusters. | Claude; consistent with GPT's Unicode risk assessment | **Confirmed in current sources.** Card `08 gui-unicode-cell-rendering -bug.md`. |
| 09 | Windows `NvimProcess::is_running()` can read a process handle while `shutdown()` closes it. | Claude | **Plausible and confirmed by the unsynchronized handle access.** Card `09 windows-nvim-process-handle-race -bug.md`. |
| 10 | Windows tray and macOS Dock-reopen glue retain raw pointers to window-owned callback state. | Claude; Gemini raised the broader lifetime theme | **Confirmed.** Card `10 platform-window-callback-lifetime -bug.md`. |
| 11 | FreeType/render failures are reported as `AtlasOverflow`, activating the wrong recovery policy. | Claude | **Confirmed.** Card `11 glyph-raster-error-taxonomy -bug.md`. |
| 12 | The command palette hardcodes optional host kinds separately from `HostProviderRegistry`, so optional-off builds can advertise impossible actions. | GPT; Claude identified bottom-layer `HostKind` churn | **Confirmed.** Card `12 host-provider-availability -bug.md`. |
| 13 | `App::active_host_manager()` returns a mutable process-static dummy when the active workspace invariant is broken. | Claude | **Confirmed.** Card `13 active-workspace-invariant -bug.md`. |
| 14 | Unattended review helpers promise read-only behavior in prompts but grant write-capable/full-auto permissions. | GPT | **Confirmed and distinct from the iceboxed script-deduplication work.** Card `14 review-automation-read-only -bug.md`. |

Related observations are grouped into the owning cards rather than multiplied into tiny plans: item 04 also pins NanoVG's floating dependency and reduces duplicated pipeline setup only where needed; item 05 includes the Metal pane-scissor underflow/parity fix; item 08 removes the remaining grid/Kanban Unicode-decoder duplication while establishing the overlay helper; items 26-28 include duplicated asset/ImGui/resource plumbing; item 34 includes a generated tracker-status/index check for ambiguous unchecked boxes under `kanban/done`.

### Validation gaps

| Priority | Gap | Agents | Decision |
|---:|---|---|---|
| 15 | The entire 631-line session attach suite is excluded on Apple. | GPT, Claude, Gemini | **Unanimous.** Restore it before splitting session attach. Card `15 macos-session-attach-coverage -test.md`. |
| 16 | Snapshot diff/finalization math is itself not directly tested. | Claude | **Accepted.** Card `16 render-comparison-core -test.md`. |
| 17 | `App` session save/load rollback paths lack focused fault coverage. | Claude | **Accepted as a safety net for App decomposition.** Card `17 app-session-rollback -test.md`. |
| 18 | SatView's fast-changing host has no direct construct/pump/draw/config smoke fixture. | Claude; GPT noted the same product-host risk | **Accepted after the current observatory work settles.** Card `18 satview-host-smoke -test.md`. |
| 19 | CPU/GLSL/Metal resource contracts have no mechanical ABI/binding parity check. | GPT, Claude | **Strong agreement.** Card `19 shader-abi-parity -test.md`. |
| 20 | Overlay allocation failure behavior is only partially covered. | GPT | **Accepted without duplicating existing ToastHost/grid-null tests.** The card covers the remaining palette/chrome/diagnostics matrix: `20 overlay-allocation-failure -test.md`. |

Network hostile input, bounded shutdown, atomic-write fault injection, post-fork regression, provider availability, weather reload, and Windows process-race tests live in their owning bug cards. Keeping those tests with the fix prevents a second card from drifting away from the contract it is meant to prove.

### Refactors the room supports after stabilization

| Priority | Refactor | Agents | Consensus boundary |
|---:|---|---|---|
| 21 | Declarative config schema | Claude, GPT; Gemini supports transactional config | One schema should drive parse, validation, serialization, known keys, and docs. Start only after item 07. |
| 22 | Extract workspace and session controllers from `App` | GPT, Claude, Gemini | This is a **new follow-on** to the completed extraction of input, GUI actions, and host management; do not recreate those classes. |
| 23 | Split Chrome layout, vector drawing, text, and rename editing | GPT, Gemini | First extract a pure `ChromeLayout`; preserve behavior and add no new chrome feature during the split. |
| 24 | Extract session CLI and owner launcher from `main.cpp` | GPT, Claude | Do after the app-level spawn bug so the extracted abstraction starts safe. |
| 25 | Split session attach into shared protocol plus Windows/POSIX implementations | GPT, Claude | Restore Apple tests first; preserve wire compatibility. |
| 26 | Split SatView into core/services/scene/host/renderer targets | GPT, Claude | Wait for the current observatory/text-atlas work; keep Vulkan and Metal consuming one scene model. |
| 27 | Decompose MegaCity host and renderer by responsibility/pass | GPT, Claude | Follow `modules/megacity/AGENTS.md`; keep optional-off isolation and both backends valid. |
| 28 | Expand shared GPU resource helpers | Claude | Extract only proven repeated Vulkan/Metal resource operations; do not leak backend types into public APIs. |
| 29 | Clarify `draxul-gui` versus `draxul-ui` contracts and simplify `UiPanel` lifecycle | Claude | Document the boundary first, rename only when migration cost is justified. |
| 30 | Replace parallel font-style fields/caches with a `FontStyle` indexed model | Claude | Pure internal refactor with resolver parity tests. |
| 31 | Reduce foundation-layer churn | Claude, GPT | Partition `draxul-types` responsibilities and move product availability out of the bottom layer without reintroducing cycles. |
| 32 | Share the repeated `DX*` binary catalog container | Claude | Preserve each magic/version and generated asset determinism; share framing/validation, not semantic records. |
| 33 | Audit `PERF_MEASURE()` placement | Claude | Keep high-value spans; remove mutex-distorting trivial probes with before/after evidence. |
| 34 | Repository hygiene and one feature-doc source | Claude | Remove or relocate tracked artifacts only after provenance checks; make root `FEATURES.md` a pointer or delete it. |
| 35 | Split the monolithic test executable into module-aware CTest targets | GPT | Preserve shared fakes and tags; make optional-module coverage explicit and parallelizable. |

The room does **not** create new cards for the broad renderer-base/input/IHost refactors: those already exist in completed or iceboxed work. If their acceptance criteria are no longer true, reopen the original card rather than filing a synonym.

### Accepted future features

These ideas survived the current feature check and are not in `kanban/done` or `kanban/ice-box`:

| Priority | Feature | Raised by | Card |
|---:|---|---|---|
| 36 | Promote render references from the other platform's CI artifact | Claude | `36 cross-platform-render-reference-promotion -feature.md` |
| 37 | Click-to-dismiss/actionable toasts | Claude | `37 interactive-toasts -feature.md` |
| 38 | `do.py new-host` / `new-module` scaffolding | Claude | `38 host-module-scaffolding -feature.md` |
| 39 | Named SatView camera/time/selection bookmarks | Claude | `39 satview-view-bookmarks -feature.md` |
| 40 | Incremental crash-recovery session journal | GPT | `40 crash-recovery-session-journal -feature.md` |
| 41 | `--safe-mode` startup and recovery prompt | GPT | `41 safe-mode-startup -feature.md` |
| 42 | Global session/workspace/pane switcher | GPT | `42 global-session-workspace-switcher -feature.md` |
| 43 | Detach/rejoin a pane or workspace as an OS window | GPT | `43 detachable-pane-workspace-windows -feature.md` |
| 44 | Busy-process close guard | GPT | `44 busy-process-close-guard -feature.md` |
| 45 | First-run health center | GPT | `45 first-run-health-center -feature.md` |
| 46 | Accessibility mode | GPT | `46 accessibility-mode -feature.md` |
| 47 | One bounded terminal graphics protocol | GPT | `47 terminal-graphics-protocol -feature.md` |
| 48 | Versioned portable profile export/import | GPT | `48 portable-profile-bundle -feature.md` |
| 49 | Network/offline/privacy/cache controls | GPT | `49 network-privacy-controls -feature.md` |

These are intentionally behind the reliability and refactor wave. A good idea is not automatically the next idea.

## Reconciled exclusions: do not create duplicates

### Implemented already

- File drag-and-drop is implemented and documented (`docs/features.md`, `InputDispatcher`, `open_file:` dispatch, and tests).
- Duplicate pane in the same working directory is implemented (`duplicate_pane`).
- Terminal OSC 0/2 titles and OSC 7 directory titles are implemented and tested.
- Shared test fakes, `Result<T, Error>`, keybinding conflict detection, and malformed-msgpack fuzzing already have completed cards/code.

### Deliberately iceboxed

- Split/close stress, atlas exhaustion/dynamic growth, config reload under activity, null input dependencies, host lifecycle state-machine tests, renderer parity cleanup, dirty-range coalescing, RPC timeout UI, font fallback inspector, configuration GUI, and cross-cutting agent-script deduplication remain in `kanban/ice-box`.
- Gemini's concurrent grid reader/writer test is rejected: Draxul's contract mutates and renders the grid on the main thread; the reader thread only queues decoded messages.

### Active work already owns the finding

- The current observatory/boundary/text-atlas plan owns atlas bounds, constellation boundaries/labels, shared rasterization, and SatView optional-off validation. No duplicate cards were filed.
- The current lunar-orbit plan owns central-body-aware SatView catalog/ephemeris work.

### Existing completed cards to reopen if the project wants the regression fixed

No new card is created for these because the requested rule forbids duplicates:

- `kanban/done/25 ci-pipeline-definition-feature.md`: its push/PR acceptance criteria are false; every workflow is currently manual-only.
- `kanban/done/19 render-test-extraction-refactor.md`: production still links/calls render-test code and registers the NanoVG demo.
- `kanban/done/111 docs-design-stale-repair -refactor.md` and `kanban/done/12 claude-md-documentation-errors -bug.md`: `AGENTS.md`, `docs/module-map.md`, and `plans/design/renderers.md` again describe removed host/renderer hierarchies.
- `kanban/done/01 stale-docs-navigation-paths-bug.md`: `plans/README.md` and `scripts/sync_project_board.py` still point at deleted `plans/work-items*`; the synchronizer also paginates only the first 100 project items.
- `kanban/done/14 test-module-boundary-violations -refactor.md`: current tests again include module-private `src/` headers.
- `kanban/done/19 test-harness-unification -refactor.md`: `tests/do_py_tests.py` remains unwired and the Windows/Unix runner behavior has drifted.

The CI and architecture-doc regressions should be reopened alongside items 00-03, even though their original cards retain old numbering.

### Not promoted without more evidence

- Gemini's generic raw-pointer warning is too broad as stated; existing host lifetime cards and owner-lifetime tokens already cover known cases. Item 10 targets two concrete surviving globals.
- Gemini's malformed-hex fallback preference is a product-policy choice, not a demonstrated bug.
- Claude's dead dirty-upload path is covered by the iceboxed dirty-range work; decide there whether to wire or remove it.
- NanoVG's pipeline boilerplate can be reduced while fixing item 04, but it does not need an independent card.

## Interdependencies and safe execution order

```text
Reliability roots
  00 network transport ───────┬──> 49 privacy controls
                              └──> 45 health center
  01 safe self-launch ───────────> 24 session CLI launcher
  02 atomic sessions ────────────> 40 crash journal ──> 48 profile bundle
  03 render manifest ────────────> 36 cross-platform reference promotion
  04 NanoVG sync ────────────────> 28 shared GPU helpers
  06 shader dependencies ────────> 19 shader ABI parity
  07 config correctness ─────────> 21 declarative schema ──> 41/45/46/48/49
  08 Unicode overlays ───────────> 23 Chrome split and 29 GUI/UI cleanup
  10 callback lifetime ──────────> 43 multi-window detach/rejoin
  12 provider availability ──────> 31 foundation cleanup and 38 scaffolding

Safety nets before large moves
  15 Apple attach tests ─────────> 25 session-attach split
  17 App rollback tests ─────────> 22 workspace/session controllers
  18 SatView host smoke ─────────> 26 SatView split ──> 39 bookmarks
  19 shader ABI parity ──────────> 27 MegaCity split and renderer changes
  20 overlay failure tests ──────> 23 Chrome split

Session/product features
  22 + 24 + 25 ─────────────────> 42 global switcher
  22 + stable window ownership ─> 43 detachable windows
  22 + provider metadata ───────> 44 busy-process close guard
```

Additional sequencing rules:

- Let the current SatView/text-atlas work finish before items 08, 18, 26, 32, or 39 touch the same files.
- Fix item 04 before extracting shared GPU helpers; otherwise the bad behavior may be generalized.
- Keep item 21 separate from item 07 so the correctness patch remains reviewable.
- `WorkspaceController` can be developed separately from session CLI/attach work, but `SessionController` should wait for items 24 and 25 to stabilize ownership boundaries.
- Item 43 is an architectural feature, not a quick SDL second-window patch; it follows explicit window/callback ownership.

## Where sub-agents make sense

After the overlapping SatView work lands, independent agents can safely own these lanes:

- **Network lane:** 00, then 49.
- **Session lane:** 01, 02, 15-17, then 24-25 and 40-42.
- **Rendering lane:** 03-06, 16, 19-20, then 28 and 36.
- **Application UI lane:** 08, 12-13, then 22-23, 29, and 37.
- **Product-module lane:** 18/26/39 for SatView; 19/27 for MegaCity.
- **Tooling/docs lane:** 14, 34-35, 38, plus reopening the CI/docs/tracker cards.

Do not assign two agents concurrently to `app/app.cpp`, `app/main.cpp`, `app/chrome_host.cpp`, `modules/satview/draxul-satview/src/satview_host.cpp`, or either large product renderer. Parallelize around stable interfaces, not inside the same merge hotspot.

## Work-item filing decision

The old prompt names `plans/work-items*`, but those directories were removed. The live repository tracker is `kanban/pending`, `kanban/ice-box`, and `kanban/done`; therefore the new implementation plans are filed under `kanban/pending/`. Their numeric prefixes preserve the requested order: bugs, tests, refactors, then features.

<model>GPT-5 Codex</model>
