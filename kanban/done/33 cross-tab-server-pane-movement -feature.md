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

- [x] Make stable pane ID the canonical identity for resolving a pane's current
      Space, tab, terminal, and managed-agent route.
- [x] Audit every consumer of `DRAXUL_SPACE_ID` and `DRAXUL_TAB_ID`, including
      `--current`, managed-agent hooks, session reports, and external scripts.
- [x] Define launch-time Space/tab environment values as advisory, or add a
      bounded route-refresh mechanism for consumers that need current values.
- [x] Preserve `DRAXUL_PANE_ID`, pane ID, terminal ID, process state, scrollback,
      and managed-agent instance identity when the selected policy permits it.
- [x] If any pane kind must restart instead, require an explicit opt-in and
      increment its runtime generation; never disguise a restart as a move.
- [x] Specify working-directory behavior when moving across Space roots: moving
      a live process must not claim that its actual cwd changed.

## Atomic topology operation

- [x] Extend the topology command with destination Space ID, destination tab ID,
      target pane ID, direction, and ratio.
- [x] Validate source/destination, pane domain, companion ownership, ratio,
      capacity limits, and route policy before mutating either tree.
- [x] Detach the source leaf, collapse its old split, and insert it relative to
      the destination leaf in one server event-loop transaction.
- [x] Define empty-source-tab behavior explicitly: close the empty tab when
      another tab survives; reject moving the final pane from the final tab of a
      Space unless a replacement policy is deliberately introduced.
- [x] Reject an insertion-stage candidate failure without committing either
      split tree or the pane's route metadata.
- [x] Treat checkpoint publication as asynchronous: a failed checkpoint keeps
      the last completed durable snapshot while the accepted live move remains
      authoritative. There is no synchronous persistence transaction to roll
      back.
- [x] Bump topology revision once and return the pane's new authoritative route.
- [x] Preserve client-local focus: each attached UI independently retains focus
      when possible and chooses a local fallback if its focused pane moved away.
- [x] Reject client-local/product-host panes in the headless command unless a
      separate UI ownership-transfer contract is implemented.

## CLI and declarative control

- [x] Support:
      `draxul pane move <pane-id> --space <space-id> --tab <tab-id>
      --target <pane-id> --direction <left|right|up|down> [--ratio <value>]`.
- [x] Continue accepting the shorter existing same-tab form.
- [x] Return stable JSON fields for the moved pane and its source/destination
      routes.
- [x] Allow layout/orchestration callers to compose cross-tab movement without
      needing a focused UI.
- [x] Update CLI help and `docs/features.md` with the route and restart semantics.

## Integration and smoke validation

- [x] Add an executable-level real-server test that moves a running shell into
      another tab while output and scrollback remain readable.
- [x] Verify `pane get`, `pane read`, `pane run`, and `--current` resolve the new
      route after the move.
- [x] Move a managed agent across tabs, confirm stable instance/pane/terminal
      identity, and verify prompt/status/session-report routing after movement.
- [x] Cover moves across Spaces with different roots without misreporting cwd.
- [x] Cover source-tab collapse, final-pane rejection, capacity rejection,
      companion-pane rejection, and rollback after a forced destination failure.
- [x] Run a two-UI test proving both clients converge while focus remains local,
      including detach, move, and reconnect.
- [x] Run the focused integration cases, product-scoped aggregate, and
      same-cache smoke on macOS/Metal.
- [x] Run the Release build, full cross-platform `ctest`, and Windows/Vulkan
      smoke.

## Non-goals

- Moving client-local Nvim, Markdown, Kanban, or product panes between UI owners.
- Migrating a live terminal process between Draxul server processes.
- Implicitly changing the cwd of a running shell when it enters another Space.

## Local implementation evidence

- `MovePane` now carries explicit destination Space/tab fields while empty
  destination fields retain the existing same-tab wire behavior. Results expose
  stable moved-pane and source/destination route fields, including idempotent
  replays and legacy result decoding.
- The server validates both routes, the server-owned source domain, companion
  ownership, ratio, destination capacity, and final-Space behavior before it
  edits a candidate snapshot. It commits both trees together, closes an empty
  source tab only when another tab survives, and increments the revision once.
- Live terminal moves preserve pane ID, terminal ID, runtime generation, process
  ID, scrollback/output, agent metadata, and recorded working directory. Existing
  processes are not restarted; client-local panes are rejected.
- The two-tree mutation is exercised through a forced destination-leaf failure
  after source detachment begins against the candidate. The rejected operation
  leaves both published trees and the pane route unchanged. Checkpoint failure
  uses the production asynchronous save callback: the last completed checkpoint
  remains readable while the accepted live topology continues to expose the
  moved pane. This records the actual durability boundary rather than inventing
  a synchronous persistence rollback contract.
- An executable CLI test moves a live server pane and then exercises `pane get`,
  `pane run`, `pane read`, and `pane get --current` through the built Draxul
  binary. It proves that stable pane identity resolves the destination route even
  when launch-time Space/tab environment values are stale.
- The managed-agent process test now moves a running managed agent across tabs,
  preserves instance, pane, terminal, and runtime-generation identity, and
  verifies `agent.get`, input, running-status waits, later session reports,
  restart, checkpoint, and restore routing from the destination tab.
- The focused `[pane-move]` run passed 266 assertions in 7 cases. The elevated
  core plus Rezonality aggregate passed 30/30 CTest entries, followed by a
  passing same-cache macOS/Metal smoke.

## Two-UI convergence closeout (2026-09-22)

- Added a real-server integration case with two independent topology clients
  and two independent `TopologyProjection` instances.
- One client disconnects while the controller moves a live server pane between
  tabs. The connected projection chooses a source-tab fallback without stealing
  destination focus; the disconnected projection reconnects, converges to the
  same authoritative snapshot, retains its own source/destination focus, and
  preserves the moved pane's local leaf identity.
- The focused `[pane-move][two-ui]` run passed 32 assertions in one case.
- The final Windows Release products aggregate passed all 48 registered CTest
  entries. The same-cache Vulkan smoke then passed with `--skip-build`.
- One initial aggregate attempt hit a transient atomic control-metadata rename
  failure in the pre-existing server recovery test. Both focused recovery cases
  passed immediately afterward (14 assertions), and the complete 48-entry
  aggregate passed on the clean rerun.
