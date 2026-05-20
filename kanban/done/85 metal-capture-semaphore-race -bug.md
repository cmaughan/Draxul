# WI 85 — metal-capture-semaphore-race

**Type:** bug  
**Priority:** 0 (race condition / corrupted capture output — macOS only)  
**Source:** review-bugs-consensus.md §C4 [Claude]  
**Produced by:** claude-sonnet-4-6

---

## Problem

In `libs/draxul-renderer/src/metal/metal_renderer.mm` (lines 669–688), when a frame capture is requested:

1. `addCompletedHandler` schedules a block on a Metal background thread to signal `frame_semaphore_` when the GPU finishes.
2. After `[cmdBuf commit]`, the main thread calls `[cmdBuf waitUntilCompleted]` and then reads from `capture_buffer_`.
3. The completion handler can fire (signaling the semaphore) concurrently with `waitUntilCompleted` returning on the main thread.
4. The next frame's `begin_frame()` can then acquire the semaphore and begin writing GPU resources — including `capture_buffer_` — while the readback loop on the main thread is still in progress.

This produces corrupt or partially-overwritten render-test snapshots and is a formal data race on the capture buffer.

---

## Investigation

- [ ] Read `libs/draxul-renderer/src/metal/metal_renderer.mm:650–700` — trace the exact sequence of `commit`, `addCompletedHandler`, `waitUntilCompleted`, and readback.
- [ ] Confirm `capture_buffer_` is the same MTLBuffer used for per-frame GPU writes (verify it is not a separate staging buffer).
- [ ] Check `begin_frame()` to see what resources are written after acquiring the semaphore — confirm overlap with the capture readback.

---

## Fix Strategy

- [ ] In the capture code path, do **not** register `addCompletedHandler` for semaphore signaling; instead, signal the semaphore manually on the main thread after the readback loop completes:
  ```mm
  [cmdBuf commit];
  if (capture_requested_) {
      [cmdBuf waitUntilCompleted];
      // ... readback loop ...
      dispatch_semaphore_signal(frame_semaphore_.get()); // after readback
  }
  ```
- [ ] In the non-capture path, keep `addCompletedHandler` for normal async signaling.
- [ ] Build: `cmake --build build --target draxul draxul-tests`
- [ ] Run render snapshot tests: `py do.py smoke` and verify blessed snapshots still match.

---

## Acceptance Criteria

- [ ] Render-test snapshots are deterministic under repeated runs.
- [ ] No race between capture readback and next-frame GPU writes.
- [ ] Smoke and render-test suites pass.

---

## Interdependencies

- **WI 87** (metal-grid-handle-uaf-shutdown) — both modify `metal_renderer.mm`; combine into one Metal patch.
