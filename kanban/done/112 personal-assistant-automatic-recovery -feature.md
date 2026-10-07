# Personal assistants automatically recover their provider

Dropbox identity/instructions define a personal assistant; the provider process and local conversation reference are replaceable. On startup prepare every valid personal identity. Reuse live sessions, resume durable provider history when available, and create a new thread if the provider reports that history is gone. Recover process failures automatically without breaking ordinary terminal/discovered/managed agents.

- [x] Implement isolated provider recovery with bounded backoff and startup preparation.
- [x] Preserve shared instructions and local display history; do not replay interrupted side effects.
- [x] Keep temporary recovery out of connection-error UI; surface actionable persistent blockers.
- [x] Cover provider death, missing history, unopened startup, and ordinary terminal/agent coexistence.
- [x] Update docs; run aggregate, same-cache startup, relevant render and live checks.

The user explicitly authorized automatic replacement even when a saved provider conversation cannot resume. This supersedes the earlier manual-Retry policy, and is current-runtime recovery rather than legacy-version support. Invalid shared data is never overwritten or replaced with an empty collection.


## Implementation

The server prepares configured Codex personal identities at startup, even without an open chat tab. Existing sessions are reused. Each personal session replaces failed provider processes using stop-aware exponential backoff capped at 30 seconds. A missing provider rollout creates a fresh thread in the same backing folder, with current backing instructions and preserved local display history. Unstarted thread IDs remain transient (card 110).

Recovery shows “Starting assistant…” without a connection-error dialog or Retry requirement. Repeated failures show a waiting reason while retries continue. Invalid metadata/local state remains actionable and never overwrites shared files. Interrupted requests remain visible with a notice and are not replayed automatically, to avoid repeating side effects. Client chat polling preserves the conversation through transient transport reconnection.

Ordinary discovered/managed agents retain their launch and discovery paths. Regression coverage runs ordinary shell discovery, aliases and two clients alongside an unopened personal provider. Provider-replacement coverage also verifies ordinary shell output remains available. Card 111 separately prevents duplicate personal projections from invalidating the shared Session poll.

## Validation

2026-10-07: focused personal lifecycle tests passed (11 cases, 7.61 s). One final `python3 do.py test release` aggregate passed all 56 core CTest entries in 26.09 s; same-cache build 9.10 s, no configure/generate rerun. Same-cache isolated personal startup smoke passed in 0.63 s. The personal-assistant render check passed in 2.02 s with its unchanged reference. No failing/repeated aggregate for this slice; Windows/remote CI not run. Logs: `/tmp/draxul-auto-recovery-focused.log` and `/tmp/draxul-auto-recovery-aggregate.log`.

Live application: all three configured assistants reached Ready with no errors. Stopped only Calendar’s verified idle personal provider PID 30623; Draxul automatically replaced it with PID 31130 and returned to Ready without a user Retry or model-turn replay. Created and focused a temporary ordinary Zsh tab while the personal providers were running; its prompt rendered correctly (`/tmp/draxul-new-shell-verified.png`). Closed only that test tab and restored the existing shell pane. No shared Dropbox instructions were changed by this implementation. Changes remain uncommitted/unpushed.
