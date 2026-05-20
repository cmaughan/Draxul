# 01 App Loop And UI Request Decomposition

## Why This Exists

`app/app.cpp` is still the main merge hotspot. The module split is good, but too much policy still terminates in one orchestration class.

Current hot areas:

- cursor blink state machine
- event-driven run loop policy
- startup resize deferral
- clipboard action routing
- render/smoke mode execution
- config-save hooks

## Goal

Keep `App` as the top-level coordinator, not the home for every policy decision.

## Implementation Plan

1. ~~Extract loop/cursor timing policy.~~ **DONE**
   - extracted `CursorBlinker` (`app/cursor_blinker.h/cpp`) owning phase state machine (`Steady/Wait/On/Off`), timing, and visibility
   - added `BlinkTiming` POD struct; `App::current_blink_timing()` reads from `UiEventHandler`
   - `App` reduced: removed `CursorBlinkPhase` enum, 3 state members, `advance_cursor_blink`; `restart_cursor_blink` is now a thin wrapper
   - `pump_once` advance path: `if (cursor_blinker_.advance(now)) apply_cursor_visibility()`
   - `wait_timeout_ms` uses `cursor_blinker_.next_deadline()`
2. ~~Separate UI request execution from app orchestration.~~ **ASSESSED — no change needed**
   - `UiRequestWorker` already owns resize debounce; startup resize deferral is inherently App-level (requires `saw_flush_` knowledge) and is 3 lines — no useful extraction
3. Isolate startup/shutdown staging — already clean; `InitRollback` RAII guard handles failure rollback explicitly
4. ~~Keep `App` owning composition, not details.~~ **DONE** via cursor blinker extraction

## Tests

- ~~add focused tests around the extracted scheduler or loop-policy helper~~ **DONE** — 7 `CursorBlinker` unit tests in `tests/cursor_blinker_tests.cpp`
- startup resize deferral coverage deferred — logic is simple and covered indirectly by smoke test
- ~~keep app smoke and render snapshot scenarios green~~ **DONE**

## Suggested Slice Order

1. ~~extract cursor blink/timing helper~~ **DONE**
2. ~~extract request coordination helper~~ **ASSESSED — no change needed**
3. ~~trim `App` callsites~~ **DONE** (part of slice 1)
4. ~~add seam tests~~ **DONE**

## Sub-Agent Split

- one agent on loop/timing extraction
- one agent on request worker decomposition
- avoid parallel edits in `app/app.cpp` until helper interfaces are agreed
