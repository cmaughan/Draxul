# Draxul Project Learnings

100+ lessons learned building a GPU-accelerated Neovim frontend and 3D code-city visualizer, 100% agentically coded across Claude, Codex, and Gemini.

---

**Architecture & Module Design**

1. Split the codebase into small focused libraries early — Draxul's `libs/` structure paid off as the project grew fast with multiple agents working in parallel.
2. Keep the app layer as thin wiring only — `app/app.cpp` became the main merge hotspot when too much policy accumulated there.
3. Public headers go in `include/draxul/` (angle brackets), internal headers in `src/` (quotes) — never duplicate a header between the two or they silently diverge.
4. Do not include backend-private renderer headers from `app/` — the renderer boundary exists precisely to prevent this coupling.
5. Renderer interface layering (`IBaseRenderer` → `I3DRenderer` → `IGridRenderer`) lets grid hosts and 3D hosts compose naturally without knowing about each other.
6. The `IRenderPass::record(IRenderContext&)` abstraction is minimal and correct — any subsystem can register a pass without touching renderer internals.
7. Host hierarchy (`IHost` → `I3DHost` → `IGridHost` → `GridHostBase`) keeps terminal and 3D hosts cleanly separated.
8. When a class exceeds ~50 member variables it has become a god object and needs decomposition.
9. Prefer straightforward data flow over flexible abstractions — an explicit `build_scene_snapshot()` returning a POD-like struct is easier to debug than a live scene graph.
10. Keep Megacity types local to `draxul-megacity` unless another module genuinely needs them — premature publication creates unwanted coupling.
11. Use GLM for all vector and matrix types — do not introduce custom math structs alongside it.
12. Two separate capability-detection patterns in one codebase (SFINAE vs `dynamic_cast`) creates confusion — pick one idiom.
13. `dynamic_cast` for host capability probing is a code smell — prefer explicit virtual methods or capability queries.
14. Do not build a second standalone engine inside the repo — evolve existing subsystems incrementally.

**Rendering**

15. For a terminal grid renderer, instanced procedural quads with no vertex buffers gives O(1) draw calls regardless of grid size — 2 draw calls for a 120×40 terminal.
16. Host-visible/shared GPU memory with direct writes (no staging) is correct for terminal grid data — it's small and changes frequently.
17. Dirty-cell tracking avoids redundant text shaping and glyph rasterization, which is where the real CPU cost is.
18. BGRA8 Unorm (not sRGB) is correct when Neovim sends colors already in sRGB — using an sRGB format would double-encode.
19. Two frames in flight with explicit synchronization is standard for both Vulkan and Metal — but Metal historically had only 1 frame in flight, causing CPU-GPU stalls.
20. When rendering layered geometry with overlapping extents (roads, sidewalks, buildings), separate passes by layer priority rather than by entity.
21. Floor grid geometry should use transient streamed slices rather than creating a fresh GPU mesh each time the camera changes.
22. Distinguish three kinds of work: semantic world rebuild (expensive, rare), per-frame scene extraction (moderate), and GPU render submission (cheap).
23. `SceneObject`s are lightweight CPU draw records, not mesh assets — "rebuilding SceneObjects" is mostly matrix work, not geometry generation.
24. For point-light shadows, the explicit per-face 2D texture path was introduced because cubemap sampling had cross-backend orientation ambiguity.
25. Do all lighting math in linear space; let the final sRGB render target do the conversion — do not manually gamma-encode on top of an sRGB target.
26. The `material_id` branch in the forward shader is about surface evaluation (UV, TBN), not different lighting models — the shared lighting path stays the same.
27. Prefer persistent mapping for dynamic CPU-written GPU data and per-frame transient arenas for dynamic vertex/index/uniform data.
28. Vulkan packs all grid handles into one shared SSBO; Metal creates one MTLBuffer per handle — the inconsistency is a known tradeoff, not a bug.

**Font Pipeline & Text Rendering**

