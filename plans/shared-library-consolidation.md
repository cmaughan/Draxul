# Shared-library consolidation — seven slices

**Date:** 2026-08-16 · **Card:** kanban `done/38 shared-library-consolidation -refactor.md`
**Status:** all seven slices landed 2026-08-17; Windows acceptance tracked in
`kanban/pending/39 windows-consolidation-acceptance -test.md`.
**Input:** [duplication-audit-2026-08-15.md](duplication-audit-2026-08-15.md)

Retire the audited duplication by giving each duplicated concern exactly one
home: three new `Draxul::PluginSupport::*` leaves, two promotions of existing
core libraries to the plugin allowlist, extensions to `draxul-types`,
`draxul-plugin-support`, and `draxul-protocol`, one new host base class, and a
shared-shader-include mechanism. Every slice leaves core and all four plugins
building, smoke-clean, and ends at an observable acceptance gate. Slices that
touch a product follow the submodule workflow: commit + push in the product
repo, then one deliberate pointer-bump commit here.

Audit bug numbers (#1–#14) refer to the table in the audit doc. Each is fixed
in the slice that makes its recurrence structurally impossible — not before,
so we never fix the same bug twice.

---

## Slice 1 — Foundations: `draxul-types` absorbs the strays and ships with the SDK

**Goal:** one home for the small helpers every later slice needs, and a types
package the standalone ScoreView build can see.

- `string_util.h`: canonical `trim`/`lowercase` with `string_view` overloads;
  delete the copies in `draxul-agent` (×2), markdown (×2), kanban store.
  Preserve kanban's charset behaviour only if a test proves it matters.
- `input_types.h`: `normalize_modifiers` / `has_only_modifiers` (lock-mask
  aware); delete the four copies. **Fixes #4** (Markdown navigation dead with
  Caps/Num Lock) — add the regression test in markdown's suite.
- `log.h`/`string_util.h`: one `describe_text_for_log`; delete the four copies.
- `process_util.h`: `spawn_detached()` (double-fork + pre-built argv + devnull,
  promoted from `ServerClient::launch_detached`), one wide+narrow arg-quoter,
  `resolve_exec_paths`, `build_child_environment(term)`,
  `current_unix_time_ms`, `process_start_token`. App's
  `server_status_surface.cpp` spawn copies are deleted, not adapted. **Fixes #6.**
- `runtime_path.h`: `user_config_dir()/user_data_dir()/user_cache_dir()`;
  delete the four independent resolvers (two are byte-identical 38-line
  copies).
- `draxul-font`: one `blit_cluster_run_rgba()`; tooltip and
  `text_atlas_builder` both call it. **Fixes #14** (wide-glyph overlap in
  atlas labels).
- Packaging: add `draxul-types` (headers + lib) to the `draxul-plugin-sdk`
  install component. No consumer changes yet — Slice 3 needs it.

**Gate:** full ctest both platforms; new markdown lock-key test passes;
`external_plugin_sdk_smoke.py` proves the installed SDK now carries types.

## Slice 2 — One ImGui input/host core: `libs/draxul-imgui-core`

**Goal:** exactly one scancode→ImGuiKey table and one `IImGuiHost` in the
whole tree; a reusable ImGui lifecycle for plugins.

- New always-built `libs/draxul-imgui-core`: the complete 227-line translation
  unit (from `draxul-ui`) + `imgui_host.h` (from `draxul-renderer`).
  `draxul-ui`, `draxul-renderer`, and `plugins/support/imgui` consume it;
  delete the 91-line subset in `plugins/support/imgui` and the core originals.
  Expose to plugins via the existing `Draxul::PluginSupport::ImGui` alias.
