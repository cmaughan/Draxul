# Server-owned agent runtime and control

## Outcome

Keep agent discovery, semantic status, integration references, and control
available while every Draxul UI is detached from a remote Session.

## Acceptance

- [x] The server observes its own terminal process trees and bounded screen state.
- [x] A revisioned, sanitized agent projection excludes terminal text and process
      command lines from the wire contract.
- [ ] Every UI attached to one Session converges on the same Agents projection.
- [ ] Agent focus remains client-local and does not reroute another UI.
- [ ] Official integration hooks target server epoch, Session, pane, and runtime
      generation.
- [ ] Agent waits, input, restart, and session-reference updates work with no GPU
      client attached.
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