29. Color emoji is a pipeline feature, not a single switch — FreeType must preserve BGRA, glyph cache must keep RGBA, atlas uploads must support RGBA, and shaders must avoid tinting color glyphs.
30. CJK tofu on Windows was a fallback-font coverage problem, not a shaping problem — check fallback coverage before debugging HarfBuzz.
31. Bundled fonts and installed system fonts can diverge — a stale bundled Nerd Font makes the app look broken even when the machine has the right glyphs.
32. Font-size changes must relayout existing grid geometry even before Neovim acknowledges a resize — the visual update must be immediate.
33. The `FT_Face` lifetime hazard (GlyphCache holding a raw pointer to a freed face) was a real use-after-free — a generation counter on TextService was the smallest fix.
34. Using raw `FT_Face` pointers as hash keys means a reallocated face at the same address silently collides with old entries.
35. Draxul renders fonts at the display's true physical DPI (e.g. 220 PPI on Retina) — an 11pt font in Draxul looks like ~15pt at 72 DPI.
36. Bold/italic resolution via filename substitution (`-Regular` → `-Bold`) works for bundled fonts but breaks for arbitrary system fonts — use OS font APIs for a real font picker.
37. The CellText 32-byte inline buffer silently truncates multi-codepoint emoji clusters exceeding 31 bytes.
38. Atlas reset blocks the main thread synchronously — for a warm atlas after rendering a large file, this can stall long enough for nvim notifications to pile up.

**Cross-Platform**

39. Environment variables like `DRAXUL_LOG_FILE` do not propagate into macOS `.app` bundles — prefer CLI flags that work reliably everywhere.
40. Shader decoration constants must live in one shared file — two platform-specific copies diverged silently and produced rendering bugs.
41. Metal uses `ndc.y = -ndc.y` for Y-up; Vulkan flips `proj[1][1]` — keep these in the backend, not in shared code.
42. `poll()` on Unix returns `-1` with `errno == EINTR` on any signal (SIGWINCH, SIGCHLD) — treating any non-positive return as fatal permanently silenced the PTY reader thread on window resize.
43. `write()` on Unix must retry on EINTR or keypresses and pastes are silently dropped during signal delivery.
44. PTY shutdown requires correct fd close ordering — closing the master fd before joining the reader thread creates a race condition.
45. Windows process spawning uses `CreateProcess` with piped stdin/stdout; macOS uses `fork()`/`exec()` with `pipe()` — these need entirely separate implementations.
46. `compile_commands.json` requires a Ninja build — use a separate `clang-tools` preset for tooling that needs it.

**Testing & Validation**

47. Visual regression tests should capture pixels from the renderer output (swapchain readback), not from the desktop — compositor noise and DPI scaling make desktop screenshots non-deterministic.
48. Once a UI project has reliable capture, diff, and bless mechanics, even small regressions become cheap to spot and repair.
49. Hero screenshots need a separate path from deterministic render tests — the hero uses the user's real Neovim config; regression tests use `-u NONE --noplugin`.
50. The render scenario TOML loader originally only handled single-line arrays and silently failed on multi-line arrays — always test the config format against real usage.
51. Plugin-driven screenshot actions should call plugin APIs directly rather than replay key mappings — simulated keypresses are unreliable when mappings are lazy-loaded.
52. Replay fixtures (`tests/support/replay_fixture.h`) are the preferred way to reproduce UI parsing bugs without launching Neovim.
53. Always build and run the smoke test before committing — it catches broken includes, link errors, and basic startup failures.
54. Add focused geometry tests: vertex/index counts, monotonic branch radii, monotonic UV V coordinates, unit-length normals, orthogonal tangents, byte-identical output for identical seeds.
55. Tests under ASan/UBSan catch real bugs that code review misses — the PTY EINTR and scrollback stale-stride bugs were ASan-detectable.

**Build System**

56. CMake FetchContent for all dependencies removes external setup friction but means builds pull a lot of code.
57. A root-level `do.py` with short single-word commands (`smoke`, `basic`, `blessall`, `test`, `review`, `consensus`) lowers friction for using safety nets.
58. The pre-commit hook running `clang-format` means the first commit attempt may fail — re-stage the reformatted files and retry.
59. `r.bat` with `--ninja` for Ninja Multi-Config is much faster than the default Visual Studio generator for development iteration.
60. `DRAXUL_ENABLE_MEGACITY` as a CMake option lets the 3D host be excluded from production builds.
61. `cmake/CompileShaders.cmake` should glob `*.h` files as shader include dependencies, not just `*.glsl` — otherwise changing a shared header skips shader recompilation.

