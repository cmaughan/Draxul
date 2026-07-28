# Fake remote terminal with two clients

## Goal

Implement Slice 3 of `plans/server-client-terminal-runtime.md`: prove the complete
server terminal, protocol, client projection, and rendered-host boundary using a
deterministic fake terminal before moving a real shell process.

## Acceptance

- [x] The server owns one deterministic `TerminalCore` fake terminal and pane
      descriptor.
- [x] Attach returns a complete snapshot with server epoch, terminal generation, and
      sequence.
- [x] Input echo, resize, cursor/title, disconnect, reconnect, and resync work.
- [x] Per-client event delivery is ordered and bounded; saturation requests a fresh
      snapshot without delaying other clients.
- [x] One client controls input/resize while observers are read-only until takeover.
- [ ] A headless probe and `RemoteTerminalHost` use the same client projection API.
- [x] Two clients converge on identical semantic digests and ownership events.
- [x] Stale epoch, generation, and sequence data is rejected.
- [ ] The experimental fake-remote UI wakes and redraws as shared state changes.
- [ ] New server/client/protocol/host dependency boundaries have link-isolation
      coverage.
- [ ] Plan, feature inventory, and module map reflect the landed slice.
- [ ] App, tests, full CTest, smoke, and relevant render validation pass.

## Rollback

The fake terminal remains opt-in and no local shell ownership or Session persistence
behavior changes.
