# Release diagnostic textures through their owning pane
**Summary:** Release each pane’s diagnostic images through their owner so closing one pane cannot corrupt another pane’s drawing state.

**Priority:** 85  
**Severity:** CRITICAL  
**Source:** `plugins/support/imgui/src/plugin_imgui_context.cpp`  
**Reported by:** Claude C3 and C4; consensus F04.

**Evidence and trigger:** MegaCity and SatView destroy scene passes before selecting their owning context. Shared Vulkan removal uses the current context’s descriptor pool. SatView additionally permits null-backend removal after another hidden pane creates a context.

**Related:** `kanban/pending/26 plugin-imgui-vulkan-api -refactor.md`; `plugins/satview/kanban/pending/08 satview-resize-target-lifetime -perf.md`.

- [x] **Investigate:** Trace texture registration/removal ownership, context changes, scene teardown, resize retirement, and lazy backend initialization.
- [x] **Fix:** Provide scoped owner-context handling or explicit owned texture removal and apply it to both product callers.
- [x] **Fix:** Remove textures before their owning backend is destroyed and handle absent backend data safely.
- [ ] **Acceptance:** Closing either of two rendered product panes removes descriptors through the correct pool.
- [ ] **Acceptance:** Closing a diagnostic-enabled SatView pane while an unrendered pane’s context is current cannot dereference a null backend.
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
