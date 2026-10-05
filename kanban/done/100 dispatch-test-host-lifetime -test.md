# Resolve live hosts in the shared Neovim dispatch regression test

**Priority:** P2
**Summary:** The shared Neovim split test retained a raw board-host pointer across topology rollback, causing invalid reads and a crash instead of a reliable regression check.

- [x] Replace the retained pointer with lookup of the live board host after event pumping.
- [x] Verify every dispatch scenario, then rerun the core aggregate and startup smoke.

Found while validating `kanban/done/99 readable-agent-instance-ids -feature.md`. The first Release aggregate passed 55 of 56 entries; app shard 0 timed out in rejection scenario 2 and read freed host storage in projection-retry scenario 3 (an impossible key-event count followed by SIGSEGV). The test's fixture registry records creation history, not live ownership. Resolve the board through the current Space/tab pane tree rather than dereferencing a pointer saved before projection/recovery. Product code is unchanged by this fix.

The rejection case also treated `run_smoke_test(100ms)` as a fixed pumping duration, but it returns as soon as a frame is ready. The test now explicitly pumps for that drain interval before retrying, and yields briefly between condition polls. All five scenarios passed five repeated runs in 4.33 seconds. The final core aggregate passed all 56 entries (19.65 seconds), followed by a passing same-cache Release startup smoke (1.09 seconds). No product behavior changed for this test repair.
