# ScoreView piece library

**Type:** feature
**Priority:** 64
**Raised by:** Claude

## User need

When ScoreView launches without a source, show recent pieces with useful progress summaries instead of only a placeholder score.

## Implementation plan

- [ ] Add a versioned ScoreView library index under the config directory mapping content hash to last known source path, title/composer, last opened time, progress path, and lightweight summary.
- [ ] Update the index only after a source loads successfully; write atomically and tolerate moved/missing files without deleting history immediately.
- [ ] Derive summary fields from `PlayerModel`/progress data: resumed tempo, mastery/progress, last session, and optional trouble count, without parsing every full file each frame.
- [ ] Add a pure library model for sort/filter/recent/pinned/missing states and keep filesystem scanning off the render path.
- [ ] Present the library through ScoreView's no-source state with keyboard/mouse selection, open, locate missing source, remove history, and clear-progress actions.
- [ ] Open a selected piece through the normal ScoreSessionController path and preserve optional `.musicxml`/`.mxl` validation.
- [ ] Avoid storing score contents or absolute paths in logs; document the local history/privacy behavior.

## Tests and acceptance

- [ ] Test index migration, atomic failure, duplicate content at new path, missing/moved file, Unicode metadata/path, corrupt progress, sorting, pinning, and remove/clear behavior.
- [ ] A corrupt index degrades to an empty library with a warning and never corrupts per-piece progress.
- [ ] Library startup performs bounded I/O and does not initialize audio/devices until a piece is opened.
- [ ] Headless ScoreHost tests cover no-source -> library -> selected piece transition.

## Dependencies and parallelism

Depends on progress crash-safety item 17, hostile MXL item 18, and preferably ScoreSessionController from item 21. A model/storage agent can work independently from presentation after the index schema is agreed.

<model>GPT-5 Codex</model>
