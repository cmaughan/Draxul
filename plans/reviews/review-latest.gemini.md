# Draxul Codebase Quality & Maintenance Review

**Reviewer:** Gemini 3.5 Flash (Medium)  
**Date:** 2026-07-15  
**Scope:** A comprehensive scan of all source files under `app/`, `libs/`, `modules/` (kanban, markdown, megacity, satview, score), `shaders/`, `tests/`, and `scripts/`.

---

## 1. Architectural Analysis, Module Separation, and Layout

Draxul's codebase is designed with a clear layering system where libraries under `libs/` compile into isolated modules and dependency flows are unidirectional (libs only link downwards, `app/` is the orchestrator). In practice, this design is mostly respected, particularly through the `HostProviderRegistry` mechanism which lets module hosts self-register at runtime, preventing the core application from having static source-level dependencies on them.

However, several architectural erosion points are present in the latest code:
* **The "Config-Option" Ritual:** Adding a new user setting requires modifying up to 5 files in core configuration (`libs/draxul-config/`) and another 4-5 files in each module host (`SatView`, `ScoreView`, etc.). This creates a tight coupling and high merge friction for multiple agents working concurrently.
* **`draxul-types` Monolithic Creep:** What was originally documented as a header-only POD types library (`types.h`, `events.h`) now compiles implementation files like `log.cpp`, `bmp.cpp`, and `perf_timing.cpp`. It acts as a dependency-injection bottleneck at the bottom of the graph.
* **Sideways and Upward Leaks:** Foundation enums like `HostKind` statically enumerate optional modules like `MegaCity`, `SatView`, and `ScoreView`. Every new optional host forces edits to bottom-layer files.
* **`app/` Bloat:** The orchestrator layer `app/app.cpp` has grown to nearly 3,000 lines of code, holding split-tree layouts, session persistence, fuzzy matching, and weather synchronization.

---

## 2. Deeper Dives, Specific Code Smells, and Multi-Agent Challenges

### 2.1 Thread Safety and Race Conditions
* **Windows `NvimProcess` Shutdown Race:** In `NvimProcess::shutdown()` (on Windows), the process handles `impl_->proc_info_.hProcess` and `hThread` are closed before `impl_->started_` is set to `false`. Concurrently, `NvimProcess::is_running()` checks `impl_->started_` first, then calls `GetExitCodeProcess` with the process handle. If `is_running()` runs in parallel with `shutdown()`, it can attempt to call `GetExitCodeProcess` with an already closed or recycled handle, leading to undefined behavior or crashes.
* **`MicPlayerInput` Stream Allocation Race:** In `modules/score/draxul-scoreview/src/mic_player_input.cpp`, a detached thread handles permission checks and stream creation. The destructor checks `shared_->stream` to close it. If the owner is destroyed after the background thread passes `abandoned.exchange(true)` but before it assigns the newly opened stream to `s->stream`, the stream is leaked permanently.

### 2.2 Memory Safety and Callback Lifetimes
* **Dangling-Pointer Window Callbacks:** Inside `sdl_window_macos.mm`, `install_dock_reopen_handler` saves a raw pointer to a `std::function` owned by an `SdlWindow` instance inside a global pointer `g_reopen_callback`. When the window is destroyed, `g_reopen_callback` is not cleared, leaving a dangling pointer. If a Dock reopen Apple Event fires post-destruction, a crash or use-after-free occurs.
* **`InputDispatcher` Window Callback Lifetimes:** `InputDispatcher::connect()` assigns callbacks capturing `this` (a raw pointer to the dispatcher) to the `IWindow` instance. Since `InputDispatcher` lacks a destructor or explicit disconnect cleanup, if the dispatcher is destroyed before the window, late window events can invoke the callbacks on a deleted dispatcher pointer.

### 2.3 Shell Command Injection Vulnerabilities
* **`popen` with Unsanitized Shell String Interpolation:** In `WeatherService::run_curl` (`app/weather_service.cpp`) and similarly inside `SatView`'s catalog downloader, network calls are built by appending strings directly into a shell command template and executing them using `popen`. If user configuration (like `weather_location` or catalog sources) contains quotes, semicolons, or backticks, arbitrary shell command execution is possible.
* **Blocking Thread Joins on Exit:** The `WeatherService` worker runs on a background thread executing a blocking `curl` call with a timeout of up to 15 seconds. On application stop, the main thread calls `thread_.join()`, freezing the application's UI on shutdown if network connectivity is slow.

