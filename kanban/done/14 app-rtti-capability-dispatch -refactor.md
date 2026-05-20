# 14 app-rtti-capability-dispatch -refactor

**Type:** refactor
**Priority:** 14
**Source:** GPT review (review-latest.gpt.md) — HIGH finding; Gemini (leakage of UI concerns)

## Problem

`app/app.cpp` contains at least three RTTI-based capability dispatches that keep concrete-implementation knowledge in the orchestration layer:

1. `dynamic_cast<SdlWindow*>` — used to enable render-test mode. If the window is not `SdlWindow`, render-test silently does nothing.
2. `dynamic_cast<I3DHost*>` (lines ~307, ~457) — used to call ImGui-related methods. If a new host subclass forgets to inherit `I3DHost`, ImGui wiring is silently skipped.

These mean `app.cpp` must know about concrete types it is supposed to be abstract over. Adding a new window or host type requires checking `app.cpp` for silent cast failures.

GPT: "This keeps lower-layer capability knowledge in the orchestration layer and weakens compile-time guarantees when adding new host/window types."

## Acceptance Criteria

- [x] Read `app/app.cpp` in full. Catalogue every `dynamic_cast` and what it guards.
- [x] For `dynamic_cast<SdlWindow*>`: extract render-test behaviour into a virtual method on `IWindow` (e.g., `virtual bool is_render_test_mode() const { return false; }`). `SdlWindow` overrides to return the real flag. `app.cpp` calls the virtual method — no cast.
- [x] For `dynamic_cast<I3DHost*>`: extract the ImGui hook calls into a virtual method on `IHost` with a default no-op (e.g., `virtual void render_imgui() {}`). `I3DHost` overrides with the real implementation. `app.cpp` calls the virtual — no cast.
- [x] After the change, `app.cpp` should contain zero `dynamic_cast` calls.
- [x] Build both targets; run `ctest`. Run smoke test (`py do.py smoke`).

## Implementation Notes

- Adding no-op virtual methods to `IHost` and `IWindow` is the minimal change. Avoid redesigning the interface hierarchy — that is `15 ihost-interface-width`.
- Verify that the `MegaCityHost` and any other `I3DHost` subclass correctly overrides the new virtual.
- A sub-agent is appropriate: read `app.cpp`, identify all casts, propose the minimal virtual additions, implement them.

## Interdependencies

- No blockers.
- **Pairs well with** icebox 16 (HostManager dynamic_cast removal) — doing this first makes that smaller.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
