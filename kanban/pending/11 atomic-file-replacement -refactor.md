# Share staged atomic file replacement

**Priority:** P2 — several callers repeat publication stages with different guarantees.  
**Source:** `libs/draxul-host/src/plugin_storage.cpp`  
**Proposed by:** Claude 13, narrowed. **Owner:** one storage/core agent. **Depends on:** card 10 for store migration.  
**Evidence:** session, plugin storage, agent integration, config and control transport each implement temp/write/replace; plugin storage already has fault injection.

**Boundary verification**
- [ ] Record each caller’s permissions, durability, exclusive temp and post-replace error contract.
**Implementation and migration**
- [ ] Add `draxul-atomic-file` with explicit stage/options and injected operations; migrate plugin storage, Session store, agent/config, then security-sensitive control transport.
**Unit tests**
- [ ] Fault-inject before and after replacement, cleanup, directory flush and mode/ACL handling.
**Cross-platform validation**
- [ ] Verify `MoveFileExW`/ACL and POSIX mode/fsync behavior, aggregate and smoke on each OS.
**Agent documentation and tooling**
- [ ] Record caller-specific options and error mapping.
**Acceptance criteria**
- [ ] Each migrated caller retains its current security and durability guarantees.
