# Personal agent schedule wakeups and default approvals

Personal agents currently only act on incoming chat messages; promises to follow a schedule do not create a wakeup. Add server-owned five-minute checks for every valid personal Codex identity, including unopened chats, and use Codex Approve for me on fresh/resumed threads.

- [x] Implement five-minute server wakeups, with deferred/coalesced checks while busy and no UI dependency.
- [x] Persist schedules through agent instructions; hide routine no-op checks while retaining meaningful results/errors.
- [x] Apply workspace-write / on-request / auto_review to personal Codex threads only.
- [x] Validate real process boundary, timer behavior and resume permissions; aggregate tests and same-cache smoke.
- [x] Update feature and personal-agent documentation with runtime/permission/provider limits.

## Decisions

The timer checks schedules; it does not promise exact-time delivery or wake a sleeping computer. Server must remain running. No catch-up burst after sleep/restart. Existing local per-agent executor locks apply; Dropbox does not provide cross-machine coordination. Claude support remains tracked in `kanban/pending/108 claude-personal-chat-adapter -feature.md`.

## Implementation and evidence

- `PersonalChatService::tick` runs on the server loop with a steady-clock five-minute cadence. All valid Codex identities are started/resumed at the first due tick, regardless of tabs. Worker-owned turns keep one deferred check behind busy/approval states; Stop clears it. Invalid/missing/deleting metadata cancels deferred work. Failed providers are not automatically restarted.
- Schedule prompts include current UTC time, read instructions/schedule/state, supersede old notes that Draxul has no timer, and avoid duplicating externally managed jobs. Bootstrap now instructs agents to persist requested schedules and completion/next-due times. No existing Dropbox instructions or external launch timers were changed.
- Scheduled turns request a typed JSON result. Internal prompts, commentary and no-op results stay out of the display transcript; substantive messages and errors remain visible. Snapshot exposes process-local check count and latest UTC check time.
- Fresh and resumed personal threads explicitly request workspace-write / on-request / auto_review. Live installed Codex accepted these fields and completed a tool-free structured no-op response; this does not bypass approvals or alter general terminal agent permissions. Official reference: https://learn.chatgpt.com/docs/sandboxing .
- Inspected existing personal state: AI News and Calendar preserve requested schedules but explicitly record no scheduler activation. The Solana agent records its separate LaunchAgent. The wake prompt respects existing external ownership.

## Validation

- Final aggregate: `python3 do.py test release`, **56/56 core CTest entries passed in 22.82 s**. One aggregate build target `draxul-tests-core` took **17.06 s**, reusing the existing Release cache (no configure/generate rerun).
- Focused iteration: **9 personal cases passed in 4.83 s**, including unopened multiple agents, no-op silence, meaningful results, ordinary chat after a scheduled turn, busy deferral/coalescing, Stop cancellation, malformed results, missing metadata, and start/resume permissions. Earlier iterations caught a test-only result/value API mismatch at compilation and two test expectations (optimistic working state; intentionally retained missing metadata). Those were corrected before the passing run. Focused tests overlap the aggregate because they were implementation diagnostics.
- Same-cache isolated personal-host smoke: **passed, 0.48 s**, isolated server shutdown passed. Native personal chat render: **1/1 passed, 1.42 s**, unchanged reference. Live installed Codex protocol probe also passed. `git diff --check` clean.
- Windows/remote CI not run here; the portable fake-provider integration runs in normal CI. No extra routine platform gate added.
- Existing live server was not interrupted. Restart server and GUI together to activate the rebuilt native chats, permission defaults and timer. Code is not committed/pushed in this turn.
