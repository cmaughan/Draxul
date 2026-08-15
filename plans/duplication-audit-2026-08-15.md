# Code duplication audit — 2026-08-15

Full-tree audit of core (`app/`, `libs/`, `modules/`, `sdk/`, `tests/`) plus the
three product submodules and `plugins/{support,spinning-triangle}`. Method:
content-hash pass for exact copies, same-basename similarity scan, then four
targeted sweeps (Vulkan↔Metal pairs, cross-plugin scaffolding, core internals,
test infrastructure) with normalized clone-window detection over ~99k core lines.

Ballpark total: **~5,500–6,000 duplicated lines**, concentrated in a dozen
clusters. The important result is not the volume — it is that **at least ten of
the copies have already drifted into behavioural divergence**, which is exactly
the failure mode duplication causes.

## Confirmed divergence bugs (fix these regardless of any refactor)

| # | Bug | Where |
|---|-----|-------|
| 1 | **Numeric keypad + lock keys dead in ScoreView ImGui fields** — its forked scancode map dropped 22 cases the shared copy has | `plugins/scoreview/product/draxul-score-runtime-support/src/sdl_imgui_input.cpp:9` vs `plugins/support/imgui/src/sdl_imgui_input.cpp:39-59` |
| 2 | **Letter/digit/F-key events likely dead in MegaCity/SatView ImGui** — the shared `plugins/support` map is itself a 91-line subset of core's full 227-line switch (`libs/draxul-ui/src/sdl_imgui_input.cpp`); ScoreView's fork added those ranges back but lost the keypad. Three forks, each a different incomplete subset of the core original | `plugins/support/imgui/src/sdl_imgui_input.cpp` |
| 3 | **ODR hazard**: ScoreView redefines `draxul::LogCategory` (8→3 enumerators, `Renderer`=5 upstream vs 1), `KeyEvent`, etc. under the same include paths/namespace; `draxul-test-scoreview` links both definitions | `plugins/scoreview/product/draxul-score-runtime-support/include/draxul/log.h`, `events.h` vs `libs/draxul-types` |
| 4 | **Markdown navigation dies with Caps/Num Lock on** — kanban's `has_only_modifiers` masks lock keys, markdown's copy does not | `modules/markdown/draxul-markdown/src/markdown_navigation.cpp:27` vs `modules/kanban/draxul-kanban/src/kanban_navigation.cpp:27-31` |
| 5 | **Client topology projection drops agent identity** (`agent`, `agent_session`, `restore_policy`) that the server-side copy of the same conversion preserves; the two sides also default host kind differently | `libs/draxul-client/src/topology_projection.cpp:288-296` vs `libs/draxul-server/src/session_topology_bridge.cpp:135-150` |
| 6 | **App-side detached spawn reintroduces two documented bugs** — single-fork (zombie child) and post-fork heap allocation — that `ServerClient::launch_detached` carries explicit fix comments for | `app/server_status_surface.cpp:78-147` vs `libs/draxul-client/src/server_client.cpp:1075-1200` |
| 7 | **MegaCity MSAA probe is unsafe** — checks device limits only, never per-format support, and has no 2× fallback rung; SatView's copy of the same pipeline does both | `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_vk_resources.cpp:277-286` vs `plugins/satview/src/render/satview_render_vk.cpp:126-141` |
| 8 | **Metal scissor clamp missing `std::max(0, …)`** in one of its own two copies — negative pane x casts to a huge `NSUInteger` | `libs/draxul-renderer/src/metal/metal_renderer.mm:897-900` (the copy at `:997` has the guard) |
| 9 | **Spinning-triangle VK config parse builds begin/end from two separate `"{}"` literals** (UB-adjacent); the Metal copy fixed it | `plugins/spinning-triangle/src/spinning_triangle_vk.cpp:344-348` vs `_metal.mm:224-229` |
| 10 | **ScoreView ImGui context missing `IsSRGB` (gamma mismatch vs the other panes) and docking flag** | `plugins/scoreview/product/draxul-scoreview/.../score_runtime.cpp:280-283` |
| 11 | `grid_contract.h` push-constant ABI guard adopted by Metal only; Vulkan writes a bare `float[7]` and takes screen size from a different source | `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:1094-1102` |
| 12 | Remote terminal host drift vs local: missing key-release guard in copy-mode, `copy_on_select` condition differs, wheel clears selection only remotely, and zero `PERF_MEASURE` coverage | `libs/draxul-host/src/remote_terminal_host.cpp` vs `local_terminal_host.cpp` |
| 13 | SDK smoke harness asymmetry: only core's copy checks the installed header for ABI leaks; only ScoreView's copy prints child output on render failure | `tests/external_plugin_sdk_smoke.py` vs `plugins/scoreview/tests/external_product_plugin_smoke.py` |
| 14 | Tooltip vs text-atlas glyph blit drift: only the tooltip path advances wide chars correctly — CJK/emoji overlap in atlas-built labels | `libs/draxul-plugin-support/src/tooltip.cpp:33-118` vs `libs/draxul-font/src/text_atlas_builder.cpp:118-200` |

