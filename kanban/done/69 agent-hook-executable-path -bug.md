# Use the supplied executable location in agent hooks

**Summary:** Use the supplied application location so agent conversations can be reported outside the command search path.

**Priority:** P1 — Off-PATH launches prevent hooks from reporting agent sessions.
**Source:** `libs/draxul-agent-integration/src/agent_integration.cpp`

**Evidence and trigger:** B12; generated hooks invoke bare `draxul` despite receiving its executable location from the server.

- [x] **Investigate:** Trace executable environment propagation and command construction for both agent integrations.
- [x] **Fix:** Prefer the supplied location and pass paths safely, including spaces and non-English characters.
- [x] **Acceptance:** Both integrations report native conversation references when the application is absent from the command search path.
- [x] **Validation:** Check Windows and macOS hook behavior; run core aggregate tests and same-cache smoke.

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
- [x] Windows: confirm the PowerShell hook reports through `DRAXUL_EXECUTABLE`.
      The shared end-to-end case now includes Windows: it installs both real
      `.ps1` hooks into temporary provider directories and runs them against a
      copied `draxul app.exe` under `Dräxul bïn`, with an empty child `PATH`.
      Execution passed in the parent aggregate; isolated-profile same-cache
      smoke and final accounting are recorded in the closure evidence below.

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

### Windows test handoff — 2026-10-08

- Extended the existing `draxul-test-server-integration` hook case to Windows;
  retained the POSIX path and shared isolated checkpoint/server setup. Both
  providers use controlled managed identities with `ShellOnly` restoration, so
  no provider executable or API call is needed.
- Windows copies the real CLI and adjacent DLLs into a spaced/non-ASCII path,
  installs the actual generated hooks into temporary directories, and launches
  system PowerShell through `CreateProcessW`. A UTF-8 BOM preserves script paths;
  child-only environment changes isolate provider/config homes and empty PATH.
- Each hook receives the real pane, instance, Session, epoch, runtime generation
  and isolated runtime directory. Final topology assertions verify both exact
  native session references, sources, instance identities and pane associations;
  a silent hook failure cannot pass merely by returning exit zero.
- Each PowerShell invocation has a 30-second bound and an owned kill-on-close
  job for cleanup of the hook and its CLI descendants. No parent environment,
  installed user integration, default server or shared build cache is modified.
- This worker did not build or execute the test; the parent owned the shared
  validation cache. Subsequent execution and startup evidence are recorded below.

### Windows execution evidence — 2026-10-08

- Parent run `build-ninja-debug/validation-logs/20261008-150658-521060/`:
  `draxul-test-server-integration-shard-1` passed all 14 cases / 555 assertions
  in 24.08 s, with no skipped cases. Its seed was 3638335588.
- Listed the already-built executable with the same shard count/index/seed,
  `--list-tests --verbosity high`, without executing tests or rebuilding. The
  real Codex/Claude hook case is present in shard 1; the complete shard pass and
  matching 14-case count establish its execution, including final route assertions.
  Shard 0 also passed 14 cases / 2796 assertions in 9.58 s.
- Default-profile startup smoke failed the existing server deadline tracked by
  `kanban/pending/65 windows-validation-timing -test.md`. The subsequent
  same-cache isolated-profile smoke passed as recorded below.

### Closure evidence — 2026-10-08

- Parent confirmed `py do.py smoke debug --skip-build` passed after the full
  tests with APPDATA and LOCALAPPDATA set to
  `D:/dev/Draxul/build-windows-smoke-profile`. This reuses the tested Ninja Debug
  executable; default-profile startup remains tracked in
  `kanban/pending/65 windows-validation-timing -test.md`.
- Full behavioral aggregate: 73/79 CTest entries passed in 232.92 s, not a
  green aggregate. Failures outside this card were `draxul-test-server-shard-0`,
  `draxul-test-font-shard-0`, `draxul-test-core-shard-1`,
  `draxul-test-nvim-transport-shard-1`, `draxul-test-scope-integration`, and
  `draxul-test-personal-host-shard-0`. The Nvim shutdown failure and subsequent
  repair are recorded in `kanban/done/71 cancellable-nvim-output -bug.md`.
- Closed after the exact Windows hook case executed both providers and
  verified native-session route associations, plus
  isolated same-cache startup. macOS evidence is retained above. No extra focused
  runtime pass or build was performed here. Configure/compile costs belong to
  the parent's three-attempt validation record; isolated smoke duration was not
  supplied.
- Final Release startup: parent confirmed `py do.py smoke release --skip-build`
  passed exit 0 in 33.866647 s overall. Log:
  `build-ninja-debug/windows-gates/final-release-smoke-retry.log`. The first
  attempt failed during MSVC environment initialization under machine resource
  exhaustion after 68.059 s, before application startup; it is not hidden by
  the successful retry. Release includes the Nvim shutdown correction.
