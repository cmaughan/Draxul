Here is the full review report:

---

# Draxul Codebase Review

*Review date: 2026-03-29. Source examined: `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, `plans/work-items/`, `docs/features.md`. Already-tracked work items and completed/iced items are noted inline but not counted as new findings.*

---

## Code Smells

### 1. Duplicated `param_or` Lambda in CSI Dispatch
`libs/draxul-host/src/terminal_host_base_csi.cpp` — the same `param_or` lambda is independently defined inside `csi_cursor_move`, `csi_erase`, `csi_scroll`, `csi_insert_delete`, and `csi_margins`. Classic extract-to-free-function candidate; each copy can drift independently.

### 2. `(void)pixel_h` Suppression Pattern
`app/app.cpp` — structured bindings like `auto [pixel_w, pixel_h] = window_->size_pixels(); (void)pixel_h;` appear multiple times. The window API forces callers to destructure both dimensions even when only one is needed. A `width_pixels()` / `height_pixels()` accessor pair would remove the noise.

### 3. MegaCityHost as God Object
`libs/draxul-megacity/include/draxul/megacity_host.h` — ~50 member variables spanning camera state, grid occupancy, route threading, hover tooltip, selection opacity, sign label atlas, tree meshes, semantic model, layout state, config documents, and UI panel flags. No single person can hold this invariant space in their head.

### 4. `App` as God Orchestrator
`app/app.cpp` (~867 lines) wires window, renderer, text service, UI panel, host manager, input dispatcher, and GUI action handler with no injection boundary of its own. Every other major subsystem has been given a `Deps` struct; `App` has not, making it untestable in isolation.

### 5. `static_cast<I3DRenderer*>` Downcast in HostManager
`app/host_manager.cpp:283` — `static_cast<I3DRenderer*>(deps_.grid_renderer)` assumes `IGridRenderer` inherits from `I3DRenderer`. This compiles silently if the hierarchy ever changes but becomes UB. A `dynamic_cast` with a null check, or a typed accessor, is the correct tool here.

### 6. `dynamic_cast` Capability Probing
`host_manager.cpp` uses `dynamic_cast<MegaCityHost*>` (lines 241, 274) and `dynamic_cast<I3DHost*>` (line 280) to detect host capabilities post-construction. This is a code smell indicating the creation path does not carry enough type information. A factory that returns tagged handles, or a `bool supports_3d()` query on `IHost`, would make the capability explicit without coupling to concrete types.

### 7. Four HostContext Constructor Overloads
`libs/draxul-host/include/draxul/host.h:78–117` — four positional constructors for a struct that has grown past the point where positional construction is clear. A named-field builder or designated initializers would be more readable.

### 8. `std::function` in Character-Frequency Paths
The VT parser, alt screen manager, and selection manager wire callbacks via `std::function` + `std::bind_front`. These are invoked per-character (VT parser) or per-cell (alt screen remap). The type-erasure heap allocation is unnecessary since the concrete types are known at construction; a template parameter or direct virtual call would avoid the overhead without sacrificing the decoupling.

### 9. Notification Queue Drop with No Backpressure
`libs/draxul-nvim/src/rpc.cpp` — the notification queue warns at 512 entries and silently drops after 4096. If the main thread stalls (font rebuild, long atlas reset), nvim output is lost. There is no backpressure signal to nvim and no persistent log of dropped notifications for diagnostics.

### 10. Two Capability Detection Patterns in the Same Codebase
`RendererBundle` uses SFINAE-based `reset<T>` to detect `IImGuiHost` / `ICaptureRenderer`, while `host_manager.cpp` uses raw `dynamic_cast`. Two separate capability detection idioms create confusion about which to follow for new code.

---

## Testing Holes

### 1. App Class Has No Unit Tests
`app/app.cpp` — initialization rollback, `pump_once` frame gating, and shutdown ordering are exercised only by end-to-end smoke/render tests, not isolated unit tests. Adding a `Deps`-injected `App` test fixture would close this gap.

### 2. InputDispatcher Chord State Machine
`app/input_dispatcher.cpp` — the tmux-style chord prefix state machine (partial chord cancellation, timeout behavior, modifier key interactions during an open chord) has no unit tests. Chord handling is a common source of regression in terminal GUIs.

### 3. SplitTree Edge Cases
`app/split_tree.cpp` — deep nesting, divider drag ratio clamping, and hit-test boundary conditions on divider edges are not directly tested. `host_manager_tests.cpp` tests HostManager but not the underlying tree operations at their limits.

### 4. VT Parser Buffer Overflow Paths
`terminal_vt_tests.cpp` covers standard CSI sequences well, but the 64 KB plain text buffer, 4 KB CSI buffer, and 8 KB OSC buffer overflow/truncation paths are not tested. Malformed or truncated escape sequences at buffer boundaries are a common source of terminal bugs.

### 5. Grid Scroll with Double-Width Cell Fixup
`grid.cpp scroll()` has special handling for double-width cells at row boundaries. This fixup logic is not directly tested — only the basic displacement is covered in `grid_tests.cpp`.

### 6. Renderer State Dirty Range Tracking
`renderer_state.cpp` tracks dirty ranges for incremental GPU upload. The coalescing behavior when overlay regions interact, and the impact of cursor save/restore on dirty ranges, are untested.

### 7. GridRenderingPipeline Atlas Reset Retry
`grid_rendering_pipeline.cpp flush()` has an atlas-reset retry loop (clears atlas and retries if it fills during a single frame). This recovery path has no test coverage.

### 8. GlyphCache Face-Lifetime Hazard Regression
*(Partially tracked as work item 00)* — there is no regression test that verifies GlyphCache rejects or gracefully handles a face pointer that has been invalidated by FontManager destroying its face. Without a test, a future refactor of face management could reintroduce this silently.

### 9. Config Missing/Malformed Path *(tracked as work item 07)*
`ConfigDocument` handles TOML parsing errors, merges defaults, and writes back values. Missing-key, malformed-value, and I/O-error paths are not unit-tested; only the happy path is exercised by the smoke test.

### 10. Alt Screen Snapshot Save/Restore Across Scrollback Wrap *(tracked as work items 06/08)*
Alt screen switching saves and restores cell state, and the scrollback ring wraps at 2000 rows. The interaction — switching to alt screen when the scrollback ring is mid-wrap, then restoring — is not tested. Combined state like this is a common source of visual corruption in terminal emulators.

---

## Maintainability Issues

### 1. `CellText` 32-Byte Inline Buffer with Silent Truncation
`libs/draxul-grid/include/draxul/grid.h` — UTF-8 sequences longer than 31 bytes (multi-codepoint emoji like family sequences can reach 25+ bytes) are silently truncated. The truncation can produce invalid UTF-8 at the cut point. The TODO comment acknowledges this but there is no diagnostic, fallback glyph, or compile-time explanation of why 32 bytes was chosen.

### 2. Raw `FT_Face` Pointer as Hash Key *(tracked as work item 00)*
`libs/draxul-font/src/font_engine.h` — `GlyphCache` uses raw `FT_Face` pointers as part of `ClusterKey`. If FontManager destroys and reallocates a face, the hash key silently becomes invalid, and a new face at the same address would collide with the old entries.

### 3. `try_get_int` Narrowing Cast *(tracked as work item 03)*
`libs/draxul-nvim/src/ui_events.cpp` — `int64_t → int` without range checking. For grid coordinates in practice this does not overflow, but the silent narrowing is a maintenance trap for anyone who refactors the type of the grid dimension fields.

### 4. 5-Second Hard-Coded RPC Timeout
`libs/draxul-nvim/src/rpc.cpp` — the request timeout is a magic number with no named constant and no documentation of why five seconds was chosen. On slow machines or under nvim doing a large indexing operation, this produces spurious timeouts that are hard to distinguish from genuine hangs.

### 5. Magic Overlay Capacity of 256 in RendererState
`renderer_state.h` — 256 overlay cells is not explained. If a host ever emits more than 256 overlay regions in a single frame, the excess is silently ignored. A static_assert with a comment explaining the limit would at least make violations visible at compile time.

### 6. O(n) Recursive `std::function` Traversal in SplitTree
`app/split_tree.cpp` — `find_leaf_node`, `find_parent_of`, and `find_divider_node` all use recursive `std::function` lambdas for O(n) tree traversal. At current pane counts (<10) this is harmless, but the `std::function` indirection makes the code harder to follow in a debugger and the traversal is not short-circuit-capable.

### 7. Font Pipeline Atlas Reset Blocks Main Thread
`gui_action_handler.cpp change_font_size()` triggers `text_service->set_point_size()` which resets the entire atlas and all caches synchronously on the main thread. For a warm atlas (e.g., after rendering a large file), this can cause a frame stall long enough for nvim notifications to pile up to the 4096-entry drop threshold.

### 8. No Compile-Time Shader/CPU Struct Parity Check
The 112-byte `GpuCell` is verified by `static_assert` in C++, but nothing verifies that the Metal and GLSL shader struct layouts match the C++ layout at build time. A divergence would produce silent visual corruption rather than a build error.

### 9. Platform Shader Parity Not Enforced
29 shader files split between GLSL and Metal. There is no mechanism to verify the two backends produce equivalent output for the same input. The render test suite compares against platform-specific reference images, so Metal and Vulkan could diverge on a code path not exercised by an existing render test scenario.

### 10. IHost Interface Is Too Wide
`libs/draxul-host/include/draxul/host.h` — `IHost` has 16 virtual methods, 7 with default no-ops. Methods like `on_font_metrics_changed()` are irrelevant to `MegaCityHost` (which overrides it with an empty body and an explanatory comment). Splitting into `IHost` (lifecycle + input) and `IGridCapable` (font metrics, grid-specific) would reduce the no-op override noise and make the interface contract clearer.

---

## Architectural Issues

### 1. Two Renderer Capability Detection Patterns
`RendererBundle::reset<T>` uses SFINAE for `IImGuiHost`/`ICaptureRenderer` capability detection. `host_manager.cpp` uses raw `dynamic_cast` for `I3DHost`/`MegaCityHost`. Mixing two idioms in the same codebase creates ambiguity about which pattern new code should follow.

### 2. Circular Dependency Risk: Host ↔ Renderer
The host layer depends on the renderer (`IGridRenderer`, `I3DRenderer`). `HostViewport` is defined in `host.h` (draxul-host), not in draxul-types. If any renderer source file ever needs to include `host.h` directly, a circular link dependency emerges that CMake cannot automatically detect.

### 3. Threading Model Is Implicit
`MainThreadChecker` asserts main-thread access in Grid and NvimRpc, but the threading model is not documented at the header level and there are no Clang thread-safety annotations. Each background worker (NvimRpc reader, UnixPtyProcess reader, MegaCityHost route_thread_, grid_thread_) implements its own mutex+CV+stop_flag pattern. A shared `BackgroundWorker<Request,Result>` template would enforce consistent shutdown semantics and reduce boilerplate.

### 4. `void*` Render Context *(tracked as work item 19)*
Metal and Vulkan renderers pass platform-specific contexts as `void*` in several places. This bypasses the type system entirely and relies on convention for correctness.

### 5. No Abstraction for Background Worker Pattern
NvimRpc, UnixPtyProcess, and MegaCityHost (route_thread_, grid_thread_) all independently implement thread + mutex + CV + stop flag + optional<Result>. A `BackgroundWorker<Request,Result>` template would give consistent shutdown semantics, consistent error handling, and a single place to add instrumentation (e.g., queue depth monitoring).

---

## Top 10 Good Things

1. **Clean library decomposition** — draxul-types / draxul-window / draxul-renderer / draxul-font / draxul-grid / draxul-nvim / draxul-host / draxul-app-support are well-separated with a clear dependency graph. New host types and renderer backends can be added without touching unrelated libraries.

2. **Comprehensive render test infrastructure** — TOML scenario files, platform-suffixed BMP references, pixel-diff with tolerance, bless scripts, and CI integration make rendering regressions detectable. This is unusually thorough for a personal project.

3. **ASan/UBSan CI path** — the `mac-asan` preset and `asan.yml` workflow run the full test suite under AddressSanitizer and UBSan. Combined with the `FT_Face` lifetime work item already tracked, this shows active attention to memory safety.

4. **Host hierarchy with clean capability extension** — `IHost → I3DHost → IGridHost → GridHostBase` allows MegaCityHost to register render passes without the grid stack knowing about 3D. `attach_3d_renderer()` is called once at startup via a single `dynamic_cast`, not repeatedly per frame.

5. **Two-pass instanced GPU draw with no vertex buffers** — background quads and foreground glyphs generated procedurally in shaders, host-visible shared buffer with direct writes. The architecture avoids the staging buffer complexity that most GPU terminal renderers carry.

6. **Replay fixture infrastructure** — `tests/support/replay_fixture.h` with msgpack builders and `grid_line` cell batch helpers makes it possible to write UI-parsing regression tests without launching Neovim. This is exactly the right abstraction for terminal emulator testing.

7. **ConfigDocument with merge/defaults/validation** — `config.toml` parsing, range validation (with WARN fallback), section merging, and write-back are centralized. The `scroll_speed` range check and fallback documented in CLAUDE.md is a good example of defensive config handling.

8. **Chord-style GUI keybindings** — the tmux-inspired prefix binding system (e.g. `ctrl+s, |`) gives power users a familiar mental model and avoids conflicts with Neovim keymaps. It is correctly kept in the Draxul layer only.

9. **SonarCloud + clang-format CI** — static analysis and formatting are enforced in CI, with the pre-commit hook catching format issues before they reach review. The CLAUDE.md instructions to run a single clang-format pass at the end of each work item prevents piecemeal drift.

10. **Explicit startup profiling** — the diagnostics panel shows per-step startup timings. This means performance regressions in initialization are visible to users without running a profiler, which makes it much easier to investigate startup slowdowns.

---

## Top 10 Bad Things

1. **MegaCityHost is a ~50-member god object** that mixes camera state, routing threads, tooltip state, building selection, sign atlas, tree mesh, semantic model, config documents, and UI panel flags in a single class with no extractable subcomponents. It is the single largest maintenance risk in the codebase.

2. **App has no injection boundary** — at ~867 lines it is untestable in isolation. Every other major subsystem has a `Deps` struct; App does not. Adding `AppDeps` and a `Deps`-injected constructor is the highest-leverage refactor not yet tracked.

3. **Silent `CellText` truncation at 31 bytes** can produce invalid UTF-8 for complex emoji. There is no diagnostic, no replacement character fallback, and no comment explaining why 32 bytes was the chosen limit.

4. **Two capability detection patterns** (SFINAE `reset<T>` vs raw `dynamic_cast`) in the same codebase create inconsistency. New contributors copying either pattern will increase the divergence.

5. **Font atlas reset on the main thread** during font size change can stall the frame long enough to hit the nvim notification drop threshold (4096 entries), causing lost output. This is a latent correctness issue, not just a performance issue.

6. **No compile-time shader/CPU struct parity verification** — `GpuCell` layout is verified by `static_assert` in C++ but nothing verifies Metal and GLSL shader structs match. A divergence produces silent visual corruption.

7. **`param_or` lambda duplicated five times** in `terminal_host_base_csi.cpp` — the most straightforward code smell in the repository. Each copy is a divergence risk for future CSI sequence additions.

8. **`static_cast` downcast in HostManager** (`static_cast<I3DRenderer*>(deps_.grid_renderer)`) is a latent UB bomb. At present the hierarchy makes it safe, but it will silently corrupt if the inheritance chain changes.

9. **Five-second hard-coded RPC timeout** with no named constant and no documentation. Spurious timeout errors on slow machines or under heavy nvim load are hard to distinguish from genuine hangs.

10. **No `BackgroundWorker` abstraction** — four separate mutex+CV+stop_flag patterns (NvimRpc, UnixPtyProcess, MegaCityHost route_thread_, grid_thread_) each implement their own shutdown logic. Inconsistent shutdown ordering is a common source of use-after-free in multi-threaded C++.

---

## Top 10 Quality-of-Life Features

*(Checked against `docs/features.md` and `plans/work-items/` — none of these are already implemented or tracked as active work items unless noted.)*

1. **Tab bar / named panes** — the split tree already supports multiple panes with independent hosts, but there is no tab bar or way to name/label panes. A tab bar would make the multi-pane workflow usable for daily driving and is the single highest-visibility missing UX element for a terminal frontend.

2. **Session persistence** — save and restore the pane layout (split tree + host type per pane + CWD) across restarts. Most terminal users have a "morning setup" they recreate manually. This is a natural extension of the named launch profiles work item (23) already tracked.

3. **URL detection and click-to-open** — the VT parser already handles OSC 8 hyperlinks (tracked as work item 20), but plain text URLs (http://, file://) could be detected by regex and made clickable/hoverable without OSC 8 support from the application. This would work immediately in all shell hosts.

4. **Configurable color scheme** — `[terminal]` supports fg/bg, but there is no mechanism to set the 16 ANSI palette colors or apply a named color scheme (e.g., Solarized, Gruvbox, Catppuccin). Terminal color scheme support is the most-requested feature category in nearly every terminal emulator.

5. **Shell integration breadcrumb / prompt tracking** — OSC 133 (tracked as work item 24) is the right foundation, but even without full shell integration, a simple "current command output region" highlight (shade the background of the most recent command's output) would improve navigation for long-running commands.

6. **Find-in-scrollback** — a keyboard-invocable search bar that highlights matching text in the scrollback buffer and jumps between matches. The keyboard copy mode (work item 21) is related but find-in-scrollback is independently valuable and well-defined.

7. **Zoom pane** — a keybinding to temporarily expand the focused pane to full window size. This is a common action in tmux/kitty/WezTerm workflows and is trivially reversible.

8. **Per-pane title from OSC 2/0** — many shell integrations and terminal programs set the window title via `ESC]0;title\007`. Currently the window title is fixed. Routing OSC 0/2 to the focused pane and displaying it in the tab bar (feature 1) or title bar would make the host context visible.

9. **Configurable cursor style** — bar, block, and underline cursor styles with configurable blink rate. The cursor is currently rendered as a fixed block. This is a low-implementation-cost feature with high personalization value.

10. **Font fallback UI** — a diagnostics panel section showing which glyphs fell back to which fallback font, and which glyphs were replaced by a placeholder. Currently fallback chain failures are invisible to the user, making it hard to configure `fallback_paths` correctly.

---

## Top 10 Tests to Add for Stability

*(Prioritized by likelihood of shipping a user-visible bug if missing.)*

1. **InputDispatcher chord state machine** — test partial chord cancellation (prefix key then unrelated key), timeout expiry, modifier key held during chord, and the same prefix key pressed twice. Chord handling is the most user-visible input bug category with no current test coverage.

2. **VT parser buffer boundary paths** — test sequences that exactly fill and overflow the 64 KB plain text buffer, 4 KB CSI buffer, and 8 KB OSC buffer. Include truncated escape sequences at the end of a read chunk. Buffer boundary bugs are a primary source of terminal display corruption.

3. **Grid scroll double-width cell fixup** — tests for `scroll()` when a double-width cell straddles the scroll region boundary (the phantom half-cell at column 0 or at column width-1). This is the most common grid scroll correctness failure in terminal emulators.

4. **Alt screen save/restore across scrollback wrap** — switch to alt screen when the ring is at capacity, push more rows, then restore. Verify the scrollback position and viewport offset are correct after the round-trip.

5. **Atlas reset retry in GridRenderingPipeline** — fill the atlas to near-capacity, then flush a frame that requires more glyphs than fit in the remaining space. Verify the retry succeeds and the frame renders correctly rather than losing glyphs.

6. **App initialization rollback** — test that `App::initialize()` handles renderer-init failure, text-service-init failure, and host-init failure without leaking resources. Requires extracting `AppDeps` first (see bad thing #2) but is the correct end state.

7. **ConfigDocument range validation and fallback** — test that `scroll_speed` outside (0.1, 10.0], an `atlas_size` that is not a power of two, and a missing required key each produce the correct WARN and fallback value rather than UB or a hard crash.

8. **HostManager SplitTree divider drag clamping** — drag a divider to the minimum and maximum ratio bounds and verify the tree does not produce zero-height or zero-width panes. Test deep nesting (3+ splits) to verify recursive layout is consistent.

9. **GlyphCache face-pointer invalidation regression** — create a GlyphCache, populate it with entries for a face, destroy the face via FontManager, then request a glyph from the old face. Verify the cache returns a placeholder rather than dereferencing a dangling pointer. This is a regression guard for work item 00.

10. **Renderer dirty range coalescing** — test that writing overlapping and adjacent dirty regions produces the correct minimal upload range. Verify that a cursor move (which invalidates a single cell) does not cause a full-buffer re-upload. This is both a correctness and a performance regression test.

---

## Top 10 Worst Features

*(Evaluated against user-facing quality, correctness, and implementation risk.)*

1. **Silent CellText truncation for long emoji** — from the user's perspective, complex family emoji or flag sequences can appear as garbage or replacement characters with no indication of why. The feature nominally "works" but produces invisible correctness failures for a growing category of Unicode text.

2. **Font atlas reset on font size change** — the user presses `Ctrl+=` and the UI freezes for a perceptible interval while the atlas is rebuilt synchronously. On large files with many distinct glyphs this stall is tens of milliseconds. The feature works but the UX is noticeably rough.

3. **Hard-coded 5-second RPC timeout** — on slow machines or under heavy nvim indexing, legitimate operations time out and are reported as errors. The feature cannot be tuned by the user and produces misleading diagnostics.

4. **Config error handling via WARN + silent fallback** — when `config.toml` contains invalid values, Draxul silently falls back to defaults and logs a WARN. Users who don't run with `--log-level warn` or `--log-file` never see the error. A startup toast or diagnostics panel notice would make this visible.

5. **Notification queue drop without user feedback** — if the main thread stalls long enough for the nvim notification queue to hit 4096 entries, nvim output is silently discarded. From the user's perspective, the editor display becomes incorrect without any indication of why or how to recover (other than restarting).

6. **Chord prefix with no visual feedback** — when the user types the chord prefix key (e.g., `Ctrl+S`), there is no visual indicator that Draxul is waiting for the second key. If the prefix times out or is cancelled, the user has no way to know whether their key was consumed by the chord system or forwarded to nvim.

7. **`fallback_paths` configuration requires file paths** — users must specify absolute paths to fallback font files in `config.toml`. There is no font name lookup (e.g., `fallback_fonts = ["Noto Color Emoji"]`) and no diagnostics panel indication of which glyphs are using the fallback chain. Configuring fallback fonts is effectively a trial-and-error process.

8. **Scrollback limited to 2000 rows without user configuration** — the scrollback capacity is hard-coded. Power users routinely want 10 000–100 000 rows for long-running commands. The ring buffer implementation already supports arbitrary sizes; the capacity just needs to be a config key.

9. **Split pane hosts always use platform default shell** — when the user splits a pane, the new pane always opens a Zsh (macOS) or PowerShell (Windows) shell regardless of what the primary host is doing. A user working in the nvim host cannot split to a second nvim instance, and there is no way to configure which host type new splits use.

10. **MegaCity `--host megacity` is not guarded when Tree-sitter / SQLite are unavailable** — if the user specifies `--host megacity` but the required codebase index does not exist or is corrupt, the startup failure is a crash or opaque error rather than a graceful "no codebase found, building index..." message. Work item 12 tracks degraded-init testing but the user-facing fallback path does not yet exist.

---

*End of report.*