- `plugins/support/imgui` grows `PluginImGuiContext` (RAII: create/flags/
  `IsSRGB`/docking/font/attach/frame-begin/shutdown) and `ImGuiInputBridge`
  (promote MegaCity's `megacity_host_panels.cpp:13-79` factoring).
- Product repos: MegaCity, SatView, ScoreView delete their hand-rolled
  lifecycle + key/mouse bridging and adopt the two classes. ScoreView also
  deletes its forked `sdl_imgui_input.*` and `imgui_host.h` now (rest of its
  fork falls in Slice 3). **Fixes #1** (dead keypad), **#2** (dead
  letters/F-keys in MegaCity/SatView panels), **#10** (missing `IsSRGB`/
  docking). Three product commits + one pointer-bump.

**Gate:** manual matrix — keypad, Ctrl+A/C/V, and F-keys work in every product
panel; render snapshots for all four product scenarios unchanged (except
ScoreView overlay gamma, which is expected to change — re-bless deliberately).

## Slice 3 — Unfork ScoreView: NanoVG promotion + shadow-header deletion

**Goal:** ScoreView links core's NanoVG and types instead of carrying copies;
the ODR hazard is gone.

- `libs/draxul-nanovg`: add the shader/asset-root seam (`set_shader_root()`;
  default stays `runtime_path`), verify no broad-core links, add
  `Draxul::PluginSupport::NanoVG` to the allowlist, and add it to the SDK
  install/extraction set alongside `plugins/support/imgui`.
- ScoreView repo: delete `product/draxul-score-canvas`'s vendored
  `nanovg_*`/`vma_impl.cpp`/shaders (~1,500 lines); link
  `PluginSupport::NanoVG` + `PluginSupport::VulkanResources`. Delete the
  remaining shadow headers (`events.h`, `log.h`, `input_types.h`, `host.h`) —
  link `PluginSupport::Types`; replace `HostContext` et al. with the
  `PluginRuntimeContext` family. **Fixes #3** (LogCategory/KeyEvent ODR).
- Trim the now-redundant pins from ScoreView's `Dependencies.cmake`.

**Gate:** Grieg ScoreView render snapshot on both platforms; the copied-tree
extraction smoke passes using only the installed SDK (now including types +
nanovg); `draxul-test-scoreview*` shards green.

## Slice 4 — Plugin adapter shell: `Draxul::PluginSupport::Adapter`

**Goal:** one implementation of the C-ABI plugin shell; each plugin's adapter
is one small dual-backend TU.

- First collapse SatView's and spinning-triangle's VK/Metal adapter twins into
  single TUs (`#if`-selected render entries — the MegaCity/ScoreView pattern).
  **Fixes #9** (VK config-parse iterator pair from two string literals) by
  deletion. ~1,200 lines gone, mechanical.
