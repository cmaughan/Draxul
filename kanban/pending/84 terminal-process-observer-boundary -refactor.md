# Extract private agent-process observers from PTY transports

**Priority:** P2 — separates discovery scheduling from transport lifecycle and enables deterministic timing tests.  
**Proposed by:** OpenAI Codex GPT-6 Astra, finding 5.  
**Owner:** One terminal-process owner; platform moves may be delegated after resource ownership is fixed.  
**Dependencies:** None among accepted refactors; retain the existing production/test targets.

**Evidence:** `libs/draxul-terminal-process/src/unix_pty_process.cpp:454–645` combines probing, discovery, timing, and publication. ConPTY has corresponding responsibilities at `:522–625` and `:986–1098`. Existing Unix/Windows tests require real processes and polling. `server_terminal_runtime.cpp:627–633` consumes only the existing observation API.

**Boundary:** Private observer collaborators within `draxul-terminal-process`, exposing internal start/activity/latest/stop operations. Platform probes and native contexts stay private. `draxul-agent` retains discovery policy. Transport classes retain spawning, I/O, resize, process/job ownership, and exit status. No new public library/API.

#### Boundary verification

- [ ] Capture lazy startup, probe limits, absence semantics, refresh cadence, and stop ordering.
- [ ] Document borrowed handles/file descriptors and Windows completion-key identity.
- [ ] Coordinate existing `kanban/pending/20 conpty-output-handle-shutdown-race -bug.md`, `kanban/pending/29 conpty-process-status-query-failure -bug.md`, `kanban/pending/50 pty-backpressure-shutdown-wakeup -bug.md`, and `kanban/pending/61 conpty-exited-process-id -bug.md`.

#### Implementation and migration

- [ ] Mechanically extract Unix observation behind a private collaborator.
- [ ] Extract Windows observation while preserving job notifications and process-tree fallback.
- [ ] Separate scheduling decisions from waits/probes using explicit timestamps and outcomes.
- [ ] Preserve the public process API and server caller.
- [ ] Stop and join observers before releasing borrowed resources.

#### Unit tests

- [ ] Add deterministic debounce, activity, present/missing reconciliation, stop/reset, and publication-order tests through private internal access.
- [ ] Retain real zsh/ConPTY discovery tests and server integration coverage.
- [ ] Build with `cmake --build <cache> --config Debug --target draxul-test-core draxul-terminal-process-link-isolation --parallel`.
- [ ] Run the registered core shards during iteration with `ctest --test-dir <cache> -C Debug -R '^draxul-test-core-shard-' --parallel 4 --output-on-failure`; document the focused Catch tag for the new observer cases.

#### Cross-platform validation

- [ ] Preserve macOS foreground-group reliability, Windows job/tree policy, and existing Linux `/proc` behavior.
- [ ] Verify shutdown interrupts native waits without accessing closed resources.
- [ ] Run `python3 do.py test debug`, then `python3 do.py smoke debug --skip-build`, with Windows equivalents.
- [ ] Confirm no Vulkan/Metal/window dependency enters the process library.

#### Agent documentation/tooling

- [ ] Document observer borrowing and shutdown order locally; retain canonical build guidance.
- [ ] Update module-map ownership descriptions and classify tests exactly once.

#### Acceptance criteria

- [ ] Observation implementation is separately owned from PTY I/O.
- [ ] Scheduling edge cases can be tested without wall-clock sleeps or live agent processes.
- [ ] Real discovery, transport behavior, and shutdown semantics remain compatible.
