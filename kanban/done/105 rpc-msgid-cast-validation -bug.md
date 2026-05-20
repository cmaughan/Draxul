# WI 105 — rpc-msgid-cast-validation

**Type:** bug  
**Priority:** 2 (response misrouting on protocol violation)  
**Source:** review-bugs-consensus.md §M2 [Claude]  
**Produced by:** claude-sonnet-4-6

---

## Problem

`NvimRpc::dispatch_rpc_response()` and `dispatch_rpc_request()` in
`libs/draxul-nvim/src/rpc.cpp:212,240` cast the msgpack message ID directly from `int64_t` to
`uint32_t` without range checking:

```cpp
uint32_t msgid = (uint32_t)msg_array[1].as_int();   // line 212
auto req_msgid = (uint32_t)msg_array[1].as_int();   // line 240
```

A negative value from a corrupted stream truncates silently and could collide with a valid in-flight
`msgid`, causing the wrong waiting thread to be unblocked with another request's response (incorrect
result, data corruption).

A value `> UINT32_MAX` truncates to zero, which is not a valid `msgid` (the counter starts at 1)
but would still silently discard the response.

**Trigger:** Corrupted msgpack stream, a protocol mismatch with a future Neovim version, or
deliberate protocol fuzzing.

---

## Investigation

- [ ] Read `libs/draxul-nvim/src/rpc.cpp:209–253` — confirm both cast sites and their context.
- [ ] Check that `next_msgid_` starts at 1 and never wraps to negative in normal usage.
- [ ] Confirm that neither caller checks the return value of `dispatch_rpc_response` / `dispatch_rpc_request` — both are void, so early-return on invalid ID is safe.

---

## Fix Strategy

Add range validation before the cast at both sites:

```cpp
// dispatch_rpc_response (line 212):
int64_t raw_id = msg_array[1].as_int();
if (raw_id < 0 || raw_id > static_cast<int64_t>(UINT32_MAX))
{
    DRAXUL_LOG_WARN(LogCategory::Rpc,
        "dispatch_rpc_response: out-of-range msgid %lld; discarding",
        static_cast<long long>(raw_id));
    return;
}
uint32_t msgid = static_cast<uint32_t>(raw_id);
```

Apply the same pattern to line 240 in `dispatch_rpc_request`.

- [x] Apply both fixes.
- [x] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run: `ctest --test-dir build -R rpc`
- [ ] Run smoke test: `py do.py smoke`

---

## Acceptance Criteria

- [ ] Negative or `> UINT32_MAX` msgids are logged and discarded rather than silently truncated.
- [ ] No C-style `(uint32_t)` casts on msgid values in `rpc.cpp`.
- [ ] RPC unit tests pass.
- [ ] Smoke test passes.

---

## Interdependencies

None — isolated change in `rpc.cpp`. Can be batched with the WI 89 / WI 101 / WI 103 `rpc.cpp`
pass for convenience.