- New `Adapter` leaf in `libs/draxul-plugin-support`: result factories +
  frame-delay constants, typed pane-state load/save over `HostServices`,
  config-JSON parse, action-table registrar
  (`action_count`/`action_at`/`query_extension`), `kApi` assembly,
  `ensure_allocator`. Allowlist entry. All four plugins adopt; drift guards
  (SatView's `service_path` bounds checks) become the canonical behaviour.
- Delete the dead, weaker `HostServices::ui_style()`; `UiStyleClient` stays
  the one implementation.
- Four product-side commits + pointer bumps (spinning-triangle is in-repo).

**Gate:** all four plugins list, launch, save/restore pane state, and render
on both backends; `topology_cli_integration.ps1` and plugin ctest suites green.

## Slice 5 — Shared GPU plumbing: VulkanResources + shader includes + CameraInput

**Goal:** one implementation of the HDR/MSAA pipeline scaffolding and the
shader math both products duplicate; one camera-input layer.

- Extend `PluginSupport::VulkanResources`: `create_attachment`/`_view`,
  per-format `choose_scene_sample_count` with 4→2→1 fallback (SatView's —
  **fixes #7**), `load_shader`, `upload_image_immediate`, and an
  `HdrScenePipeline` owning the MSAA→resolve→tone-map pass trio with one set
  of subpass dependency masks. MegaCity + SatView adopt; MegaCity's
  `inv_view_proj` divergence resolved by a shared `build_frame_uniforms`-style
  convention (backend passes a `ClipConvention`).
- Shared shader includes: `tone_map_aces` and the atmosphere scattering math
  become single GLSL/MSL include files consumed by both products' shaders;
  extend the `shaders/contracts/` parity-test pattern to the shared constant
  blocks.
- Core renderer parity hardening while in the area: Vulkan adopts
  `grid_contract.h` (**fixes #11**) and the Metal scissor clamp gets its
  missing `std::max(0, …)` (**fixes #8**) with a regression test.
- New `PluginSupport::CameraInput` leaf: `OrbitKeyState` (one binding table,
  Ctrl+R guarded) + `DragSmoother` (MegaCity's inertia). Both products adopt;
  document the intended W/S semantics once.

**Gate:** deterministic render captures for MegaCity, BioView, SatView on both
platforms; forced 2×/1× MSAA fallback exercised in a test; camera keys behave
identically in both products.

## Slice 6 — Wire-boundary unification: `draxul-protocol` + client consolidation

**Goal:** server and client cannot disagree about topology, and the
control-plane policy exists once.

- Move topology→layout conversion into `draxul-protocol` (one TU, consumed by
  `session_topology_bridge` and `topology_projection`); the mapping carries
  `agent`/`agent_session`/`restore_policy` and one host-kind default.
  **Fixes #5.** Add the traversal free functions,
  `kTopologyMin/MaxSplitRatio` (replacing 10 hardcoded sites, clamp-vs-reject
  documented), `topology_id_serial()`, and the shared `full_grid_update`.
- `libs/draxul-client`: `ServerControlChannel` (envelope + transient-error
  retry + token refresh) replacing the two ~85-line `app.cpp` blocks and the
  byte-identical `request()`s; `RevisionPolledClient` base collapsing
  `AgentClient`/`TopologyClient`; private `request_with_checkpoint_retry` for
  the three session ops. `server_kernel` gains `resolve_session()` for its
  five preambles.

**Gate:** server/client integration + session tests green; a new round-trip
test proves an agent-owning pane restores identically via both conversion
paths (they are now the same path).

## Slice 7 — Terminal surface base + test-infrastructure consolidation

**Goal:** local and remote terminals share one surface layer; the test suite
stops re-implementing its own scaffolding.

- `TerminalSurfaceHostBase` in `libs/draxul-host` between `GridHostBase` and
  both terminal hosts: selection wiring, copy mode, mouse
  button/move/wheel/reporting, `pixel_to_cell`, `open_link_at`,
  copy-shortcut detection (via Slice 1's modifier helpers). Local's stricter
  guards become canonical; remote inherits `PERF_MEASURE` coverage.
  **Fixes #12.**
- Test infra: `tests/support/test_support.h` gains `bundled_font_path()` /
  `init_text_service()` (retiring the `__FILE__`-vs-`DRAXUL_PROJECT_ROOT`
  split), `wait_until`/`pump_until`, one `read_file` with an explicit failure
  contract, and `DRAXUL_SKIP_UNLESS_SLOW()`. Shared `tests/support/sdk_smoke.py`
  module unifying the two Python drivers — both gates get the ABI-leak check
  and the child-output dump (**fixes #13**). ScoreView repo: layout tests
  adopt the fixture's engine fake; `synthetic_piano.h` moves from core
  `tests/support/` into the scoreview repo beside `wav_reader.h`. Merge the
  duplicated `startup_config`/`config_lifecycle` TEST_CASEs.

**Gate:** full ctest both platforms; a local/remote parity test asserts
identical copy-on-select and copy-mode behaviour; test-inventory baseline
updated deliberately for the merged cases.

---

## Sequencing constraints

1 → 3 (installable types) and 1 → 7 (modifier helpers) are hard edges;
2 → 4 only in that both touch plugin runtimes and 2's bug fixes should land
first; 4 → 5 is soft (5 touches the render halves 4 leaves behind). 6 and 7
are core-only and can proceed in parallel with 3–5 if needed.

Every slice: build + smoke before commit, full ctest at the gate, product-repo
commits pushed before the Draxul pointer bump, and `docs/features.md` /
`docs/module-map.md` updated in the slice that changes the library surface.