### 2.4 Code Duplication and Stale Build Dependencies
* **Metal Shader Build Dependency Mismatch:** In `CompileShaders_Metal.cmake`, the dependency list for compiling `grid.metallib` includes `decoration_constants_shared.h` but omits `quad_offsets_shared.h`. Because `grid.metal` includes both, editing `quad_offsets_shared.h` does not trigger a shader rebuild, leading to silent layout and rendering discrepancies.
* **Vulkan Boilerplate Hand-Rolled Across Modules:** Opaque Vulkan buffer creations, pipeline layouts, and image transitions are hand-rolled repeatedly in `codeviz_render_vk.cpp`, `satview_render_vk.cpp`, and `markdown_render_pass_vk.cpp` rather than utilizing a shared library utility.

---

## 3. Top 10 Good Things About the Application

1. **Platform-Free `RendererState`:** The complete extraction of cursor, grid, and overlay update logic into a platform-agnostic class shared by Vulkan and Metal backends is clean and well-covered by tests.
2. **Defensive IPC Threading:** Bounded RPC event queues, timed-out request evictions, and strict happens-before comments make Neovim integration highly robust.
3. **GLOB-Based Test Discovery:** Adding a new test file requires zero modification to CMake lists, eliminating merge friction in a multi-agent environment.
4. **Strong Render Snapshot Pipeline:** The TOML-driven visual comparison system with settle-frame detection, scenario-specific tolerances, and blessing helper commands is highly professional.
5. **Module Registry Isolation:** Optional product hosts (Kanban, Megacity, SatView, ScoreView) self-register at runtime, avoiding compile-time coupling to the core app.
6. **Robust Parsing Limits:** Input buffers and RPC streams are protected by explicit limits (e.g. 256MB RPC caps) to prevent memory exhaustion from malicious grid inputs.
7. **Clean Platform Separation in Windowing:** Platform-specific window setups (`sdl_window_win32.cpp` vs `sdl_window_macos.mm`) keep the main window abstraction readable.
8. **Comprehensive Unit Testing Coverage:** Over 140 test files covering text shaping, VT parsers, session serialization, and layout algorithms without requiring a physical GPU.
9. **ccache Integration:** Speeds up local developer compiles significantly by routing C/C++ builds through `ccache` dynamically when found on the system path.
10. **Hermetic FetchContent Dependency Management:** Third-party libraries (SDL3, FreeType, HarfBuzz, tinyxml2, Verovio) are version-pinned and fetched automatically during CMake configuration.

---

## 4. Top 10 Bad Things About the Application

1. **Stale steering documentation:** `CLAUDE.md` and `plans/README.md` describe a directory layout and class hierarchy (e.g., referencing `I3DHost` and `plans/work-items/`) that were previously removed or restructured.
2. **Shell injection vulnerabilities via `popen`:** Directly constructing shell commands with unsanitized parameters inside `WeatherService` and SatView downloader routines.
3. **Background thread races during cleanup:** Potential stream leaks in `MicPlayerInput` and use-after-free races in Windows `NvimProcess::shutdown()`.
4. **Platform callback dangling pointers:** Global callback slots in `sdl_window_macos.mm` retain raw pointers to transient window instance members.
5. **No auto-run CI pipeline:** CI workflows are only triggered via manual `workflow_dispatch`, hiding build and visual snapshot regressions until manual checks are run.
6. **Monolithic `app/app.cpp` class:** The `App` class is a god class containing too many responsibilities (session saving, layout resizing, weather, action bindings).
7. **Highly repetitive Vulkan boilerplate:** 3D and rendering modules manually initialize graphics pipelines, swapchains, and image transitions instead of using shared utilities.
8. **The "Config-Option" edit tax:** High merge-conflict risk due to settings spanning 8-10 edits across multiple namespaces, structs, and host files.
9. **No automatic bless promotions across OSes:** Blessings can only be done on the host OS, making cross-platform reference image updates tedious.
10. **`PERF_MEASURE` profiling lock contention:** The performance profiling macro locks a global mutex, which can distort timing data on high-frequency calls.

---

## 5. Best 10 Quality-of-Life Features to Add

*(Filtered to exclude completed or iced items in `kanban/`)*

