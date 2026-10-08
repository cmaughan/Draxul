# Release diagnostic textures through their owning pane
**Summary:** Release each pane’s diagnostic images through their owner so closing one pane cannot corrupt another pane’s drawing state.

**Priority:** P0  
**Source:** `plugins/support/imgui/src/plugin_imgui_context.cpp`  
**Reported by:** Claude C3 and C4; consensus F04.

**Evidence and trigger:** MegaCity and SatView destroy scene passes before selecting their owning context. Shared Vulkan removal uses the current context’s descriptor pool. SatView additionally permits null-backend removal after another hidden pane creates a context.

**Related:** `kanban/pending/26 plugin-imgui-vulkan-api -refactor.md`; `plugins/satview/kanban/pending/08 satview-resize-target-lifetime -perf.md`.

- [x] **Investigate:** Trace texture registration/removal ownership, context changes, scene teardown, resize retirement, and lazy backend initialization.
- [x] **Fix:** Provide scoped owner-context handling or explicit owned texture removal and apply it to both product callers.
- [x] **Fix:** Remove textures before their owning backend is destroyed and handle absent backend data safely.
- [x] **Acceptance:** Closing either of two rendered product panes removes descriptors through the correct pool.
- [x] **Acceptance:** Closing a diagnostic-enabled SatView pane while an unrendered pane’s context is current cannot dereference a null backend.
- [x] **Validation:** Run core/product aggregates appropriate to this shared seam, Vulkan validation, relevant multi-pane checks, and same-cache smoke; inspect Metal teardown alignment.

## Implementation notes (2026-10-02)

- Shared `ScopedImGuiContext` selects a resource owner's context and restores the
  caller's context, including null and nested scopes. Context destruction now
  preserves another pane's current context without restoring a destroyed one.
- MegaCity and SatView select their own context before releasing scene passes;
  scene destruction remains ahead of backend shutdown. Each Vulkan diagnostic
  target also remembers the context used for registration, so resize cleanup
  selects the right descriptor pool even when a different pane is current.
- The plugin Vulkan backend tolerates absent backend data for texture removal
  and registration; failed descriptor allocation no longer updates an invalid set.
- Added lifecycle coverage for another rendered/unrendered current pane, scene
  destruction context/backend ordering in MegaCity, retained current context after
  SatView shutdown, nested/null scopes, and teardown of a real plugin GPU host
  before its lazy backend receives any GPU frame.
- Metal diagnostic texture IDs refer directly to retained `MTLTexture` objects;
  there is no ImGui descriptor pool registration/removal. Its shared host teardown
  now uses the same owner-context ordering. macOS runtime checks are unavailable
  on this Windows host; no Metal-specific implementation gap was found.
- Root agent owns the shared-cache core/product aggregate, Vulkan/multi-pane
  validation, and smoke. No standalone or duplicate build was run for this slice.
- First aggregate exposed an invalid test that called raw Vulkan backend symbols
  in a binary linking both the host's vendor backend and the plugin backend. The
  linker selected the vendor implementation, which does not support absent data.
  Replaced that test with plugin-owned GPU host lifecycle coverage; product DLL
  runtime checks exercise the patched private backend. Vulkan API isolation is
  separately tracked in `kanban/pending/26 plugin-imgui-vulkan-api -refactor.md`.

## Native validation evidence (2026-10-02)

- Debug standalone SatView diagnostic fixture rendered the HDR/MSAA buffers and
  exited normally with code 0 in 32.7 seconds. MegaCity's diagnostic fixture
  rendered normals, depth, AO, HDR, and final scene textures and exited normally
  with code 0 in 8.2 seconds after using a shorter isolated cache root.
- Final isolated `texture-owners` session rendered two SatViews and both MegaCity
  modes together at 1100x760. `window-short.png` shows actual diagnostic images
  from the pane-local ImGui contexts. The native client exited normally with code
  0 after 75.0 seconds; its whole-client teardown produced no descriptor pool/set
  validation errors. Existing logs do contain an unrelated missing NVIDIA layer
  manifest and shader-output performance warnings.
