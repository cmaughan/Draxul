# Draxul Architectural and Code Review Report

This report presents a thorough review of the Draxul repository, focusing on its architecture, module separation, code quality, and testing robustness.

---

## 1. Top 10 Good Things

1.  **Strict Modularization:** The separation of concerns into distinct libraries (e.g., `draxul-grid`, `draxul-font`, `draxul-nvim`) is outstanding. It enforces clean boundaries and makes the codebase highly amenable to multi-agent development.
2.  **Robust Render Testing Infrastructure:** The TOML-based render scenario system with reference image comparisons is a top-tier feature. It provides deterministic verification for complex UI rendering across platforms.
3.  **Clean Abstractions for Key Systems:** Interfaces like `IHost`, `IGridRenderer`, and `IRenderPass` provide excellent decoupling between the application logic, the rendering backends, and the hosted processes.
4.  **Modern C++20 Adoption:** Idiomatic use of C++20 features (e.g., `std::span`, `std::string_view`, concepts) results in code that is both performant and readable.
5.  **Exemplary Documentation:** `GEMINI.md`, `CLAUDE.md`, and `docs/features.md` provide superior architectural context and developer onboarding, which is critical for autonomous CLI agents.
6.  **Deterministic Smoke Testing:** The `FakeWindow` and `FakeRenderer` setup allows for end-to-end orchestration tests without requiring real hardware or spawning expensive Neovim processes.
7.  **Platform Abstraction:** Core logic remains shared while platform-specific implementations (Metal vs. Vulkan, PTY vs. ConPTY) are neatly isolated.
8.  **Comprehensive Diagnostics Panel:** The F12 ImGui overlay is invaluable for real-time profiling of frame times, atlas usage, and startup steps.
9.  **High-Quality Build System:** CMake presets and `FetchContent` make the project remarkably easy to build and maintain across Windows and macOS.
10. **Developer Experience Tooling:** Scripts like `do.py` and `blessall` streamline repetitive tasks like updating render references and running specific test suites.

---

## 2. Top 10 Bad Things

1.  **God Class `App`:** `app/app.cpp` is a monolithic orchestrator (over 800 lines) handling too many disparate tasks (config loading, window management, host lifecycle, rendering, and testing).
2.  **MegaCity Coupling:** While MegaCity is "optional," its presence in the core application path adds significant complexity and "demo weight" to a project primarily aimed at being a high-performance Neovim frontend.
3.  **Technical Debt in `CellText`:** The fixed 32-byte buffer for Unicode clusters is a "magic number" that will eventually fail for complex ZWJ emoji sequences or multi-part combining characters.
4.  **Heuristic "Settle" Time in Render Tests:** Relying on a 250ms sleep/settle period in `run_render_test` is a fragile approach that can lead to flaky CI results.
5.  **Renderer `void*` Usage:** `IRenderContext` uses `void*` for native command buffers and encoders, sacrificing type safety for a thin cross-platform abstraction (partially addressed in planned refactors).
6.  **HostManager Over-responsibility:** `HostManager` manages both the binary split tree logic and the actual host process lifecycle; these should be separate concerns.
7.  **Scattered Frame-Sync Logic:** Logic for VBlank synchronization and frames-in-flight is spread across `AppOptions`, `RendererOptions`, and host-specific flags.
8.  **Input Dispatching Complexity:** `InputDispatcher` routes both GUI-specific actions and host input, which can lead to confusing routing logic in the application layer.
9.  **Lack of Multi-Agent Isolation in `app/`:** While the libraries are modular, the `app/` directory remains a bottleneck where multiple agents are likely to conflict during orchestration changes.
10. **Inconsistent Config Management:** Overriding configuration via CLI flags is handled manually in `app.cpp` instead of being a first-class feature of the `draxul-config` library.

---

## 3. 10 Quality-of-Life (QoL) Features to Add

