# WI 05 — `reader_thread_func` has no exception boundary; malformed msgpack aborts process

**Type:** bug  
**Severity:** CRITICAL  
**Source:** review-bugs-latest.gemini.md (CRITICAL), review-bugs-latest.gpt.md (CRITICAL) — unanimous  
**Consensus:** review-consensus.md Phase 1

---

## Problem

`NvimRpc::reader_thread_func()` calls `dispatch_rpc_message()`, which calls `as_int()` / `as_str()` on fixed message positions after only checking the top-level array length. `MpackValue::as_int()` (and other `as_*` accessors) throw `std::bad_variant_access` when the underlying variant type does not match.

There is **no `try/catch` in `reader_thread_func()`**. A single malformed-but-valid-msgpack packet — e.g. `[1, "oops", nil, 0]` or `[2, 7, []]` — throws on the reader thread and propagates to `std::terminate`, killing the entire process.

In practice Neovim is well-behaved, but:
- A corrupted pipe (partial read joined with next packet) can produce wrong-shaped frames.
- Future nvim protocol changes or plugin abuse could trigger this.
- Fuzzing the RPC layer is currently impossible without crashing the host.

**Files:**
- `libs/draxul-nvim/src/rpc.cpp` — `reader_thread_func()` and `dispatch_rpc_message()`
- `libs/draxul-nvim/include/draxul/nvim_rpc.h` (~line 95, `as_int` variants)

---

## Implementation Plan

- [x] Wrap the body of `dispatch_rpc_message()` (or the call site in `reader_thread_func`) in a `try { ... } catch (const std::exception& e) { ... }` block.
- [x] On catch: log the malformed packet (hex dump of first N bytes if available), increment a `malformed_packet_count_` diagnostic counter, and continue the loop — do **not** rethrow.
- [x] If the malformed packet count exceeds a threshold (e.g. 10 in one session) and no valid traffic has been seen recently, mark the transport as failed and signal the main thread.
- [x] Add a unit test using the RPC fake-server harness that feeds a wrong-shaped msgpack array and asserts the reader thread continues running (delivers the subsequent valid response). Note: replay_fixture.h only builds MpackValue literals and cannot drive the reader thread directly, so the test uses the existing `rpc_fake_server` mode mechanism (new `malformed_dispatch_then_success` mode) which is the reader-thread-level equivalent.
- [x] Note: this is separate from WI 06 which fixes the main-thread crash in `ui_events.cpp`.

---

## Interdependencies

- Pair with WI 06 (ui_events type validation) — same class of bug but different thread/file.
- Pair with WI 07 (callback race) — all in `rpc.cpp`, do together to minimise churn.
- WI 15 (rpc-queue-backpressure test) will stress the reader thread after this fix.

---

*Filed by: claude-sonnet-4-6 — 2026-04-08*
