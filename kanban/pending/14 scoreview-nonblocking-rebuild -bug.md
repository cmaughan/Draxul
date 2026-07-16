# Keep ScoreView rebuilds off the main thread

**Type:** bug
**Priority:** 14
**Raised by:** GPT/Codex; Claude identified ScoreHost as a collision hotspot

## Problem

`WindowEngraver::cancel()` waits for an active Verovio job, and `ScoreHost::rebuild_window()` calls it on user-driven restart/mode/inspector paths before performing additional synchronous engraving. A roughly 100 ms job can therefore block several frames and defeat the rolling window's asynchronous design.

## Implementation plan

- [ ] Replace blocking cancellation with generation-based invalidation: an in-flight job may finish, but stale results are discarded without making the caller wait.
- [ ] Let `WindowEngraver` retain one current job and one coalesced latest pending job, or expose a latest-wins request API with bounded memory.
- [ ] Return request/generation IDs from submit and poll so `ScoreHost` installs only the newest compatible result.
- [ ] Keep the current engraved window visible while a user-driven rebuild is pending; show a lightweight busy state rather than clearing the score.
- [ ] Reserve synchronous engraving for initial host startup or a proven worker-construction failure before interactive frames begin.
- [ ] Preserve FlowController carry state, verdict archive, current tempo, transport intent, and source window offset across asynchronous replacement.
- [ ] Make shutdown explicit: stop accepting jobs, discard pending work, signal the worker, and join only through the host's owned teardown policy; do not detach a worker that owns Verovio.
- [ ] Bound queued XML/result memory and log coalesced/stale generations at debug level.

## Tests

- [ ] Inject a blocking/fake engraver and prove restart, mode changes, spacing changes, and rewind return within one frame while work is active.
- [ ] Submit rapid generations and assert only the newest result installs and all older results are destroyed.
- [ ] Verify carry state and verdict colors across delayed replacement.
- [ ] Cover worker creation failure, engraving error, repeated cancellation, and shutdown at each state under TSan.

## Acceptance criteria

- [ ] No interactive ScoreView action waits for a running engrave job.
- [ ] Pending work is bounded and latest-wins behavior is deterministic.
- [ ] Initial load, rolling advance, rebuild errors, and shutdown preserve current user-visible behavior without stale installs.
- [ ] Focused ScoreView tests, full `ctest`, app build, and smoke pass.

## Dependencies and parallelism

Land before ScoreHost orchestration/stress tests are finalized and before item 21 decomposes the host. A worker owner can implement the lower-level generation API while the test owner builds fakes, but one agent should integrate the resulting API into `score_host.cpp`.

<model>GPT-5 Codex</model>
