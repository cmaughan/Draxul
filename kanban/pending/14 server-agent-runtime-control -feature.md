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
- [ ] The two-client detach/status/reconnect demonstration and focused automated
      gates pass.

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
  directories.
