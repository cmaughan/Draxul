# Remote terminal parity, scrollback, and flow control

## Outcome

Make the experimental server-owned terminal practical for ordinary shell, agent,
and full-screen terminal use while preserving independent client view state.

## Already delivered by Slices 3-4

- [x] Ordered dirty-cell deltas with complete-snapshot attach and resync fallback.
- [x] Compact shared-attribute frames with encoded-byte and event-count limits.
- [x] Bounded per-client queues, slow-observer resync, command batching, and fair polling.
- [x] Shared cursor, title, cwd, terminal modes, hyperlinks, and shell marks.
- [x] Server-owned real process, resize, input, generation, and zero-client output drain.

## Acceptance

- [x] The server retains bounded, renderer-neutral semantic scrollback for the real
      terminal and exposes versioned paged reads.
- [x] Each UI owns its scroll offset, selection, and copied text; one client scrolling
      never changes another client's live viewport or the server terminal grid.
- [x] Remote keyboard, focus, mouse, bracketed paste, alternate-screen, synchronized
      output, hyperlink, cursor, title, cwd, and shell-mark behavior match local hosts.
- [x] Server launch options explicitly own shell kind, command, cwd, environment,
      terminal dimensions, and scrollback limit; UI options own rendering and palette.
- [x] PowerShell works on Windows; Bash/Zsh work on Unix; unsupported remote shell
      kinds reject clearly. WSL remains explicit rather than silently masquerading as
      PowerShell.
- [x] Protocol capabilities describe scrollback and compression support; complete
      uncompressed snapshots remain the compatibility fallback.
- [x] Sanitized metrics expose frame bytes, delta/snapshot counts, queue resyncs,
      retained scrollback rows, and reconnect time without terminal content.
- [x] Local and remote replay/randomized convergence, high-output/slow-client stress,
      alternate-screen/Unicode/scrollback tests, and a remote render scenario pass.
- [ ] Release build, full CTest, smoke, and a live two-client full-screen/agent-style
      demonstration pass on Windows; macOS source wiring remains valid and any runtime
      execution gap is recorded.

## Validation checkpoint (2026-07-29)

- Release affected-library build passed.
- Release remote-terminal tests passed: 1,051 assertions in 16 test cases.
- Release CLI/server tests passed: 22 assertions in 3 test cases.
- Alternate-output Release executable linked and `--smoke-test` passed.
- The first Debug CTest run passed 21 of 22 entries and exposed a server runtime
  thread-affinity assertion. Runtime construction was moved onto the server state
  thread; the focused Debug host regression then passed (142 assertions in 3 cases).
- A clean full CTest rerun and the live two-window manual gate remain before moving
  this card to `done`. macOS runtime execution is not available in this workspace.

## Rollback

All new behavior stays behind `--experimental-remote-shell`. The fake endpoint,
complete snapshots, and ordinary local terminal hosts remain available.
