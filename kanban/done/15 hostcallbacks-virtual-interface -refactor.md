# 15 hostcallbacks-virtual-interface -refactor

**Priority:** MEDIUM
**Type:** Refactor (replace 5 std::function slots with a typed observer interface)
**Raised by:** Claude
**Model:** claude-sonnet-4-6

---

## Problem

`HostCallbacks` (`host.h:66-73`) holds five separate `std::function<void(...)>` members. Each lambda capture allocates on the heap. More critically, every lambda captures `App* this` with no lifetime guarantee — if a host fires a callback during `App` teardown (after `App` is partially destroyed), the lambda dereferences freed memory. The `running_` flag is the only guard and is insufficient for multi-pane scenarios where one host shuts down while another fires.

Additionally, every new callback requires updating all construction sites, and there is no compile-time check that all slots are populated.

---

## Code Locations

- `libs/draxul-host/include/draxul/host.h:66-73` — `HostCallbacks` struct
- `app/app.cpp` — `make_host_callbacks()` — the only constructor of `HostCallbacks`
- `app/host_manager.cpp` — where `HostCallbacks` is distributed to hosts

---

## Implementation Plan

- [x] Read `host.h`, `app.cpp::make_host_callbacks()`, and `host_manager.cpp` to understand all five callbacks and their call sites.
- [x] Design a replacement: an `IHostObserver` pure-virtual interface with one virtual method per callback. Example:
  ```cpp
  struct IHostObserver {
      virtual void on_request_frame() = 0;
      virtual void on_wake_window() = 0;
      virtual void on_set_title(std::string_view) = 0;
      virtual void on_close_requested(LeafId) = 0;
      virtual void on_dimensions_changed() = 0;
      virtual ~IHostObserver() = default;
  };
  ```
- [x] Make `App` implement `IHostObserver`. The five `App` methods replace the five lambdas.
- [x] In `App::initialize()` / `make_host_callbacks()`, pass `this` as `IHostObserver*` to hosts instead of constructing `HostCallbacks`.
- [x] The `running_` guard can be enforced in `IHostObserver` implementations: each method checks `running_` before acting. This is explicit and in one place.
- [x] Retain `HostCallbacks` as a deprecated alias or remove it entirely.
- [x] Ensure the test `10 multipane-callback-lifetime -test` passes after this change.
- [x] Build: `cmake --build build --target draxul draxul-tests && py do.py smoke`
- [x] Run `clang-format`.

---

## Acceptance Criteria

- No `std::function` in `HostCallbacks` or its replacement.
- All five callback behaviours are preserved.
- `10 multipane-callback-lifetime -test` passes.
- No heap allocations at host-callback wiring time (virtual dispatch is allocation-free).

---

## Interdependencies

- **`10 multipane-callback-lifetime -test`** — write the test first to establish the failure mode, then apply this refactor.
- This is a pure refactor; no user-visible behaviour changes.
- A sub-agent is appropriate since the change touches `host.h`, `app.cpp`, and `host_manager.cpp`.

---

*claude-sonnet-4-6*
