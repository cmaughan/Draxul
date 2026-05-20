---
# App Init/Shutdown Rollback Integration Test

## Summary
`startup_rollback_tests.cpp` originally skipped the integration path because `App` was not linkable
from tests. We added DI seams (`window_init_fn`, `renderer_create_fn`) to `AppOptions` and compiled
`app/app.cpp` + `app/host_manager.cpp` directly into the test binary.

## Context
`tests/startup_rollback_tests.cpp` — SKIP annotations removed. `app/app.h` and `app/app.cpp` for
App's initialize() and shutdown(). Verified that partial initialisation (e.g., window created but
renderer fails) does not leak resources.

## Steps
- [x] Read `tests/startup_rollback_tests.cpp` and note all skipped test cases
- [x] Determined that App can be compiled into the test binary by adding `app/app.cpp` + `app/host_manager.cpp` to `tests/CMakeLists.txt`
- [x] Added `window_init_fn` and `renderer_create_fn` factory overrides to `AppOptions` in `app_config.h`
- [x] Modified `app/app.cpp` to use those factories when set (falling back to real SDL/renderer otherwise)
- [x] Wrote integration tests: fail at window init, fail at renderer init, fail at font init, fail at host init
- [x] Wrote double-shutdown safety test
- [x] Removed all SKIP annotations; tests now run real rollback logic

## Acceptance Criteria
- [x] At least 4 rollback scenarios covered with real (not skipped) assertions (6 tests total)
- [x] Tests run under ASan without leaks (all tests pass)

## Notes
The factory seams use `std::function<bool()>` for window_init_fn and
`std::function<RendererBundle(int)>` for renderer_create_fn. In production, both are null and
the real SDL/GPU paths are taken. In tests, they let us inject controlled failures without
initialising any real system resources.

The "host init failure" test uses a nonexistent binary path to make process spawn fail immediately.

*Work item implemented by claude-sonnet-4-6*
