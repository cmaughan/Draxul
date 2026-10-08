# Route zoomed input to the visible pane
**Summary:** Keep mouse input on the visible zoomed pane so typing cannot move to a hidden pane.

**Priority:** P1 — Hidden panes must not receive input intended for the zoomed pane.
**Source:** `app/pane_manager.cpp`
**Reported by:** Claude H2; consensus F23.

**Evidence and trigger:** Lines 1193–1197 hit-test normal split rectangles and update focus while zoomed. Mouse movement can select a hidden leaf; hidden divider hits also capture input.

**Related:** `kanban/pending/78 zoomed-pane-print-crop -bug.md`.

- [x] **Investigate:** Trace zoomed leaf identity, mouse movement/buttons/wheels, focus, and divider hit-testing.
  - Confirmed on 2026-10-08: `InputDispatcher` routes every mouse move, button, and
    wheel through `PaneManager::host_at_point()`, which hit-tested the hidden split
    rectangles and called `update_focus()`. `divider_at_point()` also hit-tested the
    hidden split, so hover changed the cursor and a press captured a drag (the ratio
    update itself was already guarded). Separately, `focus_direction()` and other
    `set_focused()` callers could focus a hidden leaf while zoomed, sending typing to it.
- [x] **Fix:** Resolve zoomed input against displayed geometry and suppress hidden dividers.
  - `host_at_point()` returns the zoomed host (which covers the whole content area)
    without consulting split geometry; `divider_at_point()` returns nothing while
    zoomed; `update_focus()` carries the zoom to an explicitly focused leaf so the
    focused pane is always the visible one; restore aligns stale checkpoint focus with
    the restored zoomed leaf.
- [x] **Acceptance:** Moving or clicking across a zoomed pane never focuses hidden panes or captures hidden dividers.
- [x] **Acceptance:** Unzooming restores normal split hit-testing and intended focus.
  - Covered by `zoomed pane owns pointer and keyboard input over hidden split geometry`
    in `tests/input_dispatcher_routing_tests.cpp`, which drives real window events
    through `InputDispatcher` into a real `PaneManager`.
- [x] **Validation:** Run core aggregate tests and same-cache smoke; check zoomed input interactively.
  - macOS Debug `do.py test debug` (56 CTest entries) passed apart from three load-sensitive flakes that pass in isolation or also fail on a pre-change binary, plus the known `draxul-do-py-tests` megacity AGENTS.md failure in submodule-less worktrees; `do.py smoke debug --skip-build` passed (2026-10-08). The interactive
    check was outstanding in that headless run and is now completed by the Windows
    native-input evidence below.

**Shared with `kanban/pending/78 zoomed-pane-print-crop -bug.md`:** both bugs come
from callers reading the stored split rectangle instead of the displayed zoomed
viewport. The displayed geometry is `{0, 0, zoom_pixel_w_, zoom_pixel_h_}` passed
through `compute_viewport` for `zoomed_leaf()`; 78 should snapshot that same viewport
(or the zoomed host's current viewport) rather than `tree().descriptor_for(leaf)`.

## Windows gate preparation (2026-10-08)

- Await the parent's exclusive GPU slot; no client or build was started.
- Use two fresh server-terminal panes in one isolated Session. Discover their
  stable IDs through layout aliases and focus the left pane through its exact UI
  route. Toggle zoom with real native `Ctrl+S, Z` input: `pane action` dispatches
  host actions, not the application's `toggle_zoom` GUI action.
- Move/click/wheel across both formerly split halves and drag across the old
  divider. After each operation, type a unique marker through native window
  input, then read both panes via CLI and require the marker only in the visible
  pane. CLI `pane send`/`pane keys` cannot prove this input-routing gate because
  they address server terminals directly. Require unchanged hidden split ratios
  and no resize cursor/capture over the old divider.
- Unzoom through native input and verify separate pointer/keyboard markers reach
  each intended pane again. Guard every input batch with exact-PID/HWND liveness,
  foreground ownership, and attached UI-route assertions; never send input after
  a focus/identity check fails. Keep native screenshots, CLI responses, client
  logs, and the final exit status. Core aggregate/same-cache smoke remain parent
  validation responsibilities; live acceptance is not yet claimed.

## Windows native input result (2026-10-08)

- [x] Live interactive-input gate: exact client PID `48880`, HWND `1839388`,
  route `ui-5c496e9c90769078f460ba2f`, isolated Session `gate`; native
  `SendInput` toggled zoom with `Ctrl+S, Z`. Markers `z87a` through `z87e`
  reached only visible `pane-5` after left/right motion, right-half click,
  wheel, and a drag across the former divider. Hidden `pane-8` had none.
  Server split descriptors/ratios were unchanged. After native unzoom,
  `z87f` reached only right `pane-8`, and `z87g` only left `pane-5`.
- `build-ninja-debug/windows-gates/core-20261008/dgoqycwz_u/zoom-result.json`
  stores all seven successful assertions. `zoom-native-passed.bmp` and
  `unzoom-native-passed.bmp` show actual terminal rendering; `events.jsonl`
  preserves native events, both terminal reads, and liveness assertions.
  Cursor shape was not independently measured; hidden-divider coverage here
  is the native drag, unchanged geometry, and subsequent keyboard routing.
- One initial five-second shell-output wait timed out under load; its full
  command subsequently completed in the intended pane. The successful bounded
  retry uses short native keyboard markers and an 18-second per-marker limit,
  not CLI terminal injection. No failure was treated as a pass. Native focus
  acquisition later required `AttachThreadInput` for another scenario; input
  was refused whenever foreground ownership could not be established.
- Parent reports app/input test sharding and five core snapshots passed, and
  an isolated same-cache Debug smoke passed. Its aggregate is **73/79, not
  clean**, with separate root/font/scope/timing/personal/shutdown follow-ups.
  Do not infer a clean aggregate from this successful live input gate.
- Normal owned-client exit was 0; its exact isolated server is stopped.
  The same client later exercised card 85, whose descriptor errors are recorded
  independently and are not evidence of an input-routing failure.

## Closure (2026-10-08)

- Parent explicitly approved closure based on all seven native-input checks,
  the passed initial core/app aggregate, and the passed isolated same-cache
  Debug smoke. The broader 73/79 run is not globally clean; its unrelated
  failures remain separately owned. Parent's final Release startup passed:
  `py do.py smoke release --skip-build` exited 0 in 33.866647 seconds including
  toolchain setup; log: `windows-gates/final-release-smoke-retry.log`.
  The initial attempt failed toolchain initialization after 68.059 seconds due
  to system resource exhaustion; it was an environment failure, not a passing
  startup check.
- Stable evidence lives in
  `build-ninja-debug/windows-gates/core-20261008/dgoqycwz_u/`; the parent copied
  top-level logs, JSON/JSONL, and captures without runtime authentication files.
