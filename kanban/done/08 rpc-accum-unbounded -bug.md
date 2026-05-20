# WI 08 — rpc-accum-unbounded

**Type:** bug  
**Priority:** MEDIUM (OOM under a corrupt RPC stream with valid-looking incomplete messages)  
**Platform:** all  
**Source:** review-bugs-consensus.md — BUG-09 (Gemini)

---

## Problem

`NvimRpc::reader_thread_func` in `libs/draxul-nvim/src/rpc.cpp` (line 386) appends all incoming bytes to `std::vector<uint8_t> accum`. The compaction guard (line 420) only fires when `read_pos > 65536`, so it never compacts if no complete message is decoded (`read_pos` stays 0). The `kMaxConsecutiveInvalidDiscards` guard (line 448) only catches hard msgpack byte errors, not partial-message stalls. A malformed RPC stream that sends a valid-looking msgpack array header claiming a large payload but never delivers the body bytes will grow `accum` indefinitely until OOM.

---

## Investigation

- [x] Read `libs/draxul-nvim/src/rpc.cpp` lines 380–470 (the reader loop) to confirm the compaction condition and the accum growth path.
- [x] Confirm that the `!hard_error` path on truncated messages just `break`s the inner loop without any size limit check.
- [x] Determine a reasonable maximum buffer size (current `read_buf_` is 256 KB; 256 MB seems a safe upper bound).

---

## Fix Strategy

- [x] Add a max-size guard in the outer read loop, after appending to `accum`:
  ```cpp
  constexpr size_t kMaxAccumBytes = 256ULL * 1024 * 1024; // 256 MB
  accum.insert(accum.end(), impl_->read_buf_.begin(), impl_->read_buf_.begin() + n);
  if (accum.size() > kMaxAccumBytes) {
      DRAXUL_LOG_ERROR(LogCategory::Rpc,
          "RPC accumulation buffer exceeded %zu bytes; stream is corrupt, aborting reader",
          kMaxAccumBytes);
      impl_->read_failed_ = true;
      impl_->running_ = false;
      impl_->response_cv_.notify_all();
      if (callbacks_.on_notification_available)
          callbacks_.on_notification_available();
      return;
  }
  ```

---

## Acceptance Criteria

- [x] Reader thread aborts with an error log when `accum` exceeds the configured limit.
- [x] Normal operation (large but valid redraw events) is not affected.
- [x] Build and smoke test pass: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
