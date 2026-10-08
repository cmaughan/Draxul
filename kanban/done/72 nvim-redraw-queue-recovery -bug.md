# Recover display state after editor queue overflow

**Summary:** Recover the complete editor display when queued updates overflow so changes are not permanently lost.

**Priority:** P1  
**Source:** `libs/draxul-nvim/src/rpc.cpp`

**Evidence and trigger:** B15; the bounded queue drops incremental notifications without notifying the host or requesting resynchronization.

- [x] **Investigate:** Trace overflow across redraw transactions, flushes, requests, and host draining.
  - `dispatch_rpc_notification()` popped the oldest queued notification at 4,096
    entries. A dropped `redraw` batch can hold `grid_line`, `hl_attr_define`,
    `grid_resize`, mode, option, or `flush` events, and Neovim never resends them,
    so the grid stayed wrong until the affected cells happened to be redrawn.
    `clipboard_set` notifications could be lost the same way.
  - Every running host is pumped each main-loop iteration (foreground via the
    render tree, background via `App::pump_background_hosts()`), and each pushed
    notification wakes the window, so a full queue is always drained promptly.
    After startup the main thread never issues `request()`; requests come from
    `UiRequestWorker`. Neovim's rpcrequests are answered on the reader thread,
    whose replies are now enqueue-only (see
    `kanban/pending/71 cancellable-nvim-output -bug.md`), so pausing the reader
    cannot deadlock the writer.
- [x] **Fix:** Preserve ordered delivery or introduce explicit invalidation and complete display recovery.
  - Chose ordered delivery: at capacity the reader thread waits on a condition
    variable for `drain_notifications()` (or `close()`) instead of evicting. Neovim
    blocks on its own output meanwhile, so nothing is discarded and memory stays
    bounded by `NvimRpc::kMaxNotificationQueueDepth`. Saturation logs one warning
    per episode. `close()` releases a paused reader so shutdown stays non-blocking.
  - Caveat documented on `NvimRpc`: a response behind a full queue waits for the
    drain, so the draining thread must not block in `request()`. Startup requests
    on the main thread precede draining but cannot plausibly see 4,096 redraw
    batches; the worst case is the existing 5 s request timeout, not data loss.
- [x] **Acceptance:** A burst beyond queue capacity cannot leave a discarded unique update permanently missing; memory remains bounded.
  - `tests/rpc_backpressure_tests.cpp`: "rpc backpressure: a burst beyond queue
    capacity pauses the reader instead of dropping" sends 5,096 notifications
    before a response. The undrained queue holds exactly 4,096 and stays there,
    the response waits, and after draining all 5,096 arrive contiguous and in
    order, with the request completing and depth never above the bound.
- [x] **Validation:** Coordinate existing notification work; run core aggregate tests and same-cache smoke.
  - Added a note to `kanban/pending/45 nvim-notification-move-and-wake -perf.md`:
    wake coalescing must keep the drain-side space signal.
  - macOS core aggregate: 54/56 passed. Failures were the known
    `draxul-do-py-tests` (no product submodules in this worktree) and
    `app dispatch: shared Neovim split retains actions until its host exists`,
    which also fails 3/4 runs with the original transport sources. Same-cache
    smoke passed. The change is platform-neutral C++; Windows coverage comes from
    the normal CI run.
