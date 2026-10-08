# Use the supplied executable location in agent hooks

**Summary:** Use the supplied application location so agent conversations can be reported outside the command search path.

**Priority:** 75  
**Severity:** HIGH  
**Source:** `libs/draxul-agent-integration/src/agent_integration.cpp`

**Evidence and trigger:** B12; generated hooks invoke bare `draxul` despite receiving its executable location from the server.

- [x] **Investigate:** Trace executable environment propagation and command construction for both agent integrations.
- [x] **Fix:** Prefer the supplied location and pass paths safely, including spaces and non-English characters.
- [ ] **Acceptance:** Both integrations report native conversation references when the application is absent from the command search path.
- [ ] **Validation:** Check Windows and macOS hook behavior; run core aggregate tests and same-cache smoke.

## Progress

- [x] Hooks (POSIX and PowerShell, Codex and Claude) call `$DRAXUL_EXECUTABLE`,
      which server and local panes already inject, and fall back to `draxul`;
      integration version bumped to 3 so older installs show as outdated.
- [x] macOS: installed hook invoked a stand-in executable whose path contains a
      space, with `draxul` absent from PATH.
- [ ] Windows: confirm the PowerShell hook reports through `DRAXUL_EXECUTABLE`.

### Commit validation — 2026-10-05

- [x] `python3 do.py test release`: 56/56 core CTest entries passed in 20.18 s.
      Reused the existing Release cache; no configure/generate step. The single
      `draxul-tests-core` aggregate build took 4.73 s (build lease timestamps).
- [x] `python3 do.py smoke release --skip-build`: same-cache Release startup
      passed with Metal initialized; smoke duration was not separately recorded.
      No repeated focused tests, render snapshots, or remote CI wait were added.
- Windows hook execution and full native-session acceptance remain open; these
  local checks do not complete the platform-specific gates above.
