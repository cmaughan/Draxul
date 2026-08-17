# Give the server a survivable lifecycle: SIGTERM and endpoint eviction

**Type:** bug
**Completed:** 2026-08-03

## Resolution

Commit `59e1e13b` made server termination signals request graceful shutdown and
checkpointing without an interactive confirmation. It also added periodic
publication checks: a server whose metadata is removed or replaced retires after
two failed checks and abandons the endpoint so it cannot unlink a successor.

The behavior is implemented for POSIX signals and Windows console-control events,
with focused eviction/successor-preservation coverage and later Windows lifecycle
validation.
