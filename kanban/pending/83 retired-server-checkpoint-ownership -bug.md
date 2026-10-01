# Prevent retired servers from publishing checkpoints
**Summary:** Stop retired servers from replacing newer saved layouts so restarting preserves the replacement server’s changes.

**Priority:** 83  
**Severity:** CRITICAL  
**Source:** `libs/draxul-server/src/server_kernel_lifecycle.cpp`  
**Reported by:** Claude C2; consensus F02.

**Evidence and trigger:** Eviction exits at line 414 but continues into queued requests and checkpoint scheduling at lines 651 and 664. Detached writers retain shared destinations. A successor checkpoint can be overwritten by stale state.

**Related:** `kanban/pending/09 server-checkpoint-state -refactor.md`; `kanban/pending/10 session-model-store -refactor.md`.

- [ ] **Investigate:** Trace endpoint ownership, eviction, final saves, already-running writers, and temporary-file publication.
- [ ] **Fix:** Distinguish eviction from normal shutdown and suppress post-eviction mutations and saves.
- [ ] **Fix:** Prevent outstanding retired-owner writes from publishing over successor state.
- [ ] **Acceptance:** A successor’s saved layout survives old-server retirement, including a controlled in-flight old writer.
- [ ] **Acceptance:** Ordinary graceful shutdown still saves the latest accepted state.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; exercise ownership replacement on supported platform paths.
