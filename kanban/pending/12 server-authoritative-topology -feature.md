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
- [x] Experimental remote UIs project server Space create/rename/close mutations;
      newly projected client-local Spaces launch an independent local host per UI.
- [x] The experimental remote UI projects the server snapshot into its Space,
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
- Added the first app projection: Space identity mapping, 100 ms topology polling,
  conflict refresh/retry, server-routed Space mutations, and client-local active
  Space routing.
- Added tab/pane identity projection, shared names and split trees, server-routed
  create/close/rename actions, and a live layout reconcile path which preserves
  unchanged local hosts.
- Live Ninja Release boundary check: two UIs attached to one fresh server runtime,
  remained alive concurrently, detached, and the server shut down cleanly.
- Ninja Release `[topology]`: 45 assertions in 3 cases.
- Ninja Release `[server]`: 420 assertions in 23 cases.
- Ninja Release app suite: 4,095 assertions in 477 cases; smoke passed.

## Rollback

The topology service is reachable only through the experimental server path.
Ordinary file-backed Sessions and local hosts remain unchanged.
