All 18 work items (WI 82–99) and the consensus file are in place. Here's a summary:

---

## What was produced

**`plans/reviews/review-bugs-consensus.md`** — verified triage of all three agent reviews.

### 5 FALSE POSITIVES dismissed
- Claude BUG-14 (scrollback restore): code is correct — the `idx < size()` guard gives blank cells as intended
- Gemini BUG-2 (App data race): `on_notification_available` → `wake_window()` → SDL push, never writes `frame_requested_`
- Gemini BUG-6 (dirty tracking broken): `mark_cell_dirty` correctly sets `dirty_cell_begin_ = index` on the first dirty cell via the `!has_dirty_cells()` branch
- Claude BUG-21 (`is_running` reaps child): `shutdown()` calls `waitpid(WNOHANG)` first; gets `ECHILD`, skips SIGTERM — correct behavior
- Claude BUG-7 (signed index): max index at `kMaxGridDim=10000` is 100M < INT_MAX, no practical risk

### 18 Work Items filed (WI 82–99)

| WI | Severity | Description |
|----|----------|-------------|
| 82 | CRITICAL | Palette OOB write — `grid_rows < 2` or `grid_cols < 5` → heap corruption |
| 83 | CRITICAL | SDL clipboard called from RPC reader thread (data race) |
| 84 | CRITICAL | ConPTY `quote_windows_arg` drops trailing backslashes → arg corruption |
| 85 | CRITICAL | Metal capture semaphore signaled before readback completes |
| 86 | CRITICAL | Signed int overflow in `CapturedFrame::valid()` and `bmp.cpp` row offsets |
| 87 | HIGH | `MetalGridHandle` dangling reference to parent renderer (UAF on shutdown) |
| 88 | HIGH | PTY reader breaks on `POLLHUP` before draining `POLLIN` — data loss |
| 89 | HIGH | `on_notification_available` called under `notif_mutex_` — deadlock risk |
| 90 | HIGH | Child process inherits `SIGPIPE = SIG_IGN` — breaks shell pipelines |
| 91 | HIGH | SDL3 file-drop data pointer never freed — memory leak per drop |
| 92 | HIGH | O(4096) `vector::erase(begin())` under `notif_mutex_` — main-thread stall |
| 93 | HIGH | Windows `WideCharToMultiByte` undersized buffer — all CLI args silently empty |
| 94 | MEDIUM | Unbounded `repeat` in `grid_line` — CPU DoS |
| 95 | MEDIUM | `double_width_cont` at column 0 never cleared — rendering corruption |
| 96 | MEDIUM | `HighlightTable::attrs_` never pruned on compaction — unbounded growth |
| 97 | MEDIUM | `NvimRpc::notify()` ignores write failure — silent input discard |
| 98 | MEDIUM | Scrollback buffer wiped entirely on horizontal resize |
| 99 | MEDIUM | `vt_parser` calls `flush_plain_text()` per character — O(K²) for clusters |
