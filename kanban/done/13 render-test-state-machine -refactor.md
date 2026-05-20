# Refactor: render_render_test() State Machine Cleanup

**Type:** refactor
**Priority:** 13
**Source:** Claude, GPT, Gemini (all three flagged)

## Problem

`App::run_render_test()` in `app/app.cpp` tracks state using 7+ independent `std::optional<>` flags:
- `diagnostics_enabled`, `ready_since`, `quiet_since`, `capture_requested`, and others

The transitions between states are implicit — scattered `if`/`else` chains that check combinations of flags. A missed transition silently times out in CI with no diagnostic. The code is untestable in isolation because:
1. It mixes state, time, and I/O with no seams.
2. There is no named concept of "current state" to inspect in a test.

**Note:** Do this alongside `04 render-test-state-machine -test`. The refactor makes the code testable; the tests validate the refactor. A single agent should do both.

## Proposed design

Define a named state enum and a state struct:

```cpp
enum class RenderTestState {
    kStartup,
    kWaitingForQuiet,
    kCapturing,
    kComparing,
    kDone,
};

struct RenderTestContext {
    RenderTestState state = RenderTestState::kStartup;
    std::chrono::steady_clock::time_point quiet_start;
    std::chrono::steady_clock::time_point timeout_start;
    bool diagnostics_were_enabled = false;
    // ...
};
```

The main loop body becomes a dispatch on `ctx.state` rather than a tangle of optionals.

## Implementation steps

- [x] Read `app/app.cpp` — map out every `std::optional<>` flag in `run_render_test()` and which state they collectively represent.
- [x] Define `RenderTestState` enum and `RenderTestContext` struct (can live in an anonymous namespace in `app.cpp` or a small header).
- [x] Rewrite the state transitions as explicit state-machine cases.
- [ ] Inject a clock abstraction (a `std::function<steady_clock::time_point()>` parameter or a virtual `IClock`) so tests can control time.
- [x] Verify the render-test CI runs (`py do.py smoke` or `ctest -R draxul-render`) still pass after the refactor.

## Acceptance criteria

- [x] `run_render_test()` has a single named state variable (the enum) and no uncoordinated optionals.
- [ ] The function accepts (or can be given) an injectable clock for tests.
- [x] All existing render-test CI scenarios continue to pass.
- [ ] Tests from `04 render-test-state-machine -test` all pass.

## Interdependencies

- **`04 render-test-state-machine -test`**: write tests in the same agent pass, after the refactor.

---
*Filed by `claude-sonnet-4-6` · 2026-03-26*
