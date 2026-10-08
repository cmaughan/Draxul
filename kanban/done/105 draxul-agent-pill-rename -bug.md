# Draxul agent pill rename

Double-clicking a regular Draxul agent pill only focuses its pane, while personal
agent pills support inline renaming. Give regular agents the same editing gesture.

- [x] Route double-clicks into inline editing, with Enter/escape and UTF-8 support.
- [x] Persist the label through pane naming, preserving instance identity and resolving the owning Space/tab at commit time.
- [x] Cover routing, editor/layout and agent projection behavior; update feature documentation.
- [x] Run core aggregate tests, same-cache smoke and relevant render validation.

## Decisions

Use the existing topology pane-name override so labels survive Session projection
and restore. Resolve the stable agent instance ID when committing; never use the
currently active pane or a row index captured before editing.

## Implementation and validation

- Regular agent pills now route left double-clicks through the shared UTF-8 rename
  editor. Stable instance IDs identify edits across row reordering; personal rows
  are excluded from regular-agent indices. The shared agent caret appears in both
  sections. Right clicks do not accidentally begin a rename.
- App resolves the current owning Space/tab/pane at commit and uses RenamePane
  topology mutations. Regular-agent projections prefer the saved pane override;
  clearing it restores the provider label. Personal collection names stay separate.
- Regression coverage exercises HiDPI mouse routing, editor commit/cancel/UTF-8,
  reordered rows, inactive-tab ownership, unchanged IDs, clearing overrides and
  layout/caret rendering. Existing Session tests cover pane-name persistence.
- Shared C++ input/layout/topology paths serve macOS and Windows; there is no new
  platform-specific gate or protocol change.
- Release cache reused; no configure/generate. Core aggregate build took 20.35s.
  First core CTest pass: 55/56, 21.75s; the pre-existing detached-Session deletion
  test checked for an asynchronous checkpoint immediately and missed the file.
  No server code was changed. Rerunning the aggregate passed 56/56 in 19.77s
  (incremental aggregate build 4.57s). No separate focused run was needed.
- Same-cache Release smoke passed in 0.55s. No remote CI run was requested.
- Metal panel/chrome snapshot: initial CTest attempt timed out after 30.08s;
  direct diagnostic execution passed without code changes, then the same CTest
  passed in 1.24s. Latest image differs by 0.268%, within its existing threshold.
  No reference images were changed. The intermittent startup/checkpoint failures
  are recorded here; all final validation gates passed.
- Release app rebuilt; relaunch the UI to load the fix. Existing servers and
  agent terminals were not stopped. Changes are not committed or pushed.
