# 15 ihost-interface-width -refactor

**Type:** refactor
**Priority:** 15
**Source:** Claude review (review-latest.claude.md); Gemini ("ImGui in Host Interface")

## Problem

`IHost` has ~14 pure virtual methods, including `on_mouse_move`, `on_mouse_wheel`, `on_mouse_button`, `on_key`, `on_text_input`, `on_text_editing`, and more. Any host that does not handle mouse events (a headless replay host, a render-test host, a future read-only viewer) must implement no-op stubs for all of them. This:

- Creates a maintenance tax: every new event type added to `IHost` requires visiting every host subclass.
- Increases the chance of a stub silently masking a real event dispatch.
- Makes reading the host hierarchy harder — it is not obvious which hosts genuinely handle mouse vs. just stub it.

Claude: "A smaller base interface with optional extension interfaces (like `IMouseHost`) would reduce boilerplate."

## Acceptance Criteria

- [x] Read the full `IHost` interface and all concrete host subclasses (`NvimHost`, `LocalTerminalHost`, `PowerShellHost`, `MegaCityHost`, and any others).
- [x] Identify the event methods that only some hosts genuinely implement (vs. all hosts needing them).
- [x] Propose a split, e.g.:
  - `IHost` — lifecycle only (`initialize`, `shutdown`, `tick`, `on_resize`).
  - `IInteractiveHost` extends `IHost` — adds keyboard and text input.
  - `IMouseHost` extends `IHost` — adds mouse events.
  - Or alternatively: provide default no-op implementations in `IHost` for mouse methods (making them non-pure virtuals).
- [x] Implement the agreed split with minimal churn to existing callers.
- [x] Confirm `HostManager` can hold `IHost*` and dispatch to the appropriate sub-interface without new `dynamic_cast` (or that any new cast is explicitly documented).
- [x] Build both targets; run `ctest`.

## Implementation Notes

- The "provide default no-ops" approach is the lowest-risk option if the full interface split is too invasive.
- This item should NOT be done at the same time as `14 app-rtti-capability-dispatch` — do item 14 first to avoid two simultaneous interface changes.
- A sub-agent reading the host hierarchy and proposing the split before implementing is the right approach here.

## Interdependencies

- **Should be done after** `14 app-rtti-capability-dispatch` to avoid conflicting interface changes.
- No other blockers.

---

*Authored by claude-sonnet-4-6 — 2026-03-23*
