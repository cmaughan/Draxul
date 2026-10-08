# Claude personal chat adapter

Add Claude to the native personal-agent chat pane delivered in `kanban/done/107 personal-agent-native-chat -feature.md`. Ordinary Claude managed/discovered terminals already work and must remain unchanged.

**Priority:** P2

- [ ] Verify the installed Claude SDK/structured streaming protocol against primary documentation, including continuation, tool approvals, questions and cancellation.
- [ ] Add a server-owned adapter behind the same personal chat snapshot/command boundary; no terminal scraping or fallback.
- [ ] Preserve local provider conversation identity/history, shared Dropbox instructions/data, and the quiet answers-only UI.
- [ ] Cover resume, provider death, approval/refusal, cancellation and hidden tool output through integration tests, then run the scope aggregate and same-cache smoke.
- [ ] Connect Claude identities to the server-owned five-minute schedule checks from `kanban/done/109 personal-agent-schedule-wakeups -feature.md`, using supported provider permissions and the same quiet/deferred behavior.
- [ ] Update the feature inventory and personal-agent documentation.

The first Codex slice deliberately reports an unsupported-provider error for a Claude personal profile. This card owns that remaining provider scope. Follow the repository's review-between-slices rule; do not broaden ordinary agent behavior.
