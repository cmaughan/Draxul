# Release diagnostic textures through their owning pane
**Summary:** Release each pane’s diagnostic images through their owner so closing one pane cannot corrupt another pane’s drawing state.

**Priority:** 85  
**Severity:** CRITICAL  
**Source:** `plugins/support/imgui/src/plugin_imgui_context.cpp`  
**Reported by:** Claude C3 and C4; consensus F04.

**Evidence and trigger:** MegaCity and SatView destroy scene passes before selecting their owning context. Shared Vulkan removal uses the current context’s descriptor pool. SatView additionally permits null-backend removal after another hidden pane creates a context.

**Related:** `kanban/pending/26 plugin-imgui-vulkan-api -refactor.md`; `plugins/satview/kanban/pending/08 satview-resize-target-lifetime -perf.md`.

- [ ] **Investigate:** Trace texture registration/removal ownership, context changes, scene teardown, resize retirement, and lazy backend initialization.
- [ ] **Fix:** Provide scoped owner-context handling or explicit owned texture removal and apply it to both product callers.
- [ ] **Fix:** Remove textures before their owning backend is destroyed and handle absent backend data safely.
- [ ] **Acceptance:** Closing either of two rendered product panes removes descriptors through the correct pool.
- [ ] **Acceptance:** Closing a diagnostic-enabled SatView pane while an unrendered pane’s context is current cannot dereference a null backend.
- [ ] **Validation:** Run core/product aggregates appropriate to this shared seam, Vulkan validation, relevant multi-pane checks, and same-cache smoke; inspect Metal teardown alignment.
