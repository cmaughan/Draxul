---
# WI 62 — Selection Truncation Gives No User Feedback

**Type:** bug  
**Priority:** high (user-visible, silent data loss)  
**Raised by:** [C] Claude, [G] Gemini, [P] GPT — unanimous  
**Created:** 2026-04-03  
**Model:** claude-sonnet-4-6

---

## Problem

`SelectionManager::kSelectionMaxCells = 8192` silently truncates selections that exceed the limit. On a 200-column terminal this is only ~40 rows. Users who select large code blocks (e.g. for copy) receive a truncated clipboard paste with no indication that truncation occurred. All three review agents flagged this as one of the worst existing features.

---

## Investigation Steps

- [ ] Locate `SelectionManager` in `libs/draxul-host/include/draxul/selection_manager.h`
- [ ] Find where the 8192-cell limit is enforced and the selection is capped
- [ ] Identify the callback/channel available to surface a user-visible message (toast notification via WI 22, or log at WARN level, or both)
- [ ] Check whether the `SelectionManager::Deps` struct has a callback for user-facing messages

---

## Implementation

- [ ] When the cell limit is hit, call a warning callback (add one to `SelectionManager::Deps` if not present, e.g. `std::function<void(std::string_view)> on_selection_truncated`)
- [ ] In the host wiring, connect that callback to the existing toast/notification system (WI 22) if available, or log at `WARN` level if WI 22 is not yet landed
- [ ] Consider raising the limit from 8192 to a larger value (see WI 81 for the raise itself; this item is specifically about the warning)
- [ ] Add a unit test: select exactly `kSelectionMaxCells + 1` cells, verify the callback fires and the selection is capped at the limit

---

## Test Design

```cpp
TEST_CASE("SelectionManager warns on truncation") {
    bool warned = false;
    SelectionManager::Deps deps;
    deps.on_selection_truncated = [&](std::string_view) { warned = true; };
    // ... set up grid with > kSelectionMaxCells cells selected
    // verify warned == true and actual selection == kSelectionMaxCells
}
```

---

## Acceptance Criteria

- [ ] Selecting more than `kSelectionMaxCells` cells triggers the warning callback
- [ ] The warning message states the limit and the actual number of cells selected
- [ ] Unit test passes
- [ ] Existing selection tests are unaffected

---

## Notes

Interdependency: if WI 22 (toast notifications) is already landed, wire this warning to the toast system. If not, use `DRAXUL_LOG_WARN`. Do not block this fix on WI 22.
