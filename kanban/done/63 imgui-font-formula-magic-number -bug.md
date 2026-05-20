---
# WI 63 — ImGui Font Size Formula Duplicated 3× with Unexplained Magic Number

**Type:** bug  
**Priority:** medium (latent divergence risk on future font changes)  
**Raised by:** [C] Claude  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

The expression `float(cell_height) * (point_size - 2) / point_size` appears in three separate methods:

- `App::initialize()`
- `App::apply_font_metrics()`
- `App::initialize_host()`

The literal `2` is undocumented. Any change to the formula (e.g. a DPI-aware adjustment) requires three coordinated edits. If one is missed, ImGui font sizes diverge between initialization and font-size-change paths.

---

## Investigation Steps

- [ ] Open `app/app.cpp` and search for `point_size - 2`
- [ ] Confirm all three occurrences
- [ ] Understand what the `2` represents (likely a descender or padding correction specific to ImGui's internal font metrics)
- [ ] Check if there is already a named helper anywhere for this calculation

---

## Implementation

- [ ] Extract the formula into a private static `App::imgui_font_size(float cell_height, float point_size)` helper in `app.cpp` (or a free function in an internal header)
- [ ] Document the `2` with a comment explaining its derivation (e.g. `// empirical correction for ImGui baseline offset, see #NNN`)
- [ ] Replace all three call sites with the helper call

---

## Acceptance Criteria

- [ ] Single definition of the font size formula in the codebase
- [ ] The magic constant is named or commented
- [ ] All three original call sites use the helper
- [ ] Build and smoke test pass; ImGui font rendering is visually unchanged

---

## Notes

This is a pure refactor with no behaviour change. Commit message should reference the fact that the magic number is preserved unchanged to avoid blame confusion later.
