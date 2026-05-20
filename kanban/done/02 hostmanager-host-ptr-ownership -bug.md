# 02 hostmanager-host-ptr-ownership — Bug

## Summary

`app/host_manager.h` exposes a method `host_ptr()` that returns `std::unique_ptr<IHost>&` — a mutable reference to the owning smart pointer. This is used by `GuiActionHandler` and `InputDispatcher` so they can observe (and swap) the current host. However, because the reference is mutable, any caller could accidentally:

- Call `.reset()` on the returned reference, destroying the host and leaving all other holders with a dangling raw pointer or reference.
- `std::move()` the returned reference into a local variable, stripping ownership from the manager.

Neither of these mistakes would be caught at compile time. The feature of "observe what the current host is" does not require ownership access; observation only needs a non-owning pointer or reference.

## Steps

- [x] 1. Read `app/host_manager.h` in full to see the current `host_ptr()` signature, the `unique_ptr<IHost>` member, and any other access methods.
- [x] 2. Read `app/host_manager.cpp` in full to see the implementation.
- [x] 3. Search for all call sites of `host_ptr()` in `app/`:
  - Found in `app/app.cpp` lines 145 and 240 only.
- [x] 4. Decide on the replacement API:
  - Kept `IHost* host() const` for observation (already existed).
  - Added `void set_host(std::unique_ptr<IHost> h)` for explicit ownership transfer.
  - Removed `host_ptr()` returning `unique_ptr<IHost>&`.
- [x] 5. Update `app/host_manager.h` and `app/host_manager.cpp` with the chosen API.
- [x] 6. Update `app/gui_action_handler.cpp` (and any other call sites found in step 3) to use the new non-owning accessor.
  - Changed `GuiActionHandler::Deps::host` from `std::unique_ptr<IHost>*` to `IHost*`.
  - Updated all `deps_.host->get()->...` calls to `deps_.host->...`.
- [x] 7. Update `app/input_dispatcher.cpp` (and any other call sites) to use the new accessor.
  - Changed `InputDispatcher::Deps::host` from `std::unique_ptr<IHost>*` to `IHost*`.
  - Updated all `deps_.host->get()->...` calls to `deps_.host->...`.
- [x] 8. Update `app/app.cpp` wherever it sets or transfers the host to use `set_host()`.
  - Changed `gui_deps.host = &host_manager_.host_ptr()` to `gui_deps.host = host_manager_.host()`.
  - Changed `disp_deps.host = &host_manager_.host_ptr()` to `disp_deps.host = host_manager_.host()`.
- [x] 9. Build: `cmake --build build --target draxul draxul-tests`. Confirm no compile errors.
- [x] 10. Run tests: `ctest --test-dir build -R draxul-tests`. Confirm all tests pass.
- [x] 11. Do a final search for any remaining `host_ptr` references to confirm none were missed:
  - No references remain.
- [x] 12. Run clang-format on all touched files.
- [x] 13. Mark this work item complete and move to `plans/work-items-complete/`.

## Acceptance Criteria

- `HostManager` no longer exposes a mutable `unique_ptr<IHost>&`.
- Observation callers use a non-owning pointer or reference accessor.
- Ownership transfer uses an explicitly named method (`set_host` or equivalent).
- All existing tests pass.
- No `unique_ptr<IHost>&` return type exists anywhere in the public interface.

*Authored by: claude-sonnet-4-6*
