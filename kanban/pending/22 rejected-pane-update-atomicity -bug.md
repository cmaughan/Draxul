# Keep rejected pane updates free of mutations
**Severity:** MEDIUM  
**Source:** Codex #23; `libs/draxul-server/src/topology_service.cpp:1201`.

A mismatching plugin ID is rejected after changing source and working-directory fields, leaving altered topology under the old revision.

**Investigation**

- [ ] Trace validation and mutation ordering for `UpdateClientPane`, including command result and revision handling.

**Fix strategy**

- [ ] Complete immutable-identity validation before committing any field changes.

**Acceptance criteria**

- [ ] A rejected update leaves the complete snapshot and revision unchanged.
- [ ] A valid update publishes all intended fields with the expected revision advance; run core aggregate tests and same-cache smoke.
