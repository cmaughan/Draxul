Here is the full bug report from the codebase scan:

---

# Draxul Bug Report

## CRITICAL

---

### BUG-1: Data race — clipboard access from reader thread
**File:** `libs/draxul-nvim/src/rpc.cpp:248`, `libs/draxul-host/src/nvim_host.cpp:392–397`

`dispatch_rpc_request()` is called on the reader thread. The `on_request` callback (set in `NvimHost::initialize_host()`) delegates to `NvimHost::handle_rpc_request()`, which calls `window().clipboard_text()` → `SDL_GetClipboardText()`. SDL clipboard access is main-thread-only; calling it from the reader thread is a data race with any main-thread SDL event processing. Fires whenever neovim calls `rpcrequest` for paste operations.

**Fix:** Marshal clipboard replies to the main thread via the notification queue (promise/future), or cache clipboard text on the main thread behind a mutex.

---

### BUG-2: Signed integer overflow in `CapturedFrame::valid()`
**File:** `libs/draxul-types/include/draxul/types.h:92`

`valid()` computes `width * height * 4` as `int * int * int`. The cast to `size_t` happens *after* the signed multiplication, so sufficiently large frames produce UB. The comparison against `rgba.size()` then gives a wrong result.

**Fix:**
```cpp
return width > 0 && height > 0
    && rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
```

---

### BUG-3: Signed integer overflow in BMP row offset calculation
**File:** `libs/draxul-types/src/bmp.cpp:137–141`

`src_y * width * 4` is computed as `int * int * int` then cast to `size_t`. For `width ≥ 46341` the intermediate product overflows, yielding an out-of-bounds read.

**Fix:**
```cpp
const auto src_row = pixel_offset + static_cast<size_t>(src_y) * static_cast<size_t>(width) * 4u;
const auto dst_row = static_cast<size_t>(y) * static_cast<size_t>(width) * 4u;
```

---

### BUG-4: Signed integer overflow in `restore_cursor` / `apply_cursor` used as array index
**File:** `libs/draxul-renderer/src/renderer_state.cpp:242,259`

`int idx = cursor_row_ * grid_cols_ + cursor_col_` is computed as signed `int * int`. With `kMaxGridDim = 10000` the product is 100M, which fits today, but `idx` is immediately used as a `size_t` array index and the pattern is a UB time-bomb if `kMaxGridDim` ever increases past ~46341.

**Fix:**
```cpp
const size_t idx = static_cast<size_t>(cursor_row_) * static_cast<size_t>(grid_cols_) + static_cast<size_t>(cursor_col_);
```

---

### BUG-5: Frame semaphore signaled before `waitUntilCompleted` readback finishes
**File:** `libs/draxul-renderer/src/metal/metal_renderer.mm:669–678`

`addCompletedHandler` signals the frame semaphore on a Metal background thread. `waitUntilCompleted` blocks the main thread, but the completion handler fires (and signals the semaphore) as soon as the GPU finishes — potentially allowing the next `begin_frame()` to acquire the semaphore and begin writing to shared frame resources while the readback from `capture_buffer_` is still in progress on the main thread.

**Fix:** Either remove `addCompletedHandler` in the capture path and manually signal the semaphore *after* readback, or use a separate dedicated capture semaphore.

---

## HIGH

---

### BUG-6: `on_notification_available()` called while holding `notif_mutex_`
**File:** `libs/draxul-nvim/src/rpc.cpp:283–284`

The callback is invoked under the lock. If `wake_window()` ever acquires any mutex that the main thread holds while calling `drain_notifications()` (which also takes `notif_mutex_`), the result is deadlock. Currently safe with SDL event push, but fragile.

**Fix:** Release `notif_mutex_` before calling `on_notification_available()`, consistent with the EOF/error path at line 329.

---

### BUG-7: `size_t` cell index cast to `int` in `mark_dirty_index`
**File:** `libs/draxul-grid/src/grid.cpp:344`

Callers cast `size_t` indices to `int` before passing to `mark_dirty_index`, and `index % cols_` / `index / cols_` operate as signed integers. Safe at `kMaxGridDim = 10000` (100M < `INT_MAX`), but silently truncates if the limit grows past ~46341.

**Fix:** Change `mark_dirty_index` to take `size_t` throughout.

---

### BUG-8: Unbounded `repeat` count in `handle_grid_line` — CPU DoS
**File:** `libs/draxul-nvim/src/ui_events.cpp:264–279`

