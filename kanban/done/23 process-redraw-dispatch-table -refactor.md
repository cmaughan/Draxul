# 23 process_redraw Dispatch Table Refactor

## Why This Exists

`UiEventHandler::process_redraw()` dispatches on event names using an `if (*name == "flush") / else if (*name == "grid_line") / ...` chain of ~11 string comparisons. This runs in a hot loop — every redraw batch from Neovim passes through it, potentially thousands of times per frame. The `if/else` chain is O(n) in the number of events and grows linearly as new event types are added.

An `std::unordered_map<std::string_view, handler_fn>` dispatch table would be both faster (O(1) amortised lookup) and cleaner (new event types added in one place, not by extending an if/else chain).

**Source:** `libs/draxul-nvim/src/ui_event_handler.cpp` — `process_redraw()`.
**Raised by:** Claude.

## Goal

Replace the `if/else if` event-name dispatch in `process_redraw()` with a static dispatch table
(either a `std::unordered_map` or a `std::array` of `{name, fn}` pairs with linear search if the
set is small enough to be cache-friendly).

## Implementation Plan

- [x] Read `libs/draxul-nvim/src/ui_event_handler.cpp` to enumerate all handled event names and their handler calls.
- [x] Count the number of distinct event names — if ≤ ~15, a sorted `std::array` with `std::lower_bound` may be preferable to a hash map (no heap allocation, better cache locality).
- [x] Choose the approach and implement:
  - **Option A (hash map):** `static const std::unordered_map<std::string, std::function<void(...)>> kDispatch = {...};`
  - **Option B (sorted array):** `static constexpr std::array kDispatch = {{"event_name", &handler}, ...};` with `std::lower_bound`.
- [x] Replace the `if/else if` block with a table lookup.
- [x] Ensure the "unknown event" path (currently the final `else`) is preserved as the fallback.
- [x] Run `clang-format` on touched files.
- [x] Run `ctest --test-dir build`.

## Notes

The handlers likely need to capture `this` (the `UiEventHandler` instance). Use member function
pointers or lambdas as the callable type. Keep the handler signatures uniform.

Implemented with a sorted `std::array` of 14 redraw event names plus `std::lower_bound`, which keeps
the dispatch table allocation-free while removing the long string-comparison chain. The unknown-event
fallback is covered by an added regression test in `tests/ui_events_tests.cpp`.

## Sub-Agent Split

Single agent. Confined to `ui_event_handler.cpp`.
