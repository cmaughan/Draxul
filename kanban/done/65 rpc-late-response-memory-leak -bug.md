# Bug: Late-arriving RPC responses accumulate in responses_ map after timeout

**Severity**: MEDIUM
**File**: `libs/draxul-nvim/src/rpc.cpp:138`
**Source**: review-bugs-consensus.md (M6)

## Description

When `NvimRpc::request()` times out at line 138, it erases the `msgid` entry from `responses_` and returns an error result. If Neovim later sends a response for that same `msgid`, `dispatch_rpc_response` re-inserts the response data into `responses_`. Since the original caller has already returned, nothing ever removes this entry. Under repeated RPC timeouts (slow filesystem I/O, heavy computation, startup delays), the map grows unboundedly, leaking the response data.

## Trigger Scenario

Any RPC call that times out while Neovim eventually responds. Common in:
- Slow filesystem during startup
- Heavy Neovim computation (LSP indexing, large file open)
- Any operation exceeding `kRpcRequestTimeout`

## Fix Strategy

- [x] Add a `std::unordered_set<uint32_t> timed_out_msgids_` protected by `response_mutex_`
- [x] On timeout in `request()`: after erasing from `responses_`, insert `msgid` into `timed_out_msgids_`
- [x] In `dispatch_rpc_response`: if the `msgid` is in `timed_out_msgids_`, erase it from the set and discard the response (do not insert into `responses_`)
- [x] Bound the size of `timed_out_msgids_` (it should be small; log a warning if it grows large)

## Acceptance Criteria

- [x] Unit test: simulate a timeout then a late response; verify `responses_` does not grow
- [x] Memory usage is stable after a long session with occasional RPC timeouts
- [x] Normal request/response flow is unaffected
