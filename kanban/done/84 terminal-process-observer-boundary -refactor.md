# Extract private agent-process observers from PTY transports

**Priority:** P2 — separates discovery scheduling from transport lifecycle and enables deterministic timing tests.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 5.  
**Owner:** One terminal-process owner; platform moves may be delegated after resource ownership is fixed.  
**Dependencies:** None among accepted refactors; retain the existing production/test targets.

**Evidence:** `libs/draxul-terminal-process/src/unix_pty_process.cpp:454–645` combines probing, discovery, timing, and publication. ConPTY has corresponding responsibilities at `:522–625` and `:986–1098`. Existing Unix/Windows tests require real processes and polling. `server_terminal_runtime.cpp:627–633` consumes only the existing observation API.

**Boundary:** Private observer collaborators within `draxul-terminal-process`, exposing internal start/activity/latest/stop operations. Platform probes and native contexts stay private. `draxul-agent` retains discovery policy. Transport classes retain spawning, I/O, resize, process/job ownership, and exit status. No new public library/API.

#### Boundary verification

- [x] Capture lazy startup, probe limits, absence semantics, refresh cadence, and stop ordering.
- [x] Document borrowed handles/file descriptors and Windows completion-key identity.
- [x] Coordinate existing `kanban/done/20 conpty-output-handle-shutdown-race -bug.md`, `kanban/done/29 conpty-process-status-query-failure -bug.md`, `kanban/done/50 pty-backpressure-shutdown-wakeup -bug.md`, and `kanban/done/61 conpty-exited-process-id -bug.md`.

#### Implementation and migration

- [x] Mechanically extract Unix observation behind a private collaborator.
- [x] Extract Windows observation while preserving job notifications and process-tree fallback.
- [x] Separate scheduling decisions from waits/probes using explicit timestamps and outcomes.
- [x] Preserve the public process API and server caller.
- [x] Stop and join observers before releasing borrowed resources.

#### Unit tests

- [x] Add deterministic debounce, activity, present/missing reconciliation, stop/reset, and publication-order tests through private internal access.
- [x] Retain real zsh/ConPTY discovery tests and server integration coverage.
- [x] Build with `cmake --build <cache> --config Debug --target draxul-test-core draxul-terminal-process-link-isolation --parallel`.
- [x] Run the registered core shards during iteration with `ctest --test-dir <cache> -C Debug -R '^draxul-test-core-shard-' --parallel 4 --output-on-failure`; document the focused Catch tag for the new observer cases.

#### Cross-platform validation

- [x] Preserve macOS foreground-group reliability.
- [x] Preserve Windows job/tree policy and existing Linux `/proc` behavior.
- [x] Verify shutdown interrupts native waits without accessing closed resources.
- [x] Run `python3 do.py test debug`, then `python3 do.py smoke debug --skip-build` on macOS.
- [x] Run the Windows equivalents.
- [x] Confirm no Vulkan/Metal/window dependency enters the process library.

#### Agent documentation/tooling

- [x] Document observer borrowing and shutdown order locally; retain canonical build guidance.
- [x] Update module-map ownership descriptions and classify tests exactly once.

#### Acceptance criteria

- [x] Observation implementation is separately owned from PTY I/O.
- [x] Scheduling edge cases can be tested without wall-clock sleeps or live agent processes.
- [x] Real discovery, transport behavior, and shutdown semantics remain compatible.

#### Validation evidence

- macOS Debug configure completed in the existing `build` cache.
- The exact focused build completed for `draxul-test-core` and
  `draxul-terminal-process-link-isolation`.
- Catch tag `[agent_process_observer]` passed 18 assertions in 2 cases.
- All 4 registered `draxul-test-core-shard-*` entries passed. The first
  sandboxed attempt could not bind local control sockets; the same binaries
  passed with local socket access.
- `python3 do.py test debug` passed all 23 selected aggregate entries, and
  `python3 do.py smoke debug --skip-build` passed from the same cache.
- Windows and Linux unit/integration validation is recorded below.

#### Windows validation evidence (2026-09-22)

- The real ConPTY integration detected a temporary `codex.exe` descendant,
  published it in probe order, observed its exit, and shut down cleanly.
- The observer stop regression interrupts an otherwise one-hour wait and resets
  publication before restart; repeated active-output ConPTY shutdown also
  completed without accessing retired handles.
- `py do.py test debug --products` passed 48/48 registered entries in the normal
  MSVC/Vulkan cache, followed by the same-cache Debug startup smoke.

#### Linux validation evidence (2026-09-22)

- WSL Ubuntu 22.04 compiled `draxul-performance`, `draxul-types`,
  `draxul-agent`, and `draxul-terminal-process` with GCC 11.4 through a narrow
  test-only CMake harness; no window or renderer target entered the build.
- The live `[unix_pty_process][agent][discovery]` case used a real interactive
  Bash PTY, resolved its foreground process group through `tcgetpgrp()`, read
  the matching process records from Linux `/proc`, recognized the versioned
  fake agent symlink, and passed all 7 assertions including clean shutdown.
- The Unix fixture now selects configuration-isolated Zsh on macOS and
  configuration-isolated Bash on Linux, matching the supported shell hosts
  without requiring Zsh to be installed in a Linux validation image.