1.  **Config Hot-Reloading:** Watch `config.toml` for changes and automatically update font sizes, colors, and keybindings without restarting.
2.  **Command Palette:** An ImGui-based fuzzy finder (Cmd+Shift+P) to trigger GUI actions, switch hosts, or execute Neovim commands.
3.  **Integrated Tab Support:** A browser-like tab bar at the top to complement the current binary split system.
4.  **File Explorer Sidebar:** An optional, built-in sidebar for project navigation that works across all host types (Neovim or shell).
5.  **Log Viewer UI:** An ImGui window to view and search application logs in real-time, reducing reliance on external log files.
6.  **Remote Host Attachment:** Connect to a remote Neovim instance or shell session over SSH or a unix/tcp socket.
7.  **GPU Memory Profiler:** Show VRAM consumption and atlas residency statistics in the diagnostics panel.
8.  **Visual Keybinding Editor:** An interactive UI to view and modify keybindings, including conflict detection.
9.  **Status Line Customization:** Ability to customize the Draxul-level status overlay with custom colors, host info, and performance metrics.
10. **Integrated Update Checker:** Automated notification when a new version of Draxul is available for download.

---

## 4. 10 Tests to Improve Stability

1.  **Multi-Monitor DPI Hot-plug Test:** Verify the grid resizes and font shaping updates correctly when dragging the window between monitors with different scaling.
2.  **Large Paste Stress Test:** Simulate pasting 10MB+ of text and ensure the UI thread remains responsive via proper chunking.
3.  **Unicode Corpus Stress Test:** Render a wide range of complex scripts (Devanagari, Arabic, Emoji clusters) and verify correct HarfBuzz shaping.
4.  **Network Jitter Simulation:** For remote hosts, test the UI's resilience to high latency and dropped packets in the RPC layer.
5.  **Memory Leak Soak Test:** Run a long-duration smoke test with randomized input for 1 hour and verify memory usage remains flat.
6.  **Atlas Reset/Overflow Test:** Force the glyph atlas to fill up and verify it resets or expands without corrupting existing text.
7.  **Concurrency Race Test:** Rapidly resize the window while the host process is sending a high volume of redraw events.
8.  **Font Fallback Performance Test:** Ensure that a document with many missing glyphs doesn't cause a performance hit during the fallback search.
9.  **Startup Performance Regression Test:** A CI-gated test that fails if `App::initialize()` exceeds a specific time budget (e.g., 200ms).
10. **IME Stress Test:** Verify that complex IME inputs (e.g., Japanese/Chinese) are correctly forwarded and handled by the active host.

---

## 5. Worst 10 Features (Currently)

1.  **MegaCity (Hardcoded Integration):** Its deep integration into the app and renderer makes the core project harder to maintain and slower to build.
2.  **Binary Split Tree Only:** Lack of a tabbed interface or floating windows as top-level containers is a significant limitation for power users.
3.  **Fixed-size `CellText` Buffer:** The 32-byte limit is a "ticking time bomb" for future Unicode standard additions or complex clusters.
4.  **Manual Sync Primitives in Renderer:** Forcing backends to manage their own frames-in-flight synchronization is error-prone and leaky.
5.  **Heuristic Settle in Render Tests:** The use of arbitrary timeouts in the test runner is the primary source of CI flakiness.
6.  **Hardcoded Platform Defaults:** e.g., hardcoded Zsh on macOS and PowerShell on Windows for splits, which should be user-configurable.
7.  **Bloated `AppOptions` Struct:** The struct is becoming a dumping ground for every possible CLI flag and initialization parameter.
8.  **No GUI-side Search in Terminal Hosts:** Terminal hosts lack basic features like searching through scrollback, relying entirely on the shell.
9.  **ImGui Underutilization:** ImGui is currently limited to "Diagnostics Only," missing opportunities for a more integrated GUI experience.
10. **Lack of Proper Headless Mode:** While smoke tests exist, there is no formal "headless" renderer target that could run on Linux CI without a display server.
