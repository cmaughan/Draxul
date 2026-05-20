# WI 02 — metal-grid-handle-buffer-silent-fail

**Type:** bug  
**Priority:** HIGH (transient allocation failure permanently disables pane rendering with no error or recovery)  
**Platform:** macOS only  
**Source:** review-bugs-consensus.md — BUG-03 (GPT)

---

## Problem

`MetalGridHandle::upload_state()` in `libs/draxul-renderer/src/metal/metal_renderer.mm` (lines 109–123) grows its per-frame GPU buffer when `required_size` exceeds `buffer_sizes_[slot]`. The allocation (line 112–113) may return `nil` under memory pressure, but line 114 writes `buffer_sizes_[slot] = required_size` regardless. On the next frame the size check passes (`buffer_sizes_[slot] >= required_size`), no reallocation is attempted, `buffers_[slot].get()` returns nil, `[nil contents]` returns nullptr, and the existing `if (!mapped) return;` guard silently skips every grid upload. The pane goes dark permanently with no log message and no way to recover short of restarting the app.

---

## Investigation

- [x] Read `libs/draxul-renderer/src/metal/metal_renderer.mm` lines 103–128 to confirm the allocation, size-commit, and contents-check sequence.
- [x] Verify that `[nil contents]` returns nullptr (all ObjC messages to nil return zero).
- [x] Confirm there is no retry path once `buffer_sizes_[slot]` is set to `required_size` with a nil buffer.

---

## Fix Strategy

- [x] Only commit `buffer_sizes_[slot]` after verifying the allocation succeeded:
  ```cpp
  buffers_[slot].reset([renderer_->device_.get() newBufferWithLength:required_size
                                                             options:MTLResourceStorageModeShared]);
  if (!buffers_[slot]) {
      DRAXUL_LOG_ERROR(LogCategory::Renderer,
          "MetalGridHandle: buffer alloc failed (%zu bytes)", required_size);
      return;
  }
  buffer_sizes_[slot] = required_size;
  ```
- [ ] Coordinate with WI 00 (BUG-01) and WI 01 (BUG-02) — fix all three Metal allocation issues in one PR. *(deferred to merge time)*

---

## Acceptance Criteria

- [x] `upload_state()` logs an error and returns without committing the size when allocation fails.
- [x] On the next frame, the size check still fires and a retry is attempted.
- [x] Build and smoke test pass: `cmake --build build --target draxul draxul-tests && py do.py smoke`.
- [ ] No new ASan findings.
