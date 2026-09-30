# Separate Session values from codec and storage

**Priority:** P1 — protocol inherits TOML and durable-file implementation.  
**Source:** `libs/draxul-session-model/src/session_state.cpp`  
**Proposed by:** Claude 12. **Owner:** one session/core agent. **Precedes:** card 11.  
**Evidence:** one source contains validation, TOML codec and platform file replacement; protocol links session-model publicly.

**Boundary verification**
- [ ] Trace model-only callers, codec callers and durable persistence error paths.
**Implementation and migration**
- [ ] Keep values/pure validation in `draxul-session-model`; add `draxul-session-store` for codec/path/list/IO with `store → model`. Retarget app/server/CLI explicitly.
**Unit tests**
- [ ] Isolated validation and codec round trips; retain corruption and transactional persistence cases.
**Cross-platform validation**
- [ ] Check Windows replacement and macOS/POSIX flush, headless closure, aggregate and smoke.
**Agent documentation and tooling**
- [ ] Update module map and dependency-boundary checks.
**Acceptance criteria**
- [ ] Protocol/client model consumers no longer inherit document/file dependencies.
