# WI 102 — ui-events-type-unchecked-throw

**Type:** bug  
**Priority:** 1 (process abort from main thread on malformed redraw data)  
**Source:** review-bugs-consensus.md §H1 [GPT]  
**Produced by:** claude-sonnet-4-6

---

## Problem

Multiple redraw handlers in `libs/draxul-nvim/src/ui_events.cpp` check the outer array length but
then call `as_int()` or `as_str()` on element values without first checking their `type()`.
`MpackValue::as_int()` throws `std::bad_variant_access` on a type mismatch.

These handlers run on the **main thread** inside `pump()` → `process_redraw()`. An uncaught
exception on the main thread also calls `std::terminate()`.

Affected sites (file confirmed in current tree):

| Handler | Line | Unchecked call |
|---|---|---|
| `handle_grid_cursor_goto` | 298 | `args_array[1].as_int()` / `args_array[2].as_int()` |
| `handle_grid_scroll` | 308–313 | six `as_int()` calls |
| `handle_grid_resize` | 330 | `args_array[1].as_int()` / `args_array[2].as_int()` |
| `handle_mode_change` | 464 | `as_array()[1].as_int()` |
| `handle_option_set` | 473 | `args_array[0].as_str()` |
| `handle_set_title` | 483 | `args_array[0].as_str()` |

**Trigger:** A malformed `redraw` batch such as `["grid_resize", [1, "80", "24"]]` where col/row
are strings instead of integers.

---

## Investigation

- [ ] Read all handler functions listed above in `libs/draxul-nvim/src/ui_events.cpp` — enumerate
  every unchecked `as_*()` call.
- [ ] Check `handle_hl_attr_define` as the reference for how the codebase already handles
  type-guarded scalar extraction.
- [ ] Confirm where `process_redraw` is called and whether any outer `try/catch` exists that would
  already catch these exceptions.

---

## Fix Strategy

Two approaches — pick the one that fits best after reading the code:

**Option A — per-call type guards:**  
Replace each unchecked `as_int()` / `as_str()` with a helper that returns `std::optional` or a
default value on type mismatch:

```cpp
// Example helper (add to ui_events.cpp anonymous namespace)
static std::optional<int64_t> safe_int(const MpackValue& v)
{
    if (v.type() == MpackValue::Int || v.type() == MpackValue::UInt)
        return v.as_int();
    return std::nullopt;
}
```

Each call site then does `if (auto v = safe_int(args_array[1])) { ... }` or `val = safe_int(...).value_or(0)`.

**Option B — outer try/catch in `process_redraw`:**  
Wrap the entire dispatch loop in `process_redraw` with a `try/catch(const std::exception&)` that
logs the event name and skips the batch. This is simpler but hides the specific site.

- [ ] Apply chosen option to all sites listed above.
- [ ] Grep for other `as_int()` / `as_str()` calls in `ui_events.cpp` that are not type-guarded;
  fix those too.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] A malformed `redraw` packet (e.g. string where integer expected) results in a logged warning
  and the batch being skipped, not `std::terminate`.
- [ ] Normal Neovim operation is unaffected (smoke test passes).
- [ ] No unchecked `as_int()` / `as_str()` calls remain in redraw handlers where the type is not
  guaranteed by a prior `type()` check.

---

## Interdependencies

- **WI 101** (reader-thread exception guard) — same defensive-typing strategy; do in same pass.