**Neovim RPC & Terminal Emulation**

62. RPC write serialization is critical — `NvimRpc::request()` and `notify()` from different threads without a write mutex corrupt msgpack frames mid-stream.
63. The 5-second RPC timeout is a magic number — on slow machines or during large indexing, it produces spurious timeouts indistinguishable from genuine hangs.
64. The notification queue silently drops after 4096 entries with no backpressure — if the main thread stalls, nvim output is lost.
65. `compact_attr_ids()` must scan scrollback buffer cells in addition to main grid and alt-screen cells — otherwise scrollback loses syntax colors after compaction.
66. `restore_live_snapshot` must use `min(cols, live_snapshot_cols_)` — using the snapshot's column count after a resize produces cross-row corruption.
67. All grid and GPU state must only be touched by the main thread — the reader thread communicates via a thread-safe queue plus SDL wake event.
68. Keep shutdown paths non-blocking — a stuck Neovim child must not hang the UI on exit.
69. MPack must be built with `MPACK_EXTENSIONS=1` for Neovim's ext types (Buffer/Window/Tabpage).
70. Terminal tab stops are hardcoded at 8 spaces — a known limitation.

**AI / Agent Collaboration**

71. None of the code in Draxul has been human-written — it is 100% agentically coded across Claude, Codex, and Gemini, representing several person-years of effort in under 3 weeks.
72. Running multiple agents via `isolation: "worktree"` gives each agent its own repo copy — the integration step is where the main agent earns its keep by reasoning about intent across all diffs.
73. When one agent moves code and another modifies the same code at its original location, the integrating agent can transplant the modification to the new location automatically.
74. AI agents suggest features in stages then criticize the result — establish scope clearly upfront to avoid this suggest-then-criticize cycle.
75. AI agents duplicate logic — compute values once in the data model, not per consumer; watch for this pattern constantly.
76. Use DEBUG not INFO for diagnostic log statements — with AI-assisted development, adding/removing logs is trivial so there's no reason for verbose spew at INFO level.
77. For renderer bugs, user-supplied logs are better than agent-generated ones — the user can drive the real interaction path that triggers the bug.
78. Start the debugger immediately when a renderer change causes a startup crash — stop speculating from logs and get the exact trap site first.
79. Work items as markdown files with investigation checkboxes, fix strategy, acceptance criteria, and interdependencies give any agent enough context to go do the work.
80. Use `docs/features.md` as the canonical reference for what is already implemented — check before proposing new work items.
81. Multi-agent reviews work best when synthesized as a consensus document attributing points to each agent and flagging agreements, disagreements, and stale items.
82. Bug-focused reviews should be separated from general code reviews — the bug review prompt explicitly excludes style, naming, and architecture concerns.
83. Store prompts that produce good results under `plans/prompts/` for repeatable generation.
84. Sub-agent recommendations in work items are valuable — one agent on transport, one on app-side callsites, merge only after the contract is settled.

**Process & Workflow**

85. A running learnings commentary is worth keeping — update it as work happens, not as a post-hoc writeup; small discoveries get forgotten once the bigger feature is done.
86. Number collisions in work items across planning waves are inevitable — always reference items by full filename, not number alone.
87. Work items in `plans/work-items/` can be synced to GitHub project board via `python do.py syncboard` — the script is idempotent.
88. When you complete a work item, tick the checkboxes and move it to `plans/work-items-complete/` in the same turn.
89. The CMake dependency graph and class diagram are the two most useful structural views — regenerate them when architecture changes.
90. SVG architecture diagrams are cheap to regenerate with Claude — treat them as living documents.
91. Human-facing structure tools (dependency graphs, class diagrams, `do.py`, render snapshots) are not documentation polish — they control complexity.
92. If the issue is about what Neovim sent, start in `draxul-nvim`; if about what the screen shows, start in `draxul-grid`; if about how it's drawn, start in `draxul-renderer`.

