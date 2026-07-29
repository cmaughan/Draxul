# Server-owned Session persistence

## Outcome

Make the headless Draxul server the sole checkpoint/restore authority for every
remote Session, Space, tab, pane, and server-terminal descriptor.

## Acceptance

- [x] A renderer-neutral `draxul-session-model` owns snapshot values, validation,
      version-3 codec compatibility, and transactional file replacement.
- [x] Existing version-2/version-3 fixtures decode and re-encode without an
      unsolicited startup rewrite.
- [ ] The server restores every usable saved Session/Space before accepting clients.
- [x] Graceful shutdown and UI-free periodic checkpoints are written only by the
      server and preserve the last good file on failure.
- [x] Restored server-terminal panes receive stable descriptors but honest new
      processes/runtime generations after a cold server restart.
- [x] Client-local pane descriptors remain durable without pretending their local
      process state belongs to the server.
- [x] Server status reports checkpoint path, result, timestamp, and restore warnings.
- [ ] Focused/full Ninja Release suites, smoke, and the Slice 7 cold-restart
      demonstration pass.

## Rollback

The existing app recovery path continues reading version-3 snapshots throughout
the migration. No schema bump is allowed without explicit migration fixtures.

## Progress

- Extracted the durable model and codec below the app.
- The server loads its runtime-scoped `sessions/default.toml` without rewriting
  it at startup and writes it transactionally during graceful shutdown.
- Cold restore preserves ordered Spaces, stable pane/terminal descriptors,
  client-local host descriptors, and managed agent restore/session metadata.
- Restored server terminals remain lazy; attaching after a cold start launches
  an honest new process under the new server epoch and runtime generation.
- Dirty topology is checkpointed every 30 seconds without any UI attached; status
  and `--server-status` expose the path, state, last success, bounded failure, and
  partial-restore warnings. Failed or disabled checkpoints retain the previous bytes.