- **Remaining GPU gate:** split resize and individual pane-close commands were
  issued after that client had already exited. Their server success is not
  evidence of live texture resize/individual-pane release. Keep the two-rendered-
  pane acceptance and overall validation unchecked until those transitions are
  observed while a rendering client stays attached. No additional retries were
  made after the final bounded attempt.
- Root aggregate's SatView lifecycle/context shard passed. Fake-backend lifecycle
  tests cover selecting the owner before scene destruction, preserving another
  current context, and closing an unrendered lazy backend. Metal was inspected;
  runtime validation remains unavailable on this Windows host.
- Evidence lives under the directory recorded in
  `build-ninja-debug/critical-validation-directory.txt`, in `texture-owners/`:
  diagnostic BMPs/logs, `window-short.png`, `exit-short.json`, topology mutations,
  and isolated child config/cache paths. The owned server PID 71920 was shut down
  through its exact runtime after confirming zero attached clients.
- A longer isolated cache root made MegaCity staging fail with an unavailable
  placeholder; shortening the root restored rendering. Follow-up belongs to
  `kanban/pending/96 plugin-generation-long-path-staging -bug.md`.

- Shared core/product aggregate completed: 79/84 CTest entries passed in 372.89s,
  including both MegaCity shards and SatView's lifecycle/context shard. The separate
  SatView catalog setup deadline is recorded in
  `plugins/satview/kanban/pending/15 catalog-seeder-test-deadline -bug.md` and passed
  serially with the original seed; PCBView's isolated render timeout is recorded in
  `plugins/pcbview/kanban/pending/02 pcbview-cancellable-autoroute -perf.md`.
  Other failures are the existing root cards for emoji shaping, scope membership,
  and remote initial-state timing. Same-cache Debug smoke and final Release startup
  passed after the final settings-source correction; no renderer/product source
  changed after the native checks above. Five core snapshots also passed (26.13s).
- Final settings-only core aggregate passed 53/56 CTest entries in 81.75s, with the
  three root failures unchanged. The 84-entry aggregate reused the Debug cache and
  built 10 incremental steps in 22.75s; native checks reused that executable without
  another build. No remote CI was run. Keep this card pending for live GPU resize
  and individual-pane closure.

## macOS re-check (2026-10-05)

- The core-side fix is present in the current tree: `ScopedImGuiContext` and
  `PluginImGuiContext::destroy()` select the owner context, shut the backend down, and
  never restore a destroyed context (`plugins/support/imgui/src/plugin_imgui_context.cpp`).
- Metal teardown re-inspected: `MetalGpuImGuiHost` has no descriptor pool; texture IDs
  are retained `MTLTexture` objects, and backend shutdown only runs while
  `initialized_`, after which the host destructor's repeat call is a no-op. No Metal gap.
- The remaining acceptance (two rendered MegaCity/SatView panes, live resize and
  individual pane close through the correct Vulkan descriptor pool) needs Windows/Vulkan
  plus the product submodules, which are not initialized in this macOS worktree. Card
  stays pending for that Windows gate; no code change was made.

## Windows gate preparation (2026-10-08)

- Parent owns the current Ninja Debug build and schedules exclusive GPU access.
  Inspection only so far: no build, client launch, reference mutation, or default
  runtime access. The earlier `--screenshot` run is not reusable as a live gate:
  that mode exits after capture, which explains the recorded post-exit mutations.
- Use a normal long-lived Debug client, a fresh short runtime/profile root, and
  child-only `APPDATA`, `LOCALAPPDATA`, `TEMP`, and `TMP` overrides. Pass the exact
  runtime and Session on every CLI call. Preserve independent client/server logs.
- Bounded scenario: render diagnostic-enabled SatView and MegaCity together,
  resize their split twice, close SatView while MegaCity remains rendered, then
  recreate the pair and close MegaCity first. Require actual diagnostic images
  before each close and rendered survivor evidence afterward; server topology
  mutation success alone is insufficient. SatView accepts
  `satview_config_toml = "show_hdr_debug_panel = true\n"`; MegaCity's visible
  fixed panels invoke its G-buffer debug UI.
