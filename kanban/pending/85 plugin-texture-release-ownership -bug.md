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
- [ ] **Acceptance:** Closing either of two rendered product panes removes descriptors through the correct pool.
- [x] **Acceptance:** Closing a diagnostic-enabled SatView pane while an unrendered pane’s context is current cannot dereference a null backend.
- [ ] **Validation:** Run core/product aggregates appropriate to this shared seam, Vulkan validation, relevant multi-pane checks, and same-cache smoke; inspect Metal teardown alignment.

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