## Cluster ranking

### A. ScoreView's vendored fork of core (~1,800 lines; bugs #1, #3, #10)

Exact byte-identical copies: `nanovg_wrapper.c`, `nanovg_mtl.h`,
`nanovg_mtl_shaders.h`, `vma_impl.cpp`, `shaders/nanovg.{vert,frag}`,
`sdl_imgui_input.h`. Near-identical (95–100%): `nanovg_mtl.mm`,
`nanovg_vk.{h,cpp}` (only shader-root resolution differs — a one-flag seam).
Diverged shadow headers under the same `draxul/` paths: `events.h`, `log.h`,
`input_types.h`, `imgui_host.h`, `host.h` (a rename of `plugin_runtime.h`
types). Fix: delete the fork; link `Draxul::PluginSupport::Types`, `::ImGui`,
`::VulkanResources` (all already allowlisted); make `draxul-types` extractable
for the standalone build the way `plugins/support/imgui` already is.

### B. Plugin C-ABI adapter shells — six near-clone TUs (~1,100–1,200 lines; bug #9)

`satview_plugin_vk.cpp`/`_metal.mm` (487 identical lines),
`spinning_triangle_vk.cpp`/`_metal.mm` (391), plus 279 identical lines between
*different plugins'* adapters. Result factories, `service_path`, saved-state
JSON, `create_instance`, input dispatch, action tables, `kApi` — five of these
jobs already exist in `HostServices`
(`libs/draxul-plugin-support/src/plugin_host_services.cpp`) but only MegaCity
uses it, and only for `path()`. Fix in two steps: collapse each VK/Metal pair
to one dual-backend TU (MegaCity/ScoreView already prove the pattern), then
extract a `PluginSupport::Adapter` shell. Also: `HostServices::ui_style()` has
zero callers and is a weaker duplicate of `UiStyleClient` — delete it.

### C. MegaCity ↔ SatView HDR/MSAA/tone-map pipeline (~450 lines C++, ~200 shader; bug #7)

Same pipeline shape (MSAA RGBA16F + D32 → resolve → ACES → BGRA8-sRGB), zero
shared code: attachment creation, render-pass trio, tone-map pipeline,
staging uploads, `tone_map_aces` copied four ways, atmosphere scattering math
copied three ways (GLSL×2 + MSL). Subpass dependency masks already disagree.
Fix: extend `PluginSupport::VulkanResources` (take SatView's sample-count
probe — it is strictly safer) + shared shader includes; the repo's
`shaders/contracts/` + parity-test mechanism is the right guard rail to extend.

### D. ImGui hosting lifecycle ×3 runtimes (~250 lines; bugs #1, #2, #10)