- Around each mutation require the exact owned client process to remain alive,
  its native HWND to belong to that PID, and its exact `ui list` route to remain
  attached. Capture native-window images without using the auto-exiting
  `--screenshot` mode. Apply per-command timeouts and a total scenario deadline.
- Debug requests Vulkan validation and forwards callbacks to the app log. The
  installed SDK contains `VkLayer_khronos_validation.json` and its DLL; actual
  activation must still be established during execution. Retain all warnings,
  including the earlier unrelated NVIDIA loader warning, rather than filtering
  away evidence. Normal exact-client shutdown and exact-runtime server shutdown
  must retain exit codes and final validation output. Acceptance remains unchecked.

## Windows live gate result (2026-10-08): FAILED

- Actual Debug client PID `48880`, creation token `134359426909089713`, HWND
  `1839388`, UI route `ui-5c496e9c90769078f460ba2f`, Session `gate`; all files
  isolated below `C:/Users/cmaughan/AppData/Local/Temp/dgoqycwz_u/`. Runtime is
  `r/`, configuration `c/`, cache `l/`, and temporary plugin data `t/`.
- Rendered SatView `pane-12` beside MegaCity `pane-15` in `tab-14`, resized
  `node-13` to 0.4 then 0.6, and closed SatView individually. MegaCity remained
  rendered. Recreated SatView as `pane-18`, explicitly focused it, captured both
  rendered products again, and closed MegaCity individually. SatView survived.
  Every transition was bracketed by exact process-creation/HWND and UI-route
  liveness assertions; no post-exit server mutation is counted as GPU evidence.
- **The gate fails despite client survival:** `textures-city-closed.bmp` shows
  glyph-atlas imagery in SatView's MSAA/HDR diagnostic images. `client.log:44`
  begins invalid descriptor-set validation: `pDescriptorSets-parameter`,
  `pDescriptorSets-06563`, `vkCmdDrawIndexed-None-08600`, and
  `UNASSIGNED-Threading-Info`. These are real errors, not the unrelated NVIDIA
  manifest/performance warnings. The full log does not timestamp callbacks, so
  the precise first offending resize/close cannot be attributed from this run.
- `stderr.log` explicitly confirms insertion of `VK_LAYER_KHRONOS_validation`
  from the installed SDK. Both complete logs are retained. Screenshots include
  `textures-rendered.bmp`, `textures-resize-0.4.bmp`,
  `textures-resize-0.6.bmp`, `textures-sat-closed.bmp`,
  `textures-recreated-focused.bmp`, and `textures-city-closed.bmp`.
  `events.jsonl` preserves CLI requests/results and native liveness evidence.
  Stable copies of these artifacts are under
  `build-ninja-debug/windows-gates/core-20261008/dgoqycwz_u/`; runtime
  authentication directories were not copied.
- Normal client close returned **0**, captured through a retained process
  handle. Exact-runtime server shutdown returned 0; subsequent server-status
  reported no endpoint and no process with this runtime remained. No broad
  process termination, default profile access, build, or reference update.
- Keep both remaining checkboxes unchecked. Investigate the invalid/stale
  diagnostic descriptor path before repeating this gate; no production fix was
  attempted. Shared backend API isolation is also tracked in
  `kanban/pending/26 plugin-imgui-vulkan-api -refactor.md`.

## Read-only root-cause investigation (2026-10-08)

- **Onset limit:** the stable `client.log` has no timestamps, and this first
  probe did not record log offsets around mutations. `events.jsonl` records
  resize-to-0.4 at Unix `1791469391.73`, resize-to-0.6 at `1791469393.50`, the
  first close at `1791469395.25`, and the second close at `1791469458.66`.
  Therefore the saved artifacts do **not** prove whether the first invalid-set
  callback occurred before or after the first close. The initial log inspection
  before scripted resizes showed loader/performance warnings only. Resize
  captures have overlapping diagnostic panels and do not resolve the onset.
  Do not relabel the observed run as a proven close-only regression.
