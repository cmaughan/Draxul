# Server-authoritative shared topology

## Outcome

Make the Draxul server authoritative for Session, Space, tab, pane, and split
structure so multiple UIs converge while retaining independent routes and local
presentation state.

## Acceptance

- [x] Renderer-neutral, copyable topology values describe Spaces, tabs, panes,
      split nodes, server-terminal descriptors, and client-local descriptors.
- [x] The server owns a revisioned topology snapshot and applies bounded,
      idempotent commands with explicit revision conflicts.
- [x] Two headless clients can refresh/poll and converge after a mutation without
      sharing active Space, tab, focused pane, viewport, or window state.
- [ ] The experimental remote UI projects the server snapshot into its Space,
      tab, pane, and split chrome.
- [ ] Create, close, rename, reorder, split, ratio, and restart mutations route
      through server commands and appear in a second UI.
- [ ] Every server-terminal pane has its own server runtime and stable TerminalId.
- [ ] Every client-local pane descriptor creates an independent local host in each
      UI, or an explicit unavailable placeholder.
- [ ] Detach/reconnect preserves the topology and server terminal runtimes.
- [ ] Two live UIs pass the Slice 6 demonstration and focused/full automated gates
      in `build-ninja-release`.

## Checkpoint (2026-07-29)

- Added `topology-v1` capability, protocol JSON, server mutation service, client
  polling projection, idempotency cache, and optimistic revision conflicts.
- Ninja Release `[topology]`: 38 assertions in 3 cases.
- Ninja Release `[server]`: 419 assertions in 23 cases.

## Rollback

The topology service is reachable only through the experimental server path.
Ordinary file-backed Sessions and local hosts remain unchanged.
