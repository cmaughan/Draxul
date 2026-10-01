# Share primitive ImGui input injection

**Summary:** Share the basic translation of keyboard and mouse events for panels and plugins so fixes to input handling apply consistently to both.

**Priority:** P2 — diagnostics and plugin wrappers repeat event injection.  
**Source:** `libs/draxul-imgui-core/src/sdl_imgui_input.cpp`  
**Proposed by:** Codex 11. **Owner:** one ImGui input agent.  
**Evidence:** `ui_panel.cpp` and `imgui_input_bridge.h` repeat modifiers, text, buttons and wheel; imgui-core already owns shared scancodes.

**Boundary verification**
- [ ] Record coordinate scaling, capture verdict and current-context behavior of both wrappers.
**Implementation and migration**
- [ ] Add scalar context/IO injection in existing imgui-core; migrate wrappers, retaining scaling/capture locally and no `draxul-types` dependency.
**Unit tests**
- [ ] Context-only key, modifier, text, button, wheel and null-context cases.
**Cross-platform validation**
- [ ] Verify Windows/macOS SDL input, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Clarify primitive versus wrapper responsibilities.
**Acceptance criteria**
- [ ] One injection mapping serves both wrappers without changing their capture behavior.