`repeat` is read from msgpack with only a lower-bound check (`< 1`). A malformed or adversarial nvim can send `repeat = 65536`, causing 65536 iterations of `grid_->set_cell()` per cell batch, stalling the main thread.

**Fix:**
```cpp
repeat = std::clamp(repeat, 1, std::max(1, grid_cols - col));
```

---

### BUG-9: `MetalGridHandle` stores raw reference to parent renderer — use-after-free on abnormal shutdown
**File:** `libs/draxul-renderer/src/metal/metal_renderer.mm:40–51`

`MetalGridHandle` stores `MetalRenderer& renderer_` by reference. If a handle outlives its parent renderer (e.g., a host-owned `unique_ptr<IGridHandle>` destroyed after the renderer shuts down), the destructor dereferences `renderer_.grid_handles_` through a dangling reference. Same pattern exists in `VkGridHandle`.

**Fix:** In `MetalRenderer::shutdown()`, call `retire()` on all handles and clear `grid_handles_` before destroying renderer resources.

---

### BUG-10: O(N) `vector::erase(begin())` under `notif_mutex_` — main-thread starvation
**File:** `libs/draxul-nvim/src/rpc.cpp:269`

When the 4096-element notification queue is full, the oldest entry is dropped with `notifications_.erase(notifications_.begin())` — an O(4096) move — while holding `notif_mutex_`. The main thread also acquires this mutex in `drain_notifications()`, so sustained nvim output can starve it.

**Fix:** Replace `std::vector` with `std::deque` or a ring buffer so pop-from-front is O(1).

---

## MEDIUM

---

### BUG-13: `double_width_cont` at column 0 never cleared on overwrite
**File:** `libs/draxul-grid/src/grid.cpp:177–185`

The double-width continuation cleanup is guarded by `col > 0`. A continuation cell at column 0 (e.g., left behind by a resize) is never cleared when that cell is subsequently overwritten, leaving orphaned `double_width_cont` state that affects glyph rendering.

**Fix:** Add a separate clause to clear `cell.double_width_cont` when `col == 0`.

---

### BUG-14: `ScrollbackBuffer::restore_live_snapshot` iterates beyond `live_snapshot_rows_`
**File:** `libs/draxul-host/src/scrollback_buffer.cpp:117–121`

The loop runs `for (int r = 0; r < rows; ++r)` using the *current* grid row count, but the snapshot was captured with `live_snapshot_rows_`. The `idx < live_snapshot_.size()` fallback silently returns wrong cells from the end of the snapshot buffer instead of blank cells when `r >= live_snapshot_rows_`.

**Fix:** Add `if (r >= live_snapshot_rows_) { /* fill blank */ continue; }` at the top of the loop body.

---

### BUG-15: POSIX write loop ignores `EPIPE`; caller ignores write return value
**File:** `libs/draxul-nvim/src/nvim_process.cpp:376–386`

On POSIX, if nvim exits and the pipe breaks, `write()` returns `-1`/`EPIPE`. The process loop returns `false`, but `NvimRpc::notify()` ignores the return value of `process_->write()`, silently discarding all subsequent input events.

**Fix:** In `NvimRpc::notify()`, check the return value of `process_->write()` and set `impl_->read_failed_ = true` on failure.

---

### BUG-16: `flush_plain_text()` called after every character — O(K²) for multi-byte clusters
**File:** `libs/draxul-host/src/vt_parser.cpp:90–98`

In the `Ground` state, `flush_plain_text()` is called after every character push. For a K-byte Unicode cluster (e.g., a multi-codepoint emoji), each call rescans from offset 0, making the total work O(K²).

**Fix:** Accumulate characters and call `flush_plain_text()` only on control characters, escapes, or end-of-input.

---

### BUG-21: `is_running()` reaps the child via `waitpid`, breaking `shutdown()`
**File:** `libs/draxul-nvim/src/nvim_process.cpp:397–404`

`is_running()` calls `waitpid(WNOHANG)`. If the child has already exited, `waitpid` **reaps** it. The subsequent `waitpid` in `shutdown()` then returns `-1`/`ECHILD`, causing the code to send SIGTERM/SIGKILL to an already-gone PID and sleep 500ms unnecessarily.

**Fix:** Set a `child_exited_` bool when `is_running()` reaps the child, and skip the `waitpid`/SIGTERM path in `shutdown()` when that flag is set.
