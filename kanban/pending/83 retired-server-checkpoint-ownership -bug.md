# Prevent retired servers from publishing checkpoints
**Summary:** Stop retired servers from replacing newer saved layouts so restarting preserves the replacement server’s changes.

**Priority:** 83  
**Severity:** CRITICAL  
**Source:** `libs/draxul-server/src/server_kernel_lifecycle.cpp`  
**Reported by:** Claude C2; consensus F02.

**Evidence and trigger:** Eviction exits at line 414 but continues into queued requests and checkpoint scheduling at lines 651 and 664. Detached writers retain shared destinations. A successor checkpoint can be overwritten by stale state.

**Related:** `kanban/pending/09 server-checkpoint-state -refactor.md`; `kanban/pending/10 session-model-store -refactor.md`.

- [x] **Investigate:** Trace endpoint ownership, eviction, final saves, already-running writers, and temporary-file publication.
- [x] **Fix:** Distinguish eviction from normal shutdown and suppress post-eviction mutations and saves.
- [x] **Fix:** Prevent outstanding retired-owner writes from publishing over successor state.
- [ ] **Acceptance:** A successor’s saved layout survives old-server retirement, including a controlled in-flight old writer.
- [ ] **Acceptance:** Ordinary graceful shutdown still saves the latest accepted state.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; exercise ownership replacement on supported platform paths.

## 2026-10-02 implementation

Checkpoint workers now save to unique private staging paths. Only the state thread
publishes a finished stage, after checking PID, process-start token and server epoch.
Retirement skips final queued requests and checkpoint scheduling, and drops task
publication rights. Workers detached after the shutdown budget can only finish their
private files; task destruction cleans those files without touching the destination.
Ordinary shutdown still collects periodic writes and saves the final accepted revision.
Publication reuses the Session model's native atomic replacement and directory flush.

Added a controlled blocked-writer regression that replaces just the endpoint epoch,
waits for retirement, saves successor state, then releases the old writer and verifies
the successor and metadata survive. Existing failure coverage now targets the private
staging path; bounded-shutdown coverage expects no late publication. Focused persistence
validation is running; aggregate, native rendering and final startup are still pending.
