# WI 105 — rpc-timed-out-duplicate-response

**Type:** bug
**Priority:** 3 (MEDIUM — memory leak when Neovim sends duplicate responses for a timed-out request)
**Source:** review-consensus.md §2 [Gemini]
**Produced by:** claude-sonnet-4-6

---

## Problem

WI 65 (complete) added `timed_out_msgids_` to prevent late-arriving RPC responses from leaking into the `responses_` map. The fix removes a `msgid` from `timed_out_msgids_` when the *first* late response arrives.

If Neovim (due to a bug or retry) sends a **second** response for the same timed-out `msgid`, the `msgid` is no longer in `timed_out_msgids_`, so `dispatch_rpc_response` treats it as a legitimate pending response and inserts it into `responses_`. No caller is waiting, so it leaks there indefinitely — the same bug WI 65 was meant to fix, triggered by a second response.

Additionally, `timed_out_msgids_` accumulates entries for requests whose response *never* arrives; the current size-bound only logs a warning and does not evict entries.

**File:** `libs/draxul-nvim/src/rpc.cpp:150–170`

---

## Investigation

- [ ] Read `libs/draxul-nvim/src/rpc.cpp:140–175` — confirm the WI 65 fix and understand the exact remove-on-first-hit behaviour.
- [ ] Check whether the `timed_out_msgids_` growth-warning logic caps the set size or merely logs.

---

## Fix Strategy

- [ ] Do **not** erase from `timed_out_msgids_` on first hit. Instead, keep the ID in the set until the set is pruned (e.g., LRU eviction when size exceeds N).
  ```cpp
  if (impl_->timed_out_msgids_.count(msg_id)) {
      // discard — do NOT erase, a second response might arrive
      return;
  }
  ```
- [ ] Replace the unbounded `unordered_set` with a fixed-capacity ring buffer or small LRU set (e.g., last 64 timed-out IDs). Entries not yet evicted will absorb duplicate responses; evicted entries are so old that a response for them is negligible.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run: `ctest --test-dir build -R rpc`

---

## Acceptance Criteria

- [ ] A second late response for the same timed-out msgid does not insert into `responses_`.
- [ ] `timed_out_msgids_` memory is bounded regardless of session length.
- [ ] Existing RPC unit tests pass.

---

## Interdependencies

- **WI 100** (rpc-callbacks-init-order-race) — both modify `rpc.cpp`; consider batching.
- **WI 89** (rpc-notification-callback-under-lock) — also modifies `rpc.cpp`; batch in one pass if convenient.
