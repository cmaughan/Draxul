I reviewed the source under `app/`, `libs/`, `shaders/`, `tests/`, and `scripts/` by reading files directly. Findings below, ranked by severity.

---

# Bug review — Draxul

## CRITICAL

### 1. `libs/draxul-terminal-process/src/conpty_process.cpp:644–651` — PTY output handle closed while the reader thread is still using it

`ConPtyProcess::shutdown()` closes `output_read_` and nulls the member **before** joining the reader thread:

```cpp
if (output_read_ != INVALID_HANDLE_VALUE) {
    CancelIoEx(output_read_, nullptr);
    CloseHandle(output_read_);          // closed here
    output_read_ = INVALID_HANDLE_VALUE;
}
if (reader_thread_.joinable())
    reader_thread_.join();              // joined only afterwards
```

`reader_main()` (line 875) loops `while (reader_running_) { ReadFile(output_read_, …) }`. `output_read_` is a plain non-atomic `HANDLE` member (`conpty_process.h:68`).

**What goes wrong:** close any pane / restart any server terminal while the shell is producing output. The reader can evaluate `reader_running_` as `true`, then be descheduled while `shutdown()` runs to completion, then call `ReadFile` on a handle that was just closed. This is (a) a data race on a non-atomic `HANDLE` read concurrently with a write, and (b) a use of a closed handle — Windows recycles handle values aggressively, and Draxul opens pipes/files/sockets continuously, so the value can already refer to an unrelated kernel object whose bytes then get fed into the VT parser.

The POSIX sibling documents exactly this invariant and does the opposite (`unix_pty_process.cpp:462–466`): *"Do NOT close master_fd_ here — the reader thread may still be polling it. shutdown() will close fds after joining the reader thread."* This is a Windows/macOS divergence where only one side is correct.

**Fix:** join before closing.

```cpp
if (reader_thread_.joinable()) {
    CancelIoEx(output_read_, nullptr);   // unblock the pending ReadFile
    reader_thread_.join();
}
if (output_read_ != INVALID_HANDLE_VALUE) { CloseHandle(output_read_); output_read_ = INVALID_HANDLE_VALUE; }
```

---

## HIGH

### 2. `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:275–278` — `flush_submit_chunk()` failure is discarded, leaving an ended/submitted command buffer active

```cpp
void flush_submit_chunk() override {
    renderer_.flush_submit_chunk(false);   // bool result dropped
}
```

`VkRenderer::flush_submit_chunk(false)` (line 1247) ends and submits `active_cmd_buffer_`, then returns `start_new_chunk_command_buffer()`. It returns `false` — leaving `active_cmd_buffer_` pointing at an **already-ended, already-submitted** buffer — when `vkEndCommandBuffer` fails (1349), `vkQueueSubmit` fails (1361, e.g. `VK_ERROR_DEVICE_LOST` after a TDR), or `vkAllocateCommandBuffers` fails (967).

**What goes wrong:** a render pass calls `IRenderContext::flush_submit_chunk()` mid-frame; the submit fails; the caller keeps recording. Every subsequent `vkCmdBeginRenderPass` / `vkCmdDraw` targets a command buffer that is not in the recording state, and is concurrently in flight on the queue — undefined behaviour, validation errors, GPU hangs.

There is a second-order defect in the same path: `start_new_chunk_command_buffer` (952) increments `current_chunk_index_` **before** it can fail, and on `vkAllocateCommandBuffers` failure does `extra.pop_back()` without rolling the index back. The next call computes `extra_index = extra.size() + 1`, appends one element, and then reads `extra[extra_index]` — one past the end of the vector — producing a garbage `VkCommandBuffer` that is immediately passed to `vkResetCommandBuffer`.

**Fix:** propagate the failure and roll back the index.

```cpp
void flush_submit_chunk() override {
    if (!renderer_.flush_submit_chunk(false))
        renderer_.abort_frame();   // stop recording for the rest of this frame
}
// in start_new_chunk_command_buffer, on every failure path:
--current_chunk_index_;
return false;
```

### 3. `libs/draxul-terminal-process/src/conpty_process.cpp:124–176, 546–549` — every spawn hides *desktop-wide* console windows for 8 seconds

`console_window_snapshot()` uses `EnumWindows`, which enumerates every top-level window on the desktop, not just this process's. `spawn()` then detaches a thread that re-enumerates every 1 ms for 8 s and calls `ShowWindowAsync(hwnd, SW_HIDE)` + `SetWindowPos(..., SWP_HIDEWINDOW)` on **any** `ConsoleWindowClass` window not present in the pre-spawn snapshot.

**What goes wrong:** open a pane (or let a pane restart) in Draxul, then within 8 seconds double-click `cmd.exe` from Explorer, or have any other application open a console. That unrelated window is hidden and never restored — from the user's point of view the app silently vanished. Restarting several panes also leaves several 8-second 1 ms-polling threads running concurrently.

**Fix:** filter to the spawned child's own process. After `CreateProcessW`, only hide windows whose `GetWindowThreadProcessId` matches `proc_info_.dwProcessId` (or a descendant), and stop the loop as soon as one is hidden.

### 4. `libs/draxul-terminal-core/src/terminal_core_csi.cpp:303–321` — CHT/CBT loop bounded only by the untrusted CSI parameter

