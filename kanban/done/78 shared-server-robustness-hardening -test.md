# Shared server robustness hardening

## Outcome

The default local client/server path has bounded client-owned state, recovers paused
UIs without losing the server terminal, and prevents one stalled IPC client from
starving the others.

## Acceptance

- [x] Goodbye and inactive-client expiry release terminal subscriptions and control.
- [x] A UI reattaches after its lease expires and survives later control transfers.
- [x] Client, Session, remote-grid, event-queue, and agent-wait growth is bounded.
- [x] Topology and agent clients recover from a lower server revision.
- [x] Windows and Unix transports each serve four concurrent clients.
- [x] Force-stop validates a representable PID and Windows holds the process identity
      through the final server epoch check.
- [x] Focused Release recovery, expiry, cap, goodbye, and two-host tests pass.

## Review notes

The detailed findings, retained boundaries, and validation evidence are recorded in
`plans/server-client-terminal-runtime.md` under the post-Slice 9 robustness review.