**MegaCity / Code City Visualization**

93. MegaCity is an agent-management and code-analysis host, not a demo — it provides a 3D city metaphor for codebase structure with live performance and coverage overlays.
94. The city DB is a derived cache, not a source of truth — schema migrations can be destructive because data is always rebuildable from Tree-sitter scans.
95. Type identity in the city DB is still based on simple names, not namespaces — duplicate type names in different scopes can collide.
96. Normalized render metrics (`sqrt(base_size)` for footprint, `log1p(function_mass)` for height) compress outliers while preserving ordering.
97. Free functions are shown as trees rather than buildings — a different visual metaphor with a separate `tree_height` formula.
98. The procedural building generator uses rings of quads per floor section (3 quad strips per level) rather than stacked cubes — eliminates inner quads and allows per-level ripples.
99. For the tree generator, use hash-derived RNG streams per branch instead of one mutable global RNG — makes recursion deterministic and thread-friendly.
100. Do not start with ECS — build a small explicit world model first, then consider ECS only inside `draxul-megacity` and never let `entt::registry` leak into host/render interfaces.
101. The 2D occupancy grid debug panel (City Map) was essential for finding three rasterization bugs — visual side-by-side comparison made each bug obvious.
102. `RoadSegmentPlacement::extent` stores full widths, not half-extents — the renderer passes `extent_x` directly to `glm::scale()` on a unit cube `[-0.5, +0.5]`.

**Procedural Geometry**

103. Do not start with half-edge mesh structures for procedural tree generation — the core problem is branch skeleton plus ring stitching, not generic mesh editing.
104. Separate branch descriptor generation (light, deterministic) from mesh emission (parallelizable per-branch) for safe threading.
105. For bark UVs, `U` wraps around circumference, `V` runs along branch length — child branches continue their own local V, not a global unwrap.
106. The geometry library (`draxul-geometry`) should stay renderer-agnostic and generate CPU mesh data only.

**Performance**

107. Treat fence waits, upload flushes, and queue submissions as first-class performance costs that must be measured — do not assume they are free.
108. Blocking waits at the start of the Vulkan frame loop are a current suspect for sticky motion during camera pans.
109. Atlas upload synchronization can wait for multiple in-flight frames and is another suspect for frame hitches.
110. `std::function` callbacks in character-frequency paths (VT parser, alt screen, selection) have unnecessary type-erasure heap allocation since concrete types are known at construction.

**Specific Bugs & Pitfalls Discovered**

111. `static_cast<I3DRenderer*>` downcast compiles silently if the hierarchy changes but becomes UB — use `dynamic_cast` with null check.
112. `int64_t` to `int` narrowing in `try_get_int` for grid coordinates is a maintenance trap — add range checking.
113. BMP reader/writer had signed integer overflow UB in `write:46` and `read_u32:97`.
114. SDL file dialog's async callback could use-after-free the `SDL_Window*` if the window was destroyed during the async operation.
115. Unsigned underflow in performance timing EMA corrupted the timing display.
116. `vkCreateBuffer(size=0)` on empty mesh input is a Vulkan spec violation — check for empty input before creating buffers.
117. Float equality comparison for DPI relayout misses tiny DPI changes — use epsilon comparison.
118. The 256 overlay cell capacity in RendererState is a magic number with no static_assert — excess overlay cells are silently ignored.
119. An off-by-one at snapped cell boundaries in the occupancy grid required epsilon inset on max edges — `floor()` on exact boundary values includes one extra cell.
120. The three-pass global drawing order (all roads, then all sidewalks, then all building footprints) was necessary because per-entity layering failed when buildings shared road corridors.
121. PlantUML PNG output is pixel-limited and clips large diagrams — always use `-tsvg`.
122. The wait-timeout polling at 50ms is a safety net that masks the need for more robust event-driven wakeups.
123. `road_size` (dependency count) is heuristic and depends on Tree-sitter type-like name capture — directionally useful but not precise.
124. Chord-style prefix keybindings need careful state machine testing — partial chord cancellation and timeout behavior are common regression sources.
125. The diagnostics panel (F12) does not intercept keyboard input — the terminal remains fully interactive while it is visible.
