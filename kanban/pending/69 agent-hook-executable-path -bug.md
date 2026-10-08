# Use the supplied executable location in agent hooks

**Summary:** Use the supplied application location so agent conversations can be reported outside the command search path.

**Priority:** P1  
**Source:** `libs/draxul-agent-integration/src/agent_integration.cpp`

**Evidence and trigger:** B12; generated hooks invoke bare `draxul` despite receiving its executable location from the server.

- [x] **Investigate:** Trace executable environment propagation and command construction for both agent integrations.
- [x] **Fix:** Prefer the supplied location and pass paths safely, including spaces and non-English characters.
- [x] **Acceptance:** Both integrations report native conversation references when the application is absent from the command search path.
- [ ] **Validation:** Check Windows and macOS hook behavior; run core aggregate tests and same-cache smoke.

## Progress

- [x] Hooks (POSIX and PowerShell, Codex and Claude) call `$DRAXUL_EXECUTABLE`,
      which server and local panes already inject, and fall back to `draxul`;
      integration version bumped to 3 so older installs show as outdated.
- [x] macOS: installed hook invoked a stand-in executable whose path contains a
      space, with `draxul` absent from PATH.
- [x] macOS end to end: `installed agent hooks report native sessions through
      the pane executable off PATH` (`tests/server_topology_terminal_tests.cpp`,
      `draxul-test-server-integration`) installs the real Codex and Claude
      hooks, runs each with `env -i PATH=/usr/bin:/bin` and `DRAXUL_EXECUTABLE`
      pointing at the real `draxul` through a path with spaces and non-English
      characters (`Dräxul bïn/draxul app`), and checks that the server records
      each native session reference on its managed pane (3.5 s).
- [ ] Windows: confirm the PowerShell hook reports through `DRAXUL_EXECUTABLE`.
      Not runnable on macOS; the end-to-end case above is POSIX-only because it
      drives the `sh` hooks. A Windows equivalent would run the `.ps1` hooks
      with a stripped `PATH` against a copied `draxul.exe`.

### Commit validation — 2026-10-05

- [x] `python3 do.py test release`: 56/56 core CTest entries passed in 20.18 s.
      Reused the existing Release cache; no configure/generate step. The single
      `draxul-tests-core` aggregate build took 4.73 s (build lease timestamps).
- [x] `python3 do.py smoke release --skip-build`: same-cache Release startup
      passed with Metal initialized; smoke duration was not separately recorded.
      No repeated focused tests, render snapshots, or remote CI wait were added.
- Windows hook execution and full native-session acceptance remain open; these
  local checks do not complete the platform-specific gates above.

### Commit validation — 2026-10-08 (macOS, Debug)

- [x] Focused: the new `draxul-test-server-integration` case passed (22 assertions).
- [x] `python3 do.py test debug`: `draxul-test-server-integration` and
      `draxul-test-agent-integration` passed; the aggregate's unrelated
      failures are listed in `kanban/done/62 topology-durable-limits -bug.md`.
- [x] `python3 do.py smoke debug --skip-build`: passed.
- Windows hook execution remains open, so Validation stays unchecked.
