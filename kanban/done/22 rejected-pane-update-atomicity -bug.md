# Keep rejected pane updates free of mutations
**Severity:** MEDIUM
**Source:** Codex #23; `libs/draxul-server/src/topology_service.cpp:1201`.

A mismatching plugin ID is rejected after changing source and working-directory fields, leaving altered topology under the old revision.

**Investigation**

- [x] Trace validation and mutation ordering for `UpdateClientPane`, including command result and revision handling.

**Fix strategy**

- [x] Complete immutable-identity validation before committing any field changes.

**Acceptance criteria**

- [x] A rejected update leaves the complete snapshot and revision unchanged.
- [x] A valid update publishes all intended fields with the expected revision advance; run core aggregate tests and same-cache smoke.

**Implementation notes (2026-09-25, Windows)**

`UpdateClientPane` previously assigned the working directory and source path before checking the plugin identity. `TopologyService::handle` does not commit a revision on rejection, so those two assignments silently changed the live snapshot under the old revision. The plugin identity check now precedes every assignment. The server topology regression captures the complete snapshot, verifies mismatch rejection leaves it equal (including revision), then accepts a valid update and checks every changed field and the one-step revision advance.

**Validation:** Windows Debug core + all product aggregate passed 49/49 CTest
entries in 238.19 seconds, including the server topology regression. The
standard 30-second same-cache smoke timed out while restoring the existing
nine-pane Session; the identical Debug executable and wrapper environment
passed with a 90-second bound in roughly 45 seconds. The single-pane smoke
also exited successfully.
