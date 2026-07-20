# ScoreView progress crash-safety coverage

**Type:** test
**Priority:** 17
**Raised by:** Claude

## Gap

`save_progress_atomic()` has a happy-path tmp+rename test, but no way to fail directory creation, open, write, flush/sync, close, or final replacement independently. The current test does not prove that a last-good profile survives failure or that replacement semantics are equivalent on Windows and macOS.

## Implementation plan

- [ ] Extract or reuse a small atomic-file storage adapter with injected filesystem operations; align semantics with pending 02 session persistence rather than inventing a second policy.
- [ ] Define the durability contract explicitly: temporary file in the same directory, complete write, flush/close, optional file sync, atomic replace, optional directory sync, and cleanup.
- [ ] Preserve the previous destination until replacement succeeds; use correct Windows replace semantics instead of relying on unspecified overwrite behavior.
- [ ] Surface load corruption and save failures to ScoreHost through a warning/toast seam while falling back to a fresh in-memory model.
- [ ] Keep PlayerModel's unknown-field preservation separate from storage mechanics and assert both.

## Tests and acceptance

- [ ] Inject failure at every storage operation and assert the previous JSON remains byte-for-byte loadable.
- [ ] Cover a missing destination, existing destination, orphan temp file, permission denial, disk-full/short write, corrupt JSON, and process-style interruption before replacement.
- [ ] Verify successful replacement leaves no temp file and preserves unknown fields after load/mutate/save.
- [ ] Run the same contract tests against real temporary directories on Windows and macOS.
- [ ] Share helpers/behavior with session atomic-write tests where practical.

## Dependencies and parallelism

Coordinate with pending 02 to share one atomic storage contract. This can be a filesystem/test-focused task independent of `score_host.cpp` until error reporting is connected. Blocks relying on progress history for items 64 and 68.

<model>GPT-5 Codex</model>
