# Keep live session changes within saving limits

**Summary:** Keep accepted session changes within saving limits so successful changes survive a restart.

**Priority:** P1  
**Source:** `libs/draxul-server/src/topology_service.cpp`

**Evidence and trigger:** B10; live names, Space counts, tab counts, and split depth can exceed durable limits, preventing subsequent checkpoints.

- [x] **Investigate:** Compare every relevant live mutation limit with current-format session validation.
- [x] **Fix:** Share bounds and validate before publishing mutation side effects.
- [x] **Acceptance:** Over-limit names and layouts are rejected without changes; accepted boundary values save and restore successfully.
- [x] **Validation:** Run core aggregate tests and same-cache smoke.

## Progress

- [x] Confirmed in current source: live mutations allowed 128 Spaces, 256 tabs
      per Space, 4096-byte names, unvalidated pane renames, and split chains up
      to 256 deep, while checkpoint validation accepts 64 Spaces, 128 tabs,
      512-byte names/ids/host kinds/agent fields, and 64 layout levels. Pane
      counts (256) and path/plugin text (4096 <= 8192) already fit.
- [x] The durable bounds are now public `kSessionStateMax*` constants in
      `draxul/session_state.h`, used by the checkpoint validator and by
      `TopologyService`. Commands, `topology.layout_apply` (including a
      plan-time split-depth check), and managed-agent launches check them
      before any terminal is created or the snapshot changes. Over-long
      Space/tab/pane names return `invalid_name`; count and depth overflows
      return `limit_reached`. The wider wire limits in `topology_protocol.h`
      are unchanged.
- [x] Added `server keeps accepted topology changes within durable Session
      limits` (`tests/server_session_checkpoint_tests.cpp`): it fills a Session
      to 64 Spaces, 128 tabs, and a 64-level split with 512-byte names, checks
      that each over-limit step is rejected without a revision change, waits
      for a published checkpoint, and cold-restores the boundary topology in a
      second server.

### Validation — 2026-10-08 (macOS, Debug)

- [x] Focused: `draxul-test-server` new case passed (437 assertions, 11.9 s).
- [x] `python3 do.py test debug`: 52/56 CTest entries passed (179 s). Failures
      were unrelated to this change: `draxul-do-py-tests` (known; worktree has
      no product submodules), `draxul-test-app-shard-0` (`app dispatch: shared
      Neovim split retains actions` fails deterministically in this worktree;
      it uses a fake topology server, not `TopologyService`), core shard 1
      (remote-terminal metrics timing; passed on re-run), and server shard 1
      (real-shell exit timing in `server-owned shell survives every client
      detaching`; failed twice under machine load, then passed in a full
      `[server]` run of all 67 cases).
- [x] `python3 do.py smoke debug --skip-build`: passed (12.7 s).