1. **Interactive Pinch-to-Zoom Font Scaling:** Support trackpad pinch gestures using SDL3 multi-finger events for zooming the grid size smoothly.
2. **Workspace Tab Drag-and-Drop Reordering:** Allow users to drag tabs on the top bar to change their display order.
3. **Command-Palette Search Filters:** Support prefixes like `hosts:`, `panes:`, or `settings:` inside the command palette to quickly filter commands.
4. **Interactive Theme Customizer UI:** A color-picker panel inside the diagnostics panel to configure chrome colors visually and write them directly to `config.toml`.
5. **Config Validation CLI Command (`draxul --validate-config`):** A command-line verification option to syntax-check and lint `config.toml` without initializing the graphics subsystems.
6. **Layout Template Exporter:** Export the current pane split layout as a named layout template to be reused with `--layout-template <name>`.
7. **Toast History Viewer:** A diagnostics panel tab that stores a running log of all toast alerts shown during the session.
8. **Terminal Scrollbar Click-to-Jump Option:** A configuration setting to choose whether clicking the scrollbar track scrolls by pages or jumps directly to the clicked point.
9. **Custom Workspace Quick Launcher:** A keyboard shortcut (e.g. `Ctrl+T`) that brings up a grid launcher for one-click initialization of Neovim, SatView, BioView, or Megacity.
10. **Terminal Selection Auto-Scroll:** Enable smooth automatic upward or downward scrolling when dragging a mouse selection outside the current viewport.

---

## 6. Best 10 Tests to Add for Stability

*(Filtered to exclude completed or iced items in `kanban/`)*

1. **Windows `NvimProcess` Shutdown Thread-Safety Test:** A multi-threaded test hammering asynchronous process spawning and shutdowns to catch handle reuse races.
2. **`MicPlayerInput` Destruction Race Test:** Specifically simulate the owner destroying `MicPlayerInput` during the background thread's open phase to verify that no stream handles leak.
3. **`InputDispatcher` Destructor Cleanup Test:** Assert that all window-level callback delegates are cleanly reset on `InputDispatcher` destruction, avoiding use-after-free on late inputs.
4. **`VerovioLayoutEngine` Corrupted XML Parsing Test:** Feed empty, truncated, or malformed MusicXML datasets to verify that the engraver aborts gracefully.
5. **SatView Ephemeris Edge Cases Test:** Validate geodetic conversion calculations at meridian wraps, poles, and zero altitudes to ensure no division-by-zero or NaNs are generated.
6. **Multi-Instance `ScoreHost` Audio Initialization Test:** Simulate splitting the view into multiple score hosts to verify that concurrent SDL audio stream initialization does not collide or fail.
7. **PTY Write Blocking and Interrupted Systems Tests:** Mock PTY write limits to verify that interrupted writes (EINTR) and partial writes loop and retry correctly.
8. **Fuzzy Search Query Extreme Boundary Test:** Test `fuzzy_match` with extremely long queries, empty search keys, and corrupted UTF-8 sequences.
9. **Metal Allocation Failure Recovery Test:** Mock GPU buffer allocation failures (`newBufferWithLength` returning `nil`) to ensure that the Metal renderer bails cleanly rather than attempting to draw.
10. **Session Rollback File IO Failure Integration Test:** Mock write permission denials or disk full events during session saving to verify that workspace memory rolls back cleanly to its original state.

---

## 7. Worst 10 Features (Design and Implementation Issues)

1. **Weather Service `popen` Integration:** Implementing a weather tracker inside the terminal emulator via unsafe shell execution (`popen`) of a blocking command-line `curl` program.
2. **Double-Parsing of Key Config Settings:** Config options like `url_detection` and `enable_osc8_hyperlinks` are resolved and parsed twice at separate execution locations in `app_config_io.cpp`.
3. **Manual Dispatch-Only GitHub Actions Workflow:** Pushing code directly to main does not trigger builds automatically, relying on developers to manually trigger CI runs.
4. **Highly Duplicated Configuration Settings:** The multi-file configuration updates required to add a single user setting, resulting in high merge conflicts.
5. **Source Files Compiling inside `draxul-types`:** Polluting a library chartered to only contain header-only POD types with singletons, filesystem utils, and BMP encoders.
6. **`app/app.cpp` acting as a Monolith:** The orchestration class handles too many separate tasks, violating the single responsibility principle.
7. **Bespoke Binary File Formats:** Hand-rolling three identical catalog binary container architectures (`DXSTAR1`, `DXCLINE1`, `DXCBND01`) for SatView instead of using a unified reader.
8. **Manual GLSL/MSL Shader Mirroring:** Writing twin shader files manually for Vulkan and Metal, which leads to layout and math divergence bugs.
9. **Global Platform Callback Dangling Pointers:** The Cocoa dock-reopen callbacks storing raw pointers to window members.
10. **Mutex-blocked `PERF_MEASURE` Timing Macro:** An instrumentation feature that introduces lock contention in hot paths, altering the timing of the code it measures.
