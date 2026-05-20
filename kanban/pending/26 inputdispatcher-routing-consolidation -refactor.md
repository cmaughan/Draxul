# WI 26 — InputDispatcher mouse-routing consolidation

**Type:** refactor  
**Source:** review-bugs-latest.gpt.md (MEDIUM), review-latest.claude.md  
**Consensus:** review-consensus.md Phase 6

---

## Goal

Reduce `InputDispatcher::Deps` from 20+ function pointers to a smaller typed interface, and consolidate the three near-identical chrome/panel/overlay/host routing patterns into one.

---

## Problem

GPT and Claude both flagged:
- `InputDispatcher::Deps` at `input_dispatcher.h:51` is effectively an untyped app-service interface with 20+ function pointers. One missing callback silently breaks that input path with no diagnostic.
- The same chrome → panel → overlay → host routing pattern is hand-maintained in three separate methods: `input_dispatcher.cpp:303`, `:425`, `:490`. These drift over time and are the main source of merge conflicts when new overlay types are added.

---

## Implementation Plan

**Phase A — audit and document:**
- [ ] List all 20+ `Deps` callbacks with their call sites and frequency of use.
- [ ] Identify which callbacks are "always set" (required) vs "optional" (gracefully omitted).
- [ ] Identify the three routing methods that share the chrome/panel/overlay/host pattern.

**Phase B — introduce a routing interface:**
- [ ] Define an `IInputRouter` or `IOverlayHitTester` interface with typed methods for the hit-test / routing callbacks (tab-bar height, tab hit-test, pane pill hit-test, overlay priority).
- [ ] Replace the corresponding `Deps` function pointers with a single `IInputRouter*`.
- [ ] Update `App` to implement `IInputRouter` (or create a small adapter).

**Phase C — consolidate routing logic:**
- [ ] Extract the common "which layer owns this mouse event?" logic into a single `resolve_mouse_target()` helper.
- [ ] Have the three `dispatch_*` methods call `resolve_mouse_target()` and then dispatch to the resolved target.

**Phase D — add validation:**
- [ ] Assert (debug build) that required `Deps` fields are non-null at `InputDispatcher` construction time.
- [ ] Update WI 22 (inputdispatcher-null-deps test) to cover the new interface.

---

## Interdependencies

- WI 22 (inputdispatcher-null-deps test) should be written before this refactor to lock down the current contract.
- WI 125 (overlay-registry-refactor, active) overlaps — coordinate to avoid `InputDispatcher` being touched twice.
- This refactor helps WI 121 (app-render-tree-overlay-ordering-test, active) by making the routing logic more testable.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
