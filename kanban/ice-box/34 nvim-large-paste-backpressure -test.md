# Neovim large-paste write backpressure

**Type:** test
**Priority:** 34

Remote-terminal paste chunking is already covered by
`kanban/done/15 oversized-paste-kills-remote-pane -bug.md`. Neovim paste is a separate
outbound RPC/write path and does not involve terminal scrollback.

- [ ] Send a 500 KiB bracketed paste through a fake/controlled Nvim transport.
- [ ] Verify chunking, ordering, cancellation/shutdown, and bounded UI-thread work.
- [ ] Keep notification-queue and terminal-scrollback assertions in their owning tests.
- [ ] Run focused Nvim transport tests and same-cache smoke.