```cpp
case 'I': { // CHT
    const int n = param_or(params, 0, 1);
    for (int i = 0; i < n; ++i)
        vt_.col = std::min(std::max(0, grid_cols() - 1), ((vt_.col / 8) + 1) * 8);
    …
case 'Z': { // CBT
    const int n = param_or(params, 0, 1);
    for (int i = 0; i < n; ++i) { … }
```

`handle_csi` parses parameters with `std::from_chars` into an `int` with no upper clamp, and the CSI buffer cap (`vt_parser.cpp`, `kMaxCsiBuffer`) is far larger than the 10 digits needed. Both loops saturate `vt_.col` on the first iteration but still run `n` times.

**What goes wrong:** any program in a pane emitting `printf '\033[2000000000I'` (or a corrupt/hostile stream) spins the loop ~2×10⁹ times. On a local pane that freezes the UI thread; on the shared server this runs inside `ServerTerminalRuntime::pump()` on the single event loop, stalling every session and the control plane. Repeating the sequence extends the stall arbitrarily.

**Fix:** clamp the repeat to the number of reachable tab stops.

```cpp
const int n = std::min(param_or(params, 0, 1), std::max(1, grid_cols() / 8 + 1));
```

---

## MEDIUM

### 5. `libs/draxul-grid/src/grid.cpp:255–288` — overwriting the left half of a double-width pair orphans the old continuation cell

`set_cell` clears the right neighbour only when that neighbour is itself a *continuation*:

```cpp
if (col + 1 < cols_) {
    auto& next = cells_[index + 1];
    if (next.double_width_cont) { clear_continuation(next); … }   // handles cont
}
…
if (double_width && col + 1 < cols_) { next.double_width_cont = true; … }
```

It never handles the case where the neighbour is a double-width **leader**.

**What goes wrong:** a wide glyph occupies columns 5–6 (`cells_[5].double_width == true`, `cells_[6].double_width_cont == true`). A redraw writes a new wide glyph at column 4. Column 5 is converted into a continuation of column 4, but column 6 keeps `double_width_cont == true` with no leader at column 5. The orphan renders as an empty cell carrying the stale `hl_attr_id` (a mis-coloured blank), and `capture_agent_observation` / snapshot capture skip it (`server_terminal_runtime.cpp:605`, `local_terminal_host.cpp:658`), silently dropping a column from agent text reads. `Grid::scroll` has an explicit fix-up loop for exactly this orphan condition (line 507–531); `set_cell` does not.

**Fix:** in `set_cell`, when the new cell is double-width, also clear the far side of a pair it is about to truncate.

```cpp
if (double_width && col + 2 < cols_ && cells_[index + 1].double_width
    && cells_[index + 2].double_width_cont)
{
    clear_continuation(cells_[index + 2]);
    mark_dirty_index(static_cast<int>(index + 2));
}
```

### 6. `libs/draxul-terminal-process/src/conpty_process.cpp:705–713` — `is_running()` reports "exited with code 0" when the status query fails

```cpp
DWORD exit_code = 0;
GetExitCodeProcess(proc_info_.hProcess, &exit_code);   // return value ignored
if (exit_code != STILL_ACTIVE)
    last_exit_code_ = static_cast<int>(exit_code);
return exit_code == STILL_ACTIVE;
```

On failure `exit_code` keeps its initialiser `0`, which is indistinguishable from a clean exit.

**What goes wrong:** a failed query caches `last_exit_code_ = 0` permanently (`exit_code()` short-circuits on `is_running()`, and nothing ever clears it outside `spawn`). `ServerKernel::Impl::run_until_stopped` (`server_kernel.cpp:3287–3312`) treats "not running + exit code 0" as a *clean* exit and calls `close_exited_terminal`, destroying a pane whose shell is still alive.

**Fix:**

```cpp
if (!GetExitCodeProcess(proc_info_.hProcess, &exit_code))
    return false;   // unknown state; do not synthesise a clean exit
```

---

## Checked and found sound

For the record, these areas were read closely and no defect was confirmed: `remote_terminal_protocol.cpp` / `server_protocol.cpp` bounds and type validation (including the `std::vector<bool> seen` duplicate-cell guard and `read_bounded_integer`), `json_extract.h`, `unicode.h` UTF-8 decoding and `utf8_validated_prefix_length`, `CellText::assign` truncation, `ScrollbackBuffer` ring arithmetic and `resize`/`set_capacity` head recomputation, `split_tree.cpp` node lifetime during `close_leaf`, `ui_events.cpp` `grid_line` column/repeat clamping, `text_atlas_builder.cpp` `pack_labels` bounds, `rpc.cpp` reader-thread recovery and timed-out-msgid eviction, `unix_pty_process.cpp` post-`fork` async-signal-safety and fd-close ordering, `control_plane.cpp` `sun_path` length (guarded at `control_plane.cpp:1129`), and the detached checkpoint thread in `server_kernel.cpp:1724` (captures everything by value; `write_durable_session_state_file` is atomic).

Two items were deliberately excluded as already tracked in `kanban/ice-box/`: the child-side `close()` loop up to `_SC_OPEN_MAX` (`06 fd-close-loop-limit -bug.md`) and cursor-index signed overflow (`07 cursor-index-signed-overflow -bug.md`).
