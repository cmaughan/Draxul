# Move server-owned panes across tabs and Spaces

**Type:** feature
**Priority:** P2 / sequence 33
**Raised by:** Codex

## Goal

Allow `draxul pane move` to move a live server-terminal or managed-agent pane
into another tab, including a tab in another Space, without requiring a UI and
without silently leaving the process with an incorrect route identity.

## Current boundary

`pane move` currently rearranges leaves inside one tab. That operation only
rewrites one split tree, so the stable pane ID, terminal ID, process, and its
launch-time `DRAXUL_SPACE_ID`/`DRAXUL_TAB_ID` remain consistent.

A cross-tab move rewrites two split trees and changes the pane's authoritative
Space/tab route. Windows and Unix parents cannot mutate the environment of an
already-running shell or agent, so preserving the process would leave its
launch-time Space/tab variables stale. Restarting would refresh the environment
but destroy interactive state. The feature must choose and document an honest
route contract before exposing the mutation.

## Route contract

- [ ] Make stable pane ID the canonical identity for resolving a pane's current
      Space, tab, terminal, and managed-agent route.
- [ ] Audit every consumer of `DRAXUL_SPACE_ID` and `DRAXUL_TAB_ID`, including
      `--current`, managed-agent hooks, session reports, and external scripts.
- [ ] Define launch-time Space/tab environment values as advisory, or add a
      bounded route-refresh mechanism for consumers that need current values.
- [ ] Preserve `DRAXUL_PANE_ID`, pane ID, terminal ID, process state, scrollback,
      and managed-agent instance identity when the selected policy permits it.
- [ ] If any pane kind must restart instead, require an explicit opt-in and
      increment its runtime generation; never disguise a restart as a move.
- [ ] Specify working-directory behavior when moving across Space roots: moving
      a live process must not claim that its actual cwd changed.

## Atomic topology operation

- [ ] Extend the topology command with destination Space ID, destination tab ID,
      target pane ID, direction, and ratio.
- [ ] Validate source/destination, pane domain, companion ownership, ratio,
      capacity limits, and route policy before mutating either tree.
- [ ] Detach the source leaf, collapse its old split, and insert it relative to
      the destination leaf in one server event-loop transaction.
- [ ] Define empty-source-tab behavior explicitly: close the empty tab when
      another tab survives; reject moving the final pane from the final tab of a
      Space unless a replacement policy is deliberately introduced.
- [ ] Roll back both tabs and all metadata if insertion or persistence fails.
- [ ] Bump topology revision once and return the pane's new authoritative route.
- [ ] Preserve client-local focus: each attached UI independently retains focus
      when possible and chooses a local fallback if its focused pane moved away.
- [ ] Reject client-local/product-host panes in the headless command unless a
      separate UI ownership-transfer contract is implemented.

## CLI and declarative control

- [ ] Support:
      `draxul pane move <pane-id> --space <space-id> --tab <tab-id>
      --target <pane-id> --direction <left|right|up|down> [--ratio <value>]`.
- [ ] Continue accepting the shorter existing same-tab form.
- [ ] Return stable JSON fields for the moved pane and its source/destination
      routes.
- [ ] Allow layout/orchestration callers to compose cross-tab movement without
      needing a focused UI.
- [ ] Update CLI help and `docs/features.md` with the route and restart semantics.

## Integration and smoke validation

- [ ] Add an executable-level real-server test that moves a running shell into
      another tab while output and scrollback remain readable.
- [ ] Verify `pane get`, `pane read`, `pane run`, and `--current` resolve the new
      route after the move.
- [ ] Move a managed agent across tabs, confirm stable instance/pane/terminal
      identity, and verify prompt/status/session-report routing after movement.
- [ ] Cover moves across Spaces with different roots without misreporting cwd.
- [ ] Cover source-tab collapse, final-pane rejection, capacity rejection,
      companion-pane rejection, and rollback after a forced destination failure.
- [ ] Run a two-UI test proving both clients converge while focus remains local,
      including detach, move, and reconnect.
- [ ] Run the Release build, the focused integration test, full `ctest`, and
      `py do.py smoke`.

## Non-goals

- Moving client-local Nvim, Markdown, Kanban, or product panes between UI owners.
- Migrating a live terminal process between Draxul server processes.
- Implicitly changing the cwd of a running shell when it enters another Space.
