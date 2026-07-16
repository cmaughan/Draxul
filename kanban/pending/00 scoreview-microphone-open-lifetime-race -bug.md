# ScoreView microphone open lifetime race

**Type:** bug
**Priority:** 00
**Raised by:** GPT/Codex, Gemini; Claude flagged the detached opener lifecycle

## Problem

`MicPlayerInput` uses `Shared::abandoned.exchange(true)` as a two-party ownership handshake, but the opener performs that exchange before publishing `Shared::stream`. The destructor can observe “opener finished,” read `stream == nullptr`, return, and then race the worker's non-atomic stream write. The opened SDL recording stream can leak and remain active after its owner is gone.

## Implementation plan

- [ ] Add injected microphone dependencies for permission query, delay/polling, stream open, resume, available/read/clear, and stream destruction; production adapters call AVFoundation/SDL, tests use deterministic fakes.
- [ ] Replace `abandoned` with an explicit mutex-protected state machine such as `Opening -> Ready|Failed|Abandoned`, with the stream owned by exactly one state.
- [ ] Publish the stream and state in one critical section; if abandonment wins, destroy the just-opened stream outside the lock and never publish `Ready`.
- [ ] Make destruction atomically take the published stream or mark an in-flight opener abandoned, then destroy at most once without joining a permission-blocked thread.
- [ ] Protect error/state publication consistently; no non-atomic string or pointer may be read while the opener can write it.
- [ ] Keep the opener's shared lifetime independent from `FlowController`; the detached path must not capture any host-owned reference.
- [ ] Preserve the non-blocking TCC consent behavior and keyboard fallback on denied/unavailable input.

## Tests

- [ ] Pause a fake opener before permission, after permission, after stream creation, before publication, and after publication; destroy the owner at every handoff.
- [ ] Assert exactly one destroy for every successfully created fake stream and zero stream calls after destruction.
- [ ] Cover denied permission, open failure, successful polling, backlog clearing, and repeated construction/destruction.
- [ ] Run the interleaving suite repeatedly under the reopened TSan CI job.

## Acceptance criteria

- [ ] `Shared::stream` and error data have a proven happens-before relationship for every read/write.
- [ ] Destruction returns promptly while permission remains undecided.
- [ ] No opened stream leaks, double-destroys, resumes after abandonment, or remains active after host shutdown.
- [ ] Build both app/test targets, run focused ScoreView tests, `ctest`, and `py do.py smoke` on macOS and Windows paths.

## Dependencies and parallelism

Immediate reliability root. Blocks ScoreView host/device tests and host decomposition. A lower-level audio-lifecycle agent can own this without editing `score_host.cpp`; coordinate the injection seam with the ScoreHost test owner.

<model>GPT-5 Codex</model>
