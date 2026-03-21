# Belt And Braces

## Notification Queue Hardening

This note captures the remaining "belt and braces" hardening around the Neovim notification path.

## Current Design

- The RPC reader thread pushes every msgpack notification into `NvimRpc::notifications_`.
- The queue is bounded at `kMaxNotificationQueueDepth` (4096) entries; when full, the oldest entry is dropped and a `DRAXUL_LOG_WARN` is emitted. A warning is also emitted when depth first reaches `kNotificationQueueWarnDepth` (512).
- The app drains the queue once per pump/frame via `drain_notifications()`.
- The app currently only consumes `redraw` notifications and ignores the rest.
- `NvimRpc::notification_queue_depth()` exposes the current queue depth for diagnostics.

Relevant code:

- `libs/draxul-nvim/include/draxul/nvim.h`
- `libs/draxul-nvim/src/rpc.cpp`
- `app/app.cpp`

## Why This Matters

The current path is correct, but it assumes the UI thread keeps up with notification production.

If that assumption stops holding, the failure mode is not data corruption, but backlog:

- redraw notifications can accumulate
- memory usage can grow without a bound
- UI latency can increase because old notifications are still waiting to be processed
- a temporary renderer stall can turn into a long recovery tail

This is the kind of issue that may never matter in normal use, but becomes annoying in long-running sessions or under load.

**Status (2026-03-21):** The unbounded-queue risk is now addressed. A hard cap of 4096 entries is enforced in the reader thread push path with oldest-drop semantics and a warn-threshold log at 512. See `libs/draxul-nvim/src/rpc.cpp`.

## Conservative Hardening Options

1. Add a maximum queue depth.

- When the queue exceeds a limit, log it and apply a defined policy.

2. Coalesce redraw notifications.

- Multiple queued redraw notifications are often less useful than the latest drawable state.
- A simple approach is to merge or replace pending redraw notifications instead of storing every one individually.

3. Treat non-redraw notifications separately.

- If future notification types become important, keep their handling distinct instead of applying redraw-specific policy to everything.

4. Add visibility.

- Log queue growth when it crosses thresholds.
- Optionally expose counters for dropped/coalesced notifications so overload behavior is observable.

## Recommended First Step

The smallest safe step is:

- add a bounded queue
- apply redraw-specific coalescing
- log when coalescing or dropping happens

That gives protection without redesigning the RPC layer.

## Non-Goals

- Do not overbuild this into a generic async framework.
- Do not add complexity unless real backlog is observed or easy safeguards are cheap.
- Keep the fast path simple for normal sessions.
