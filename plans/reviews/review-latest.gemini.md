# Draxul Codebase Technical Review

**Review Date:** 2026-07-05  
**Reviewer:** Antigravity (Gemini 3.5 Flash)  
**Scope:** Static codebase analysis of source files under `app/`, `libs/`, `shaders/`, `tests/`, `scripts/`, and `plans/`.

---

## 1. Module Separation & Architecture

### Strengths
- **Decoupled Architecture**: Code is broken down into small, single-purpose libraries under `libs/` (e.g., [draxul-types](file:///D:/dev/Draxul/libs/draxul-types), [draxul-window](file:///D:/dev/Draxul/libs/draxul-window), [draxul-renderer](file:///D:/dev/Draxul/libs/draxul-renderer), [draxul-font](file:///D:/dev/Draxul/libs/draxul-font), [draxul-grid](file:///D:/dev/Draxul/libs/draxul-grid), and [draxul-nvim](file:///D:/dev/Draxul/libs/draxul-nvim)).
- **Clean Platform Separation**: Platform-specific rendering pipelines (Vulkan for Windows, Metal for macOS) are isolated into distinct folders (`src/vulkan/` and `src/metal/`) within [draxul-renderer](file:///D:/dev/Draxul/libs/draxul-renderer). There are no platform `#ifdef`s leaking into the shared renderer abstraction layer.
- **Strictly Acyclic Dependencies**: The dependency flow is hierarchical and clean: logic dependencies point down from `app/` through `libs/` without circular cycles.
- **Interface-First Contract Design**: Subsystem coupling relies on abstract interfaces (`IWindow`, `IGridRenderer`, `IHost`, `IBaseRenderer`, `IRenderPass`) rather than concrete classes. This design promotes mockability and clean implementation swapping.
- **Dependency Injection**: Subsystems avoid global singletons. Configuration, windowing references, and renderer services are injected via explicit struct bundles (e.g., `AppDeps`, `HostManager::Deps`, `InputDispatcher::Deps`).

### Issues & Refactoring Opportunities
- **Bloated `App` God Object**: [app.cpp](file:///D:/dev/Draxul/app/app.cpp) serves as a major integration hotspot (~105 KB, 200+ line initializer). It conflates application lifecycle, workspace switching, window resizing, RPC message routing, weather synchronization, session checkpointing, and split layouts. Workspace management (tab list, tree orchestration) should be extracted into a dedicated `WorkspaceManager` class.
- **Monolithic `IHost` Interface**: The `IHost` interface in [host.h](file:///D:/dev/Draxul/libs/draxul-host/include/draxul/host.h) has accumulated over 100 member declarations (combining event routing, layout sizing, process status queries, and ImGui overlays). It would benefit from being partitioned into narrower interfaces (`IInputHandler`, `IDrawable`, `IProcessHost`).
- **Verbose Dependency Structs**: Dependency structures like `InputDispatcher::Deps` pack dozens of raw function callbacks. If any of these are left uninitialized (i.e. `nullptr`), keyboard or mouse interactions silently fail without clear diagnostic logging.
- **ChromeHost Double Role**: [ChromeHost](file:///D:/dev/Draxul/app/chrome_host.h) implements `IHost` but behaves like a central layout decorator (rendering the tab strip, status pills, and dividers). This mixing of grid hosts and GUI ornaments makes spatial hit-testing fragile.

---

## 2. Code Smells & Reliability Issues

### Critical Issues

#### 1. Shell Command Injection in Weather Geocoding
In [weather_service.cpp](file:///D:/dev/Draxul/app/weather_service.cpp#L360), query parameters from `weather_location` are URL-encoded via a manual string encoder in `try_geocode`. However, this helper only encodes space, comma, ampersand, equals, hash, and percent. Other shell-significant characters—most notably double quotes (`"`)—are left unencoded. 
The geocoding query is subsequently concatenated directly into a shell command template:
```cpp
std::string cmd = "curl -s --max-time 15 --connect-timeout 10 \"";
cmd += url;
cmd += "\" 2>";
cmd += kNullDevice;
```
This command string is executed via `popen`, passing it directly to `/bin/sh` or `cmd.exe`. A local configuration setting such as:
`weather_location = "Paris\" ; command_injection_here ; \""`
closes the quote and executes arbitrary shell commands.

#### 2. Unsafe Allocations in `fork()` Child Processes (macOS)
In multithreaded environments, calling memory allocation routines or non-reentrant system calls in the child process after `fork()` but before `execve()` can cause deadlocks if another thread was holding the allocator/malloc lock at the moment of fork.
Two spawn paths in the application violate this async-signal-safety contract:
- In [main.cpp](file:///D:/dev/Draxul/app/main.cpp#L373-L379), the child process allocates a `std::vector<std::string>`, copies parent arguments, allocates a `std::vector<char*>`, and invokes `exe_path.string()`.
- In [session_picker_host.cpp](file:///D:/dev/Draxul/app/session_picker_host.cpp#L295-L300), the child process allocates a `std::vector<char*>` and calls `executable_path_.string()`.
These operations should be migrated to `posix_spawn()` or prepared entirely in the parent process prior to the fork.

#### 3. Fragile Lifetime Bindings via Raw Borrowed Pointers
subsystems like `GridHostBase` store raw borrowed pointers to parent services (`IHostCallbacks*`, `IWindow*`, `TextService*`). If the lifecycle of the host outlives these dependency objects (e.g. during rapid pane destructions or asynchronous shutdowns), dereferencing the raw pointers triggers use-after-free crashes.

#### 4. Non-Atomic Configuration and Session State Persistency
In [session_state.cpp](file:///D:/dev/Draxul/app/session_state.cpp#L496), session states and runtime topologies are saved by opening files directly via `std::ofstream` with `std::ios::trunc`. If a crash, out-of-disk space, or power loss occurs mid-write, the existing session file is corrupted. File writes should use a temporary-write-and-atomic-replace pattern (using `std::filesystem::rename`).

---

## 3. Testing Gaps

### Unregistered & Skipped Render Tests
- **Unregistered Test Files**: Render test scripts like `wide-char-scroll.toml`, `claude-logo.toml`, `readme-hero.toml`, `readme-overlay.toml`, and `readme-view.toml` exist in the [tests/render](file:///D:/dev/Draxul/tests/render) directory but are not registered in the `DRAXUL_RENDER_SCENARIOS` list within the root [CMakeLists.txt](file:///D:/dev/Draxul/CMakeLists.txt#L426). They are silently ignored by `ctest`.
- **Stale Registrations**: The `ligatures-view` scenario is listed in the root `CMakeLists.txt` but no `ligatures-view.toml` file exists on disk. CMake silently emits a status message and skips it instead of failing configuration, leading to a false sense of test coverage.

### MacOS Exclusion of Attach Tests
In [tests/CMakeLists.txt](file:///D:/dev/Draxul/tests/CMakeLists.txt#L56), the entire [session_attach_tests.cpp](file:///D:/dev/Draxul/tests/session_attach_tests.cpp) file is excluded from Apple compilation to bypass a Catch2 compilation warning (`__int128` streaming conflict). This leaves the persistent session attach IPC mechanism completely untested on macOS, where it runs on top of UNIX domain sockets.

### Missing Integration Edge Cases
- **Concurrent Split & Close Stressing**: No unit or integration tests repeatedly stress rapid pane splitting and closing cycles, pane zooming during splits, or resizing concurrent with host crashes.
- **Atlas Exhaustion Handling**: There are no tests verifying that the font engine behaves gracefully (or evicts items correctly) if the 2048x2048 glyph atlas is completely filled by a large Unicode text dump.

---

## 4. Maintainability & Clean Code

- **Scattered Test Mocking**: Mocks like `FakeWindow`, `FakeRenderer`, and `FakeHost` are duplicated across multiple test files (e.g. `app_pump_tests.cpp`, `input_dispatcher_routing_tests.cpp`, `gui_action_handler_tests.cpp`) rather than being centralized in a shared testing support library.
- **Implicit Shader Compilation Dependencies**: If a `.metal` or `.glsl` file is changed, the build system does not always reliably rebuild the shader library because dependencies are not completely tracked by CMake.
- **Opaque Font Fallback Chain**: If a font fallback fails to load, the error is logged internally but never surfaced to the user. The application degrades to displaying blank spaces/tofu without explaining why fallback fonts failed.

---

## Top 10 GOOD Things

1. **Strict Platform Separation**: Vulkan and Metal backends are completely decoupled. Zero platform `#ifdef` leaks in shared code, easing platform maintenance.
2. **Robust Font Shaping & Ligatures**: Deep HarfBuzz and FreeType integration correctly handles up to 6-cell ligatures (e.g., `!==`) and splits them correctly during editing.
3. **Procedural Box Drawing**: Blocks U+2500–257F and U+2580–259F are generated on the fly, avoiding anti-aliasing alignment gaps on high-DPI monitors.
4. **Dependency Injection Discipline**: Central objects use struct-based dependency injection rather than singletons, easing testing.
5. **No Circular Dependencies**: strictly acyclic target dependency layout (types -> window -> renderer -> font -> grid -> host -> app).
6. **Detailed Diagnostic Panel**: The ImGui overlay (F12) provides rich real-time visual insights into framerates, dirty cell counters, and atlas texture allocations.
7. **Redraw Replay Fixtures**: [replay_fixture.h](file:///D:/dev/Draxul/tests/support/replay_fixture.h) allows mocking msgpack redraw events, making terminal drawing issues reproducible without spawning Neovim.
8. **Isolated Product Modules**: Kanban, Markdown, MegaCity, and SatView are clean, isolated optional modules that do not leak source dependencies into the main app core.
9. **Rich Emulation Support**: Deep support for mouse modes, focus tracking, bracketed paste, OSC 7 (cwd tracking), and OSC 52 (remote clipboard).
10. **Clean Onboarding Presets**: Comprehensive compile presets for Release, Debug, and Sanitizers (ASan/UBSan) make building the app easy on both Windows and macOS.

---

## Top 10 BAD Things

1. **Weather Service Shell Injection**: manual geocoder url-encoding permits shell command injection in `popen` calls when `weather_location` contains quotes.
2. **Unsafe fork() Heap Operations**: Unix child processes allocate vectors and strings after `fork()`, risking deadlocks in multithreaded runs.
3. **Silent Render Test Drop**: Render TOML scenarios are silently skipped if missing, and several existing files are not registered in the build system.
4. **Attach Tests Omitted on Apple**: Chrono warning workarounds result in complete exclusion of session attach tests on macOS.
5. **App God Class Bloat**: The `App` class handles way too many responsibilities (lifecycle, workspaces, RPC, geocoding, session files).
6. **Fragile Raw Pointer Contracts**: raw pointers for callbacks and text services risk dangling pointer crashes.
7. **Bloated Input Dispatcher Callbacks**: `InputDispatcher::Deps` contains over 20 raw function pointers; missing one breaks input handling silently.
8. **Non-Atomic Session Writes**: Truncating session files directly poses a high risk of config corruption during system crashes.
9. **Duplicate Test Mocks**: Redundant Mock window/renderer/host classes are copied across multiple test source files.
10. **Silent Font Fallback Failures**: Fallback font errors only write to debug logs, leaving users with empty character boxes without feedback.

---

## Best 10 Features to Add (Quality of Life)

1. **Unified Safe HTTP Transport Service**: Replace geocoding/cloud `popen` command strings with a safe, argv-based process spawning utility or an embedded HTTP library.
2. **Atomic Session and Config Writes**: Save all JSON/TOML configuration and session files to temporary files, then replace them atomically via `std::filesystem::rename`.
3. **WorkspaceManager Separation**: Refactor the tab, pane, and split tree layout orchestration out of `App` into a distinct workspace manager.
4. **Shared Test Support Library**: Centralize all mock classes (`FakeWindow`, `FakeRenderer`, `FakeHost`) into a shared static library under `tests/support`.
5. **Auto-Discovered Render Test Manifest**: Build a test discovery manifest (or file glob) that fails the CMake run if a test or its reference image is missing.
6. **Transaction-based Configuration Reloads**: Verify `config.toml` changes before applying them; if invalid, roll back and push a toast notification showing the syntax error.
7. **Dynamic Multi-Page Glyph Atlas**: Support allocating secondary pages or resizing the glyph texture when the atlas fills up, rather than silently dropping text.
8. **Unified `Result<T, Error>` Wrapper**: Standardize error return types across libraries to prevent silent failures and unchecked boolean flags.
9. **Toast Alerts for RPC Hanging**: Show a toast notification ("Neovim not responding") when Neovim RPC requests exceed a timeout threshold.
10. **Keybinding Chord Conflict Warnings**: Inspect configured keybindings at startup and show a toast warning if there are overlapping chord conflicts.

---

## Best 10 Tests to Add (Stability)

1. **MacOS Session Attach Integration Tests**: Fix the Catch2 chrono formatting warning and re-enable `session_attach_tests.cpp` on Apple platforms.
2. **Weather Geocoding Shell Injection Guard Tests**: Validate `try_geocode` against command injection payloads (semicolons, quotes, backticks).
3. **HostManager Split/Close Stress Tests**: Repeatedly split, close, zoom, and resize panes concurrently to test for layout deadlocks or assertion failures.
4. **Glyph Atlas Exhaustion Stress Tests**: Load thousands of unique unicode characters to verify cache eviction handles boundary overflows gracefully.
5. **DPI Change Render Synchronization Tests**: Call `on_display_scale_changed` concurrently with active render frame submissions to test for Vulkan/Metal synchronization issues.
6. **InputDispatcher Missing Callback Resilience Tests**: Construct the dispatcher with null callbacks and verify that input events are safely rejected without segfaulting.
7. **Thread-Safe Grid Redraw Verification Tests**: Verify under ThreadSanitizer (TSan) that concurrent grid writes from the RPC reader thread and reads from the render thread are free of data races.
8. **Config Reload Concurrent Stress Tests**: Trigger `reload_config()` repeatedly while simulating rapid keyboard and mouse events to check for config races.
9. **Host Lifecycle State Machine Tests**: Verify that calling host methods (`pump`, `draw`) before `initialize` or after `shutdown` returns safe error codes rather than crashing.
10. **Corrupt msgpack RPC Fuzzing Tests**: Fuzz the Neovim RPC reader thread with invalid arrays and partial packets to verify the parser handles transport errors gracefully.

---

## Worst 10 Existing Features

1. **Geocoding URL String Concatenation**: raw query string formatting for weather geocoding.
2. **Unsafe fork() Heap Allocations**: Allocating std::vectors/strings inside the Unix/macOS child process path post-fork.
3. **Exclusion of MacOS Attach Tests**: Completely bypassing `session_attach_tests.cpp` on macOS on CMake level due to a compiler warning/formatting issue.
4. **Raw Lifetime Contracts for Host Callbacks**: `GridHostBase` stores raw pointers to parent callbacks with no automatic lifetime management, risking dangling pointer dereferences.
5. **Silent Glyph Dropping on Atlas Overflow**: Bailing out when the shelf packer runs out of space, leaving the screen with missing text and no user-visible error.
6. **Linear/Blocking curl Geocoding in Weather Worker**: Weather geocoding runs in a background thread but uses blocking `popen` commands, making thread termination block on standard timeouts.
7. **Unverified/Unstaged Font Fallbacks**: Opaque fallback loading; if a font fails to resolve, there is no notification, resulting in empty glyph grids.
8. **Non-atomic session state truncation**: Direct `trunc` write to the configuration file, presenting a risk of data loss.
9. **Bloated callback interface on IHost**: `IHost` acts as a monolithic surface for all host operations, combining process, VT, mouse, font, rendering, and status queries.
10. **Strict, Non-fallback Terminal Hex Color Parser**: Hex colors that are slightly malformed cause terminal colors to fail completely, rather than falling back to standard terminal defaults.
