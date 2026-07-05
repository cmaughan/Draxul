# Global session, workspace, and pane switcher

**Type:** feature
**Priority:** 42
**Raised by:** GPT/Codex

## User need

Provide one fuzzy overlay to find running/saved sessions, workspace tabs, panes, working directories, and titles, then activate/attach/restore the selected target.

## Implementation plan

- [ ] Land stable workspace/session boundaries (items 22, 24, 25).
- [ ] Define immutable switcher entries with target kind/id, session status, workspace/pane names, host kind, cwd/title, and searchable aliases.
- [ ] Merge live owner summaries with saved session listings without blocking the UI; mark stale/unavailable targets.
- [ ] Reuse command-palette fuzzy scoring and cell-grid overlay primitives but give the switcher its own mode/state.
- [ ] Route selection to local focus, live attach/activate, or saved restore with explicit confirmation where the current workspace would be replaced.
- [ ] Refresh results when session-owner state changes and preserve selection by stable id.
- [ ] Add a GUI action/keybinding with no conflicting default until chosen.

## Tests and acceptance

- [ ] Test ranking/filtering, duplicate names, stale targets, local focus, live attach, saved restore, and failure rollback.
- [ ] Non-ASCII names/cwds render correctly through item 08's helper.
- [ ] No result advertises an operation unavailable in the current provider/session state.
- [ ] Update `docs/features.md`; full tests/render snapshots/smoke pass.

## Dependencies and parallelism

Depends on items 22, 24, and 25. Good UI sub-agent work after the underlying read-only query APIs are stable.

<model>GPT-5 Codex</model>