Context create/flags, `set_imgui_font`, key/mouse/text bridging, frame begin,
shutdown — hand-rolled in MegaCity, SatView, and ScoreView with three different
flag sets. Fix: `PluginImGuiContext` RAII + `ImGuiInputBridge` in
`plugins/support/imgui` (MegaCity's `megacity_host_panels.cpp:13-79` is the
cleanest existing factoring — promote it), and make the input map delegate to
one full table (core's 227-line switch is the only complete one).

### E. Local ↔ Remote terminal host (~400 lines; bug #12)

The whole terminal-surface layer — selection, copy mode, mouse
reporting/wheel/links, `pixel_to_cell` — written twice because
`RemoteTerminalHost` inherits `GridHostBase` directly. Fix: a
`TerminalSurfaceHostBase` between `GridHostBase` and both hosts; local's
stricter guards become canonical.

### F. Server/client split duplication (~600 lines; bug #5)

- Topology→layout conversion on both sides (`session_topology_bridge.cpp` vs
  `topology_projection.cpp`) — move one copy into `draxul-protocol`.
- `AgentClient` and `TopologyClient` are ~70% the same class (byte-identical
  `request()`); the retry/token-refresh envelope is also copy-pasted twice at
  ~85 lines each in `app.cpp:4489,4678` → one `ServerControlChannel`.
- `ServerClient` checkpoint-busy retry ×3 (`server_client.cpp:796-905`).
- `server_kernel.cpp` session-resolution preamble ×5.
- `full_grid_update` byte-identical in client and host.

### G. Platform pairs in process land (~250 lines; bug #6)

- Agent-observer state machine duplicated ConPTY/Unix with different debounce
  constants and probe latency; no `IPtyProcess` seam exists.
- `resolve_exec_paths` byte-identical 34 lines in `nvim_process.cpp` and
  `unix_pty_process.cpp`; env-building near-identical.
- Detached spawn ×3 (bug #6) and Windows arg quoting ×3 → `process_util.h`.

### H. Camera keyboard latch layer (~140 lines)

MegaCity `codeviz_input_state.cpp` vs SatView `satview_camera_key_state.h` —
same binding table, drifted semantics (W/S pan vs orbit; only SatView guards
Ctrl+R). The camera *math* is genuinely different and should stay separate.
Fix: `PluginSupport::CameraInput` (latch table + MegaCity's drag inertia).

### I. Small helpers scattered through core (~500 lines)

`describe_text_for_log` ×4 · `trim` ×5 (kanban's trims a different charset) ·
`normalize_modifiers`/`has_only_modifiers` ×4+ (bug #4) · app-dirs resolution
×4 (two byte-identical 38-line copies in `app_config_io.cpp` and
`config_document.cpp`) · split-ratio bounds `0.1/0.9` hardcoded in 10 places
across 3 libraries (app clamps, server rejects) · `parse_duration_ms` ×2 ·
`lowercase` ×4 · `current_unix_time_ms` ×2 · `numeric_suffix` ×2. Natural
homes: `draxul-types` (`string_util.h`, `input_types.h`, `process_util.h`,
`runtime_path.h`) and `topology_protocol.h` for the topology constants.

### J. Build scaffolding (~130 lines + pins ×3)

Shader-compile loops re-implemented in 4 plugin CMakeLists (only SatView gets
include-dependency tracking right); the sanitizer/coverage foreach
byte-identical ×3; shader-payload mapping ×4; ScoreView re-pins SDL3, json,
glm, nanovg, imgui, VMA at the same tags as root (three places to
desynchronize). Fix: `draxul_plugin_compile_shaders()` +
`draxul_plugin_apply_build_policy()` in `cmake/DraxulPlugins.cmake` and a
shared `PluginDependencyPins.cmake`.

### K. Test infrastructure (~900 lines; bug #13)

- SDK smoke harness: two ~230-line Python drivers, half byte-identical, with
  asymmetric checks (bug #13) → `tests/support/sdk_smoke.py`.
- Font-path/TextService bootstrap: 33 helper definitions across 18 files (two
  conflicting repo-root strategies) → `tests/support/test_support.h`.
- 26 hand-rolled poll/wait loops; the right `wait_until` primitive already
  exists — in one ScoreView test file.
- `startup_config_tests.cpp` vs `config_lifecycle_tests.cpp`: three whole
  TEST_CASEs duplicated with different sentinel integers.
- ScoreView `scoreview_layout_tests.cpp` re-implements its own fixture's
  layout-engine fake (~80 lines, two byte-identical helpers).
- `read_file` ×11 with three different failure behaviours; slow-test gate
  ×33 in one file; SatView fixture payload literals ×5 files (keep the real
  reference TLEs inline — they are data, not duplication).
- Also: `tests/support/synthetic_piano.h` is ScoreView-only and belongs in the
  scoreview repo; megacity's fixture uses `#define private public` where the
  other two products use declared friend seams.

## Verified clean (worth defending)

- Unicode/grapheme width: single implementation (`draxul/unicode.h`), no second wcwidth.
- Color conversion, DPI scaling (`pixel_scale.h`), clipboard, retry backoff: single-sourced.
- No header duplicated between any lib's `src/` and `include/draxul/` (the documented SonarCloud pitfall is currently absent — though `draxul-plugin-support` and `draxul-gui` both publish into `draxul/gui/`, which is the setup that produces it later).
- Core Vulkan↔Metal renderer bodies: zero clone runs — that platform seam is genuinely thin (`RendererState`, `pending_atlas_upload.h`, `grid_contract.h` did their job where adopted).
- One `test_main`, one shard bootstrap, plugin tests genuinely reuse core fakes.
- TOML config persistence: both products already route through shared `toml_support`.

## Suggested order of attack

1. **Bug pass** (items #1–#14 above) — each is a small, independent diff.
2. **Cluster A** — delete ScoreView's fork; removes the ODR hazard and ~1,800 lines.
3. **Cluster B step 1** — collapse SatView/spinning-triangle to single dual-backend TUs (~1,200 lines, mechanical, pattern already proven in-tree).
4. **Clusters I + H small-helper consolidation** into `draxul-types` — cheap, kills bug #4's class of drift.
5. **Cluster F** (`ServerControlChannel` + shared topology conversion) — protects the wire boundary.
6. **Cluster E** (`TerminalSurfaceHostBase`) — biggest single-seam win in core.
7. **Clusters C, D, J, K** as the files come under active development.
