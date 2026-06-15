# Kanban Pending Batch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement and close the highest-priority small pending kanban cards that can be validated from this Windows workspace.

**Architecture:** Keep fixes at the owning adapter/helper layer: UI redraw integer tolerance stays in `ui_events.cpp`, MPack encode invariants stay in `mpack_codec.cpp`, and RPC write-failure signaling stays in `rpc.cpp`. Do not broaden these into framework refactors.

**Tech Stack:** C++20, Catch2, CMake/MSBuild, existing Draxul `draxul-tests` target.

---

## Batch Scope

Implement:
- `kanban/pending/00 try-get-int-ub-crash -bug.md`
- `kanban/pending/08 mpack-codec-size-truncation -bug.md`
- `kanban/pending/01 rpc-spurious-notification-callback -bug.md`

Plan/defer:
- POSIX process cards: `02`, `04`, `06` need macOS/Linux sanitizer validation.
- Large feature/refactor cards: `20`, `24`, `27`, `39`, `55`, `82`, `125`, `126`, `127`, `135`.
- Blocked cards: `26` waits on `22`; `39` waits on `26`; `27` waits on atlas tests/refactor closeout.

---

### Task 1: Harden UI Redraw Integer Adapter

**Files:**
- Modify: `tests/ui_events_tests.cpp`
- Modify: `libs/draxul-nvim/src/ui_events.cpp`
- Move when complete: `kanban/pending/00 try-get-int-ub-crash -bug.md` -> `kanban/done/00 try-get-int-ub-crash -bug.md`

- [x] **Step 1: Write the failing test**

Add a test named `ui event handler rejects out-of-range integer redraw payloads` that:
- sets mode to `42` with a valid integer payload
- sends `UINT64_MAX` and `INT64_MAX` as `mode_change` indices
- asserts no throw and that `current_mode()` remains `42`

- [x] **Step 2: Run test to verify it fails**

Run:
```powershell
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "ui event handler rejects out-of-range integer redraw payloads"
```

Expected before fix: the test fails because `try_get_int()` throws or accepts a narrowed value.

- [x] **Step 3: Write minimal implementation**

Update `try_get_int(const MpackValue&, int&)` to:
```cpp
if (value.type() != MpackValue::Int && value.type() != MpackValue::UInt)
    return false;
try
{
    const int64_t raw = value.as_int();
    if (raw < static_cast<int64_t>(std::numeric_limits<int>::min())
        || raw > static_cast<int64_t>(std::numeric_limits<int>::max()))
        return false;
    out = static_cast<int>(raw);
    return true;
}
catch (const std::exception&)
{
    return false;
}
```

- [x] **Step 4: Run test to verify it passes**

Run:
```powershell
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "ui event handler rejects out-of-range integer redraw payloads"
```

Expected after fix: the focused test passes.

---

### Task 2: Guard MPack Encode Size Narrowing

**Files:**
- Modify: `libs/draxul-nvim/src/mpack_codec.cpp`
- Move when complete: `kanban/pending/08 mpack-codec-size-truncation -bug.md` -> `kanban/done/08 mpack-codec-size-truncation -bug.md`

- [x] **Step 1: Verify existing RPC codec coverage**

Run:
```powershell
.\build\tests\Debug\draxul-tests.exe "[rpc]"
```

Expected before implementation: existing RPC tests pass or only fail for unrelated current-tree issues.

- [x] **Step 2: Add assertions and explicit casts**

In `write_value()`:
```cpp
const size_t size = val.as_str().size();
assert(size <= std::numeric_limits<uint32_t>::max());
mpack_write_str(writer, val.as_str().c_str(), static_cast<uint32_t>(size));
```

Use the same pattern for array and map sizes before `mpack_start_array()` and `mpack_start_map()`.

- [x] **Step 3: Verify no C-style uint32 narrowing remains**

Run:
```powershell
rg -n "\(\s*uint32_t\s*\)" libs/draxul-nvim/src/mpack_codec.cpp
```

Expected: no matches.

- [x] **Step 4: Run RPC codec tests**

Run:
```powershell
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "[rpc]"
```

Expected after fix: RPC tests pass.

---

### Task 3: Remove Spurious RPC Notification Callback

**Files:**
- Modify: `tests/rpc_fake_server.cpp`
- Modify: `tests/rpc_integration_tests.cpp` or `tests/rpc_backpressure_tests.cpp`
- Modify: `libs/draxul-nvim/src/rpc.cpp`
- Move when complete: `kanban/pending/01 rpc-spurious-notification-callback -bug.md` -> `kanban/done/01 rpc-spurious-notification-callback -bug.md`

- [x] **Step 1: Add deterministic fake-server mode**

Add a mode that closes the child stdin/read side, writes a ready marker, waits for a parent-created release marker, then exits. This makes `NvimRpc::notify()` observe a write failure without also racing an immediate reader-thread EOF notification.

- [x] **Step 2: Write the failing test**

Initialize `NvimRpc` with a callback counter, spawn the fake server in the new mode, call `notify()`, and assert:
```cpp
REQUIRE(callback_count == 0);
REQUIRE(rpc.connection_failed());
```

- [x] **Step 3: Run test to verify it fails**

Run:
```powershell
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "rpc notify write failure does not signal notification availability"
```

Expected before fix: callback count is `1`.

- [x] **Step 4: Write minimal implementation**

In `NvimRpc::notify()` write-failure branch, remove only the `callbacks_.on_notification_available()` call. Keep:
```cpp
impl_->read_failed_ = true;
impl_->response_cv_.notify_all();
```

- [x] **Step 5: Run test to verify it passes**

Run:
```powershell
cmake --build build --config Debug --target draxul-tests
.\build\tests\Debug\draxul-tests.exe "rpc notify write failure does not signal notification availability"
```

Expected after fix: callback count stays `0`.

---

### Task 4: Batch Verification And Kanban Closeout

**Files:**
- Modify/move completed kanban cards under `kanban/done/`
- Do not touch unrelated pending cards except to mention deferrals in the final response

- [x] **Step 1: Run focused tests**

Run:
```powershell
.\build\tests\Debug\draxul-tests.exe "[ui],[rpc]"
```

Expected: all selected UI/RPC tests pass.

- [x] **Step 2: Run project verification required by AGENTS**

Run:
```powershell
cmake --build build --config Release --target draxul draxul-tests
ctest --test-dir build --build-config Release --output-on-failure
python do.py smoke
```

Expected: build, tests, and smoke pass. If a command cannot be completed in this environment, record the exact failure.

- [x] **Step 3: Move completed cards**

Move only cards whose acceptance criteria were implemented and verified:
```powershell
Move-Item -LiteralPath 'kanban/pending/00 try-get-int-ub-crash -bug.md' -Destination 'kanban/done/00 try-get-int-ub-crash -bug.md'
Move-Item -LiteralPath 'kanban/pending/08 mpack-codec-size-truncation -bug.md' -Destination 'kanban/done/08 mpack-codec-size-truncation -bug.md'
Move-Item -LiteralPath 'kanban/pending/01 rpc-spurious-notification-callback -bug.md' -Destination 'kanban/done/01 rpc-spurious-notification-callback -bug.md'
```

- [x] **Step 4: Final review**

Inspect:
```powershell
git diff --stat
git diff --check
git status --short
```

Expected: only planned files changed, no whitespace errors.
