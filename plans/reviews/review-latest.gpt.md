**Review**
Static inspection only of the current tree at `HEAD fffa7af` from March 28, 2026. I scanned `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, `plans/`, and [`docs/features.md`](/Users/cmaughan/dev/Draxul/docs/features.md). I did not build, run, or edit anything. Outside the Megacity stack, the overall layout is good; Megacity is the clear architectural outlier.

**Findings**
1. **Megacity is the main architectural hotspot.** [`megacity_host.h`](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/include/draxul/megacity_host.h#L38), [`megacity_host.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/src/megacity_host.cpp#L168), [`semantic_city_layout.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/src/semantic_city_layout.cpp#L455), [`megacity_render_vk.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/src/megacity_render_vk.cpp#L1), and [`megacity_render.mm`](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/src/megacity_render.mm#L1) are each large, high-conflict files. Host lifecycle, config persistence, DB/scanner orchestration, route/grid workers, UI state, and camera control are all concentrated there.
2. **Megacity still pierces renderer encapsulation.** [`libs/draxul-megacity/CMakeLists.txt`](/Users/cmaughan/dev/Draxul/libs/draxul-megacity/CMakeLists.txt#L76) adds private Metal/Vulkan renderer source dirs to Megacity includes, and [`tests/CMakeLists.txt`](/Users/cmaughan/dev/Draxul/tests/CMakeLists.txt#L19) exposes `libs/draxul-megacity/src` to tests. The module split is therefore partly conventional, not enforced.
3. **CLI parsing can hard-crash on malformed numeric input.** [`app/main.cpp`](/Users/cmaughan/dev/Draxul/app/main.cpp#L146) uses unchecked `std::stoi` for `--screenshot-delay` and `--screenshot-size`, and [`app/main.cpp`](/Users/cmaughan/dev/Draxul/app/main.cpp#L217) does not wrap `parse_args()` in error handling. Bad input currently looks like process termination, not a clean usage error.
4. **Core interfaces still expose too much mutable state.** [`window.h`](/Users/cmaughan/dev/Draxul/libs/draxul-window/include/draxul/window.h#L73) publishes callback slots as public members, and [`host.h`](/Users/cmaughan/dev/Draxul/libs/draxul-host/include/draxul/host.h#L77) passes a broad `HostContext` bag containing window, renderer, text, config, ownership, launch, and DPI state. That raises coupling and lifetime risk for concurrent edits.
5. **`SdlWindow` is carrying too many platform responsibilities.** [`sdl_window.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-window/src/sdl_window.cpp#L32), [`sdl_window.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-window/src/sdl_window.cpp#L137), [`sdl_window.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-window/src/sdl_window.cpp#L272), [`sdl_window.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-window/src/sdl_window.cpp#L380), and [`sdl_window.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-window/src/sdl_window.cpp#L472) combine DPI diagnostics, title policy, Win32 activation hacks, event dispatch, wake plumbing, and native file dialogs in one class.
6. **Test ergonomics are weaker than the comments suggest.** [`tests/CMakeLists.txt`](/Users/cmaughan/dev/Draxul/tests/CMakeLists.txt#L6) says new `*_tests.cpp` files are auto-discovered, but it uses plain `file(GLOB ...)` without `CONFIGURE_DEPENDS`, so new files are missed until CMake re-configures. That is exactly the kind of silent failure that slows multi-agent work.
7. **Some of the largest tests mirror the same monolith pattern as production hotspots.** [`tests/megacity_scene_tests.cpp`](/Users/cmaughan/dev/Draxul/tests/megacity_scene_tests.cpp#L1) is a large private-header integration block that even sleeps/pumps in the test body at [`tests/megacity_scene_tests.cpp`](/Users/cmaughan/dev/Draxul/tests/megacity_scene_tests.cpp#L54). It gives coverage, but it is expensive to refactor and easy to conflict in.
8. **User-facing limits are still hard-coded into core code.** [`scrollback_buffer.h`](/Users/cmaughan/dev/Draxul/libs/draxul-host/include/draxul/scrollback_buffer.h#L35) fixes scrollback at 2000 rows, and [`selection_manager.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-host/src/selection_manager.cpp#L114) truncates overlays at `kSelectionMaxCells`. Even if acceptable today, these are policy decisions embedded in mechanics.
9. **The current feature reference is malformed.** [`docs/features.md`](/Users/cmaughan/dev/Draxul/docs/features.md#L89) is broken around diagnostics, keybindings, and config tables. In a fast-moving repo, documentation drift becomes a coordination bug.

**Assumptions**
- I treated Megacity as a supported part of the product, not a throwaway demo.
- I avoided recommending items already open, complete, or explicitly iceboxed in `plans/work-items*`.
- This review is static only, so runtime-only regressions may still exist.

**Top 10 Good Things**
- The non-Megacity core is genuinely modular, especially `grid`, `host`, `renderer`, `font`, `nvim`, and `window`.
- [`split_tree.cpp`](/Users/cmaughan/dev/Draxul/app/split_tree.cpp#L13) is small, readable, and heavily tested.
- [`grid_host_base.h`](/Users/cmaughan/dev/Draxul/libs/draxul-host/include/draxul/grid_host_base.h#L16) is a strong reusable seam for grid-based hosts.
- [`grid_rendering_pipeline.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp#L78) is focused and testable.
- Terminal coverage is deep and broad, especially [`terminal_vt_tests.cpp`](/Users/cmaughan/dev/Draxul/tests/terminal_vt_tests.cpp#L18).
- Config lifecycle/recovery coverage is stronger than most projects, especially [`config_lifecycle_tests.cpp`](/Users/cmaughan/dev/Draxul/tests/config_lifecycle_tests.cpp#L52).
- Logging is well-structured and optimized for hot-path checks in [`log.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-types/src/log.cpp#L38).
- Neovim redraw dispatch is data-driven and self-checking in [`ui_events.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-nvim/src/ui_events.cpp#L71).
- App-level smoke testing with fakes is solid in [`app_smoke_tests.cpp`](/Users/cmaughan/dev/Draxul/tests/app_smoke_tests.cpp#L1).
- The planning/review/work-item discipline is unusually mature and helps coordination.

**Top 10 Bad Things**
- Megacity dominates the architectural risk budget.
- Module boundaries still leak through private source includes.
- Public callback/context surfaces are broader than they should be.
- `SdlWindow` is a cross-platform kitchen sink.
- `app/main.cpp` still has brittle CLI parsing.
- Test auto-discovery is not actually automatic.
- Several hotspot files are too large for low-conflict parallel work.
- One of the largest test files is integration-heavy and private-header-dependent.
- `docs/features.md` is currently not trustworthy.
- The agent helper scripts duplicate path/prompt plumbing and add maintenance noise.

**Top 10 QoL Features To Add**
- `close_pane` action with a default keybinding and menu entry.
- `duplicate_pane` that preserves host kind, command, cwd, and args.
- `restart_host` or reconnect action for the focused pane.
- Split the current pane into the same host type by default, with explicit “split to shell” as a separate action.
- Recent files and recent working directories menus.
- Pane headers or status chips showing host, cwd, and disconnected/busy state.
- Keyboard shortcut/help overlay for GUI actions and pane control.
- “Open current working directory” in Finder/Explorer.
- Export diagnostics snapshot to markdown or JSON from the UI.
- One-click capture/copy of the current frame from the UI.

**Top 10 Tests To Add**
- `app/main.cpp` invalid `--screenshot-delay` and `--screenshot-size` inputs fail cleanly instead of throwing.
- `SdlWindow` native file-dialog result and cancel event routing.
- `SdlWindow::wake()` plus `wait_events()` no-lost-wake behavior.
- `SdlWindow` mixed event ordering: resize, display-scale, drop-file, and dialog-result in one queue.
- City DB migration from older `PRAGMA user_version` values to current schema.
- City DB behavior for future/unsupported schema versions.
- `megacity_db_path()` resolution for repo-root versus app-support cases.
- MegaCity stale route-result suppression during rapid reselection.
- MegaCity shutdown while route/grid work is still in flight.
- A docs smoke test that verifies `scripts/build_docs.py` does not corrupt `docs/features.md`-style markdown output.

**Worst 10 Current Features**
- MegaCity as a first-class host: visually ambitious, but far too expensive relative to core editor value.
- The MegaCity tuning surface is much larger than its stability and modularity budget.
- Pane splits defaulting to a shell instead of inheriting active context.
- Fixed 2000-row scrollback.
- Fixed 8192-cell selection limit.
- Non-Windows window titles getting an emoji prefix in [`sdl_window.cpp`](/Users/cmaughan/dev/Draxul/libs/draxul-window/src/sdl_window.cpp#L194).
- The diagnostics panel trying to be a control room instead of a support panel.
- Native file-open being a raw path handoff with no recent/history affordance.
- Screenshot/render-test CLI flow being brittle on invalid numeric input.
- Pane splitting feeling one-way: create is first-class, but close/duplicate/restart are not.