- **Source-confirmed lifetime gap, separate from selecting the wrong owner:**
  `plugins/satview/src/satview_plugin.cpp:439` builds runtime UI, capturing raw
  diagnostic descriptor IDs, before `record_prepass()` at line 471; overlay
  submission is deferred until `sink.render()` at line 484. The adapter merely
  retains `ImDrawData*` (`satview_imgui_adapter.h:31`). On an extent change,
  `satview_render_vk.cpp:1201` destroys the old target generation, whose
  diagnostic descriptors are freed at lines 388-392, before the queued overlay
  is encoded. Thus a resize alone is sufficient to produce stale descriptor IDs;
  no pane close or wrong-pool removal is required. Closing a sibling also resizes
  the survivor and exercises this same path.
- MegaCity has the same ordering: `megacity_plugin.cpp:336` captures UI,
  line 361 runs the prepass, and line 372 submits captured ImGui draw data.
  `codeviz_render_vk.cpp:2869` destroys resized G-buffer targets and frees their
  diagnostic descriptors before that submission. The existing device-idle waits
  protect previously submitted GPU work, not the current CPU-side draw list.
- The original owner selection is present: SatView removal selects the captured
  `imgui_texture_context` at line 383, MegaCity at line 2023, and both products
  release their scene before their ImGui backend. The observed failures report
  invalid descriptors **during bind/draw**, not wrong-pool `vkFreeDescriptorSets`
  errors. No evidence here establishes an ODR collision or a regression in the
  already-added owner-context scopes. The stale-overlay lifetime gap is the
  strongest source-supported explanation; font-atlas diagnostic imagery is
  consistent with the failed/stale texture binding, not proof of cross-pane
  texture ownership.
- Proposed bounded fix for agreement: preserve retired diagnostic target/image/
  descriptor generations until the queued overlay and its GPU submission have
  completed, or restructure target preparation so draw data never captures the
  generation being replaced. Keep owner-context removal. Adding another idle
  wait is not a fix for already-built CPU draw data. The SatView overlap is
  `plugins/satview/kanban/pending/08 satview-resize-target-lifetime -perf.md`;
  MegaCity needs the corresponding correctness treatment. No production edit,
  build, or additional GPU scenario was performed during this investigation.
- `scripts/windows_core_gate_probe.py` now records client-log byte length and
  validation-error count with each future liveness checkpoint. This improves
  the next scheduled gate's before/after attribution; it does not retroactively
  supply missing timestamps for this run.

## Root cause and fix — Windows, 2026-10-08

The invalid-descriptor failures had three causes, all of the same shape:
draw data built earlier in the frame names diagnostic descriptors that later
frame work frees.

1. **Resize retirement (SatView + MegaCity, Vulkan).** As diagnosed above, the
   runtime UI captures the current target generation's ImGui descriptors before
   `record_prepass()` replaces size-dependent targets. Both products now
   *retire* the replaced generation (`retire_hdr_targets` /
   `retire_gbuffer_targets`) and destroy it only after the pass has prepared
   `buffered_frame_count` more frames, by which time the host has waited on
   that frame's slot. Full teardown destroys retired sets explicitly.
2. **Render-pass change (SatView, Vulkan).** A native window resize recreates
   the swapchain and gives plugins a new continuation render pass.
   `ensure_hdr_setup()` then destroyed every HDR resource, including diagnostic
   descriptors and the sampler they reference, mid-frame. It now retires the
   targets together with the old sampler (the retired set owns and destroys
   it after its targets). MegaCity's render-pass path does not touch its
   G-buffer targets.
