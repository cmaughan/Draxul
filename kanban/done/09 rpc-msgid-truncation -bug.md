# WI 09 — `int64_t` msgid silently truncated to `uint32_t` without range validation

**Type:** bug  
**Severity:** MEDIUM  
**Source:** review-bugs-latest.claude.md  
**Consensus:** review-consensus.md Phase 4

---

## Problem

`libs/draxul-nvim/src/rpc.cpp` lines 212 and 240:

```cpp
uint32_t msgid = (uint32_t)msg_array[1].as_int();   // line 212
auto req_msgid = (uint32_t)msg_array[1].as_int();   // line 240
```

`as_int()` returns `int64_t`. A negative or `> UINT32_MAX` value (from a corrupted stream or future nvim protocol change) silently wraps. A negative ID could collide with a valid in-flight `msgid`, causing the wrong waiting thread to be unblocked with another request's response — silent correctness failure, potentially manifesting as a frozen UI or wrong command result.

**Files:**
- `libs/draxul-nvim/src/rpc.cpp` (~lines 212, 240)

---

## Implementation Plan

- [x] At both call sites, validate the raw `int64_t` before casting:
  ```cpp
  int64_t raw_id = msg_array[1].as_int();
  if (raw_id < 0 || raw_id > static_cast<int64_t>(UINT32_MAX)) {
      DRAXUL_LOG_WARN(LogCategory::Rpc, "msgid out of range: {}", raw_id);
      return;
  }
  uint32_t msgid = static_cast<uint32_t>(raw_id);
  ```
- [x] Apply the same guard to the request-dispatch path if it also uses an unchecked cast.
- [x] Add a unit test with a fake RPC stream that injects a negative msgid and asserts graceful discard (no response mismatch, no crash).

---

## Resolution

The range-validation fix in `libs/draxul-nvim/src/rpc.cpp` landed previously
alongside WI 105 (`rpc-msgid-cast-validation`). This work item was the earlier
duplicate filing of the same bug. Both `dispatch_rpc_response()` and
`dispatch_rpc_request()` now reject negative or `> UINT32_MAX` msgids with a
`LogCategory::Rpc` warning before the `uint32_t` cast.

The missing piece — a regression test — was added here:

- `tests/rpc_fake_server.cpp`: new `out_of_range_msgid_then_success` mode that
  emits a response whose msgid is the negative int64 `-1`, immediately followed
  by a well-formed response for the real msgid.
- `tests/rpc_integration_tests.cpp`: new `[rpc]` test case that drives the fake
  server in that mode and asserts (a) the poisoned response is discarded with an
  "out-of-range msgid" warning log, (b) the real response still completes the
  request successfully, and (c) the client does not fall back to the 5-second
  request timeout.

---

## Interdependencies

- Small, self-contained fix; can land after Phase 1 or independently.
- Related to WI 05 (reader exception handling) — both are in `rpc.cpp`.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
