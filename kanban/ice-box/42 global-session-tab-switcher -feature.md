# Global session, tab, and pane switcher

**Type:** feature
**Priority:** 42
**Raised by:** GPT/Codex

## User need

Provide one fuzzy overlay over the server Session registry and authoritative
topology, then switch the current client to the selected Session/tab/pane.

## Implementation plan

- [ ] Land stable tab/session boundaries (items 22, 24, 25).
- [ ] Define immutable switcher entries with target kind/id, session status, tab/pane names, host kind, cwd/title, and searchable aliases.
- [ ] Query the server registry/topology without blocking the UI; include
  detached durable Sessions and mark stale/unavailable targets.
- [ ] Reuse command-palette fuzzy scoring and cell-grid overlay primitives but give the switcher its own mode/state.
- [ ] Route selection to client-local focus or server Session switch/attach; do
  not load a second client-owned snapshot.
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