3. **Shared plugin ImGui pool (`plugins/support/imgui`).** The same render-pass
   change made `VulkanGpuImGuiHost` shut ImGui down **and destroy its
   descriptor pool**, invalidating every texture descriptor a product had
   registered while the products still held and later freed the handles. A
   render-pass-only change now re-initializes ImGui against the existing pool;
   only a device change or real shutdown destroys it. The pool grew from 64 to
   256 sets because MegaCity registers 15 diagnostic sets per frame target and
   a retired generation briefly coexists with its replacement.
4. **Metal (inspection, not runnable here).** `ImGui::Image((__bridge void*)…)`
   passes unretained `MTLTexture` pointers. Both products released the old
   targets on resize while that frame's draw data still named them, so the same
   retire countdown now keeps the replaced targets alive in
   `satview_render.mm` and `codeviz_render.mm`. These two edits are not
   compiled on Windows; macOS CI/build must confirm them.

### Live Vulkan gate (Debug, validation layer inserted from the SDK)

Driver: the `scripts/windows_core_gate_probe.py` long-lived client and exact-
PID/HWND/route guards, isolated runtime/profile. Sequence: SatView (HDR debug
panel) beside MegaCity (G-buffer debug), four **native window** resizes
(1200x800, 1500x1000, 1100x900, 1400x950), four split ratios (0.4/0.6/0.3/0.7),
close SatView, recreate SatView beside MegaCity and focus it, close MegaCity.
The validation count was sampled after every step.

- Before the fixes (`dgvum2579_`, `dguhogj590`): 44 and 34 validation errors
  (`vkFreeDescriptorSets-00310`, `vkCmdBindDescriptorSets-…-parameter/06563`,
  `vkCmdDrawIndexed-08600`). Temporary descriptor tracing showed a set added
  by one backend, freed by `ensure_hdr_setup()` after the render-pass re-init,
  and then bound by that frame's ImGui draw.
- After the fixes (`dg_bt2g0a_`): **0** validation errors at every one of the
  12 checkpoints; 6 swapchain creations and 11 ImGui backend re-inits
  exercised; 413 texture registrations and 413 removals. The client exited 0
  and its isolated server shut down with 0. The only `[error]` line is the
  unrelated stale `W:\p4\…` loader manifest. Captures show live normals/AO/
  depth/HDR/final G-buffer images before and after each transition.
  Two further runs without window resizes (`dg42xmwosz`, `dg93kl13d2`) were
  also clean. The temporary tracing has been removed.
- Separate pre-existing observation (also in the earlier failed gate's
  captures): MegaCity's floating G-buffer Debug window draws over the
  neighbouring SatView pane; tracked in
  `plugins/megacity/kanban/pending/17 debug-window-pane-clipping -bug.md`.

## Validation summary — Windows, 2026-10-08

- Configure/build: fresh Ninja Debug cache after fast-forwarding 573 commits
  (1298 steps), then incremental rebuilds per fix; Release cache rebuilt
  (`py do.py build release`).
- Core + products aggregate (`py do.py test debug --products`): 85/90 CTest
  entries passed in 533 s. The five failures were unrelated to these changes:
  `personal_agent_tests.cpp:488` `remove_all` (deterministic, also failed in
  the earlier gate, tracked on `kanban/pending/67 personal-assistant-host -feature.md`);
  Rezonality native shard failing only on the stale `W:\p4` implicit-layer
  loader manifest (421/422); `draxul-render-flashcards-queue` aborted because
  the desktop pointer moved / capture was unavailable while the machine was in
  use; and two Windows encoding test bugs fixed in the same session and rerun
  green (`personal_assistant_host_tests.cpp` CJK `u8` literal, 5/5 cases;
  `draxul-rezonality-agent-layout` UTF-8 decoding, passed 5.9 s).
- Same-cache Debug smoke: `py do.py smoke debug --skip-build` passed (about
  6 s, isolated runtime; see `kanban/pending/65 windows-validation-timing -test.md`).
- Final Release startup: `py do.py smoke release --skip-build` passed in 4.8 s
  from the Release cache (isolated runtime, owned server shut down).
- Remote CI not run; macOS/Metal paths rely on normal cross-platform CI.
