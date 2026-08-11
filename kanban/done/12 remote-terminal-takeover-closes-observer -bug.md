# Remote terminal takeover closes observer

## Problem

When two `--experimental-remote-terminal` windows are attached, sustained input and
resizing can make one or both appear to crash. Input batching fixed one source of
starvation, but the remaining failure was a transport-size bug: a large resize delta
repeated every cell's full highlight attributes and could exceed the 8 MiB control
frame by itself. The failed poll never advanced the observer's sequence, so it
retried the same oversized response until its remote host stopped and the application
cleanly closed its only dead pane. A single Windows pipe listener also let one slow
client temporarily starve the other.

## Acceptance

- [x] Two rendered remote-terminal hosts can repeatedly transfer control.
- [x] The former controller remains running and renders the new shared dimensions.
- [x] Expected ownership races and transient local transport contention do not kill
      either host.
- [x] Adjacent input is batched and command work is bounded between projection polls.
- [x] Terminal cells use a compact attribute-table wire encoding and large poll
      batches are bounded by bytes as well as event count.
- [x] Windows exposes the advertised four pipe instances concurrently, and one
      stalled client cannot starve another.
- [x] Timed-out overlapped pipe I/O is cancelled and completed before its stack
      storage is released.
- [x] Two optimized Release Vulkan windows survive the previously failing
      resize/typing stress sequence without transport warnings.
- [x] A fatal protocol or server-identity error still stops the affected host.
- [x] Release build, focused regression, full CTest, and smoke pass.

## Rollback

The experimental remote-terminal CLI remains opt-in; the fix must not change local
terminal or Session behavior.
