# Server-owned agent runtime and control

## Outcome

Keep agent discovery, semantic status, integration references, and control
available while every Draxul UI is detached from a remote Session.

## Acceptance

- [x] The server observes its own terminal process trees and bounded screen state.
- [x] A revisioned, sanitized agent projection excludes terminal text and process
      command lines from the wire contract.
- [x] Every UI attached to one Session converges on the same Agents projection.
- [x] Agent focus remains client-local and does not reroute another UI.
- [x] Official integration hooks target server epoch, Session, pane, and runtime
      generation.
- [x] Agent waits, bounded pane reads, input, and restart work with no GPU client
      attached.
- [x] Official session-reference updates work with no GPU client attached.
- [x] Focused automated, full Debug, Release core/app, Release link/smoke, and
      executable-level headless gates pass.
- [ ] The manual two-client managed-agent detach/status/reconnect demonstration
      passes.

## Rollback

Remote agent authority remains gated by experimental remote Sessions. Local
Sessions keep the existing app-owned `AgentController`.

## Progress

- Checkpoint 8a added the Session-scoped server tracker and
  `agent.snapshot`/`agent.poll` protocol.
- Server discovery/status fixtures cover appearance, stable identity, semantic
  evaluation, retirement, revision polling, and the no-terminal-text boundary.
- Checkpoint 8b added the client poller and projected server routes into the
  existing Agents rail. A real server-owned shell test launches a manually
  discovered Codex-named process and proves two independent clients converge;
  app tests pin local focus and attention acknowledgement.
- Checkpoint 8c added the negotiated `agent-control-v1` global server API for
  list/get/explain/wait, bounded pane reads, input, and restart. The real
  server-owned shell test now disconnects its terminal client before exercising
  those routes, proving they do not depend on a GPU client or controller lease.
- Checkpoint 8d moved official native-session report mutation into server
  topology. Reports are pinned to server epoch, Session, pane, managed identity,
  and runtime generation; a cold-restore test rejects stale epochs/generations
  and proves the accepted reference survives the next checkpoint with no UI
  attached.
- Checkpoint 8e added negotiated `managed-agent-v1` launch. The server resolves
  built-in/custom profiles, creates and persists the shared agent pane, injects
  complete route/epoch/generation environment, updates generation on restart,
  and cold-restores the managed runtime. Version-2 Codex/Claude hooks route
  pinned reports directly to the global server, including isolated runtime
  directories. Managed launch reserves the new terminal's initial controller
  lease for the requesting UI, eliminating the observer-first attachment race
  without persisting client identity. Clean server-process exits now remove
  their non-final pane/tab/Space from authoritative topology, so every attached
  UI drops the dead pane together; abnormal exits remain restartable.
- Client reconciliation now releases a focused pane's input route before
  destroying that projected host. A dead remote pane waits for the server's
  topology update rather than attempting a client-local close, preventing the
  controller-only stale-pointer crash when `exit` removes its focused pane.
- Restored server-owned Sessions no longer create a placeholder for the legacy
  default terminal before reading topology. Startup projects the surviving
  stable terminal IDs directly and refreshes a newer server revision when an
  agent exits between topology snapshot and terminal attach.
- Automated Slice 8 acceptance passes all Debug core/app shards, all Release
  core/app shards, exact Release link/smoke, two projection clients, no-UI
  launch/report/restart, cold restore, and executable-level
  status/list/shutdown. The manual two-window managed-agent script remains.
