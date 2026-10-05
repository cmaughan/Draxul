# Space agent mailbox with history UI and the user as a participant

**Priority:** P2  
**Created:** 2026-10-05  
**Updated:** 2026-10-05 (research and decisions folded in)  
**Plan:** [plans/agent-mailbox-design.md](../../plans/agent-mailbox-design.md)  
**Summary:** Agents in a Space, managed or manually launched, and the user exchange structured messages through a server-owned, persistent mailbox. Provider hooks give agents prompt "you have mail" doorbells without typing into their input. A Mail pane shows and extends the history.

## Motivation and existing behavior

A live Codex/Claude conversation used agent discovery, prompt injection, explicit
Enter keys, fixed sleeps, and bounded pane reads. This worked, but involved draft
interference, ambiguous completion, and replies limited to the visible screen.
A failed input-clearing command also showed why subsequent actions must depend
on verified success.

`docs/features.md` already documents Session-scoped identities, managed and
discovered agents, authenticated local control, and agent status waits. Prompt
submission is a single deduplicated request (see
`kanban/done/102 agent-prompt-submit -feature.md`). Do not add another Enter
after `agent prompt` or recreate that work. The mailbox, cooperative inbox
protocol and history UI are not implemented.

## Research summary

The full table and sources are in the plan.

- **herdr** has no mailbox. It types prompts into terminals and reads replies from the screen. Its lessons: refuse to type while an approval dialog is up, and a timeout does not prove a prompt was not delivered.
- **MCP Agent Mail** pairs a store and web inbox with a `PostToolUse` unread-count hook. It never wakes idle agents, and its reminder nagged until limited to unread mail.
- **Claude Code** sessions have an inbox socket per session that wakes idle sessions. Its hooks can add context, block a stop, and use `asyncRewake`.
- **Codex** has equivalent hooks but no supported way to wake an idle session.

## Decisions (2026-10-05)

- **History persists.** A bounded journal per Session sits next to the Session checkpoint and survives server restart. This replaces the earlier in-memory-only first version.
- **Scope is the Space.** Direct messages and `*` broadcast resolve within the sender's Space.
- **The user is a first-class participant (`user`).** They send from the Mail pane or CLI, receive mail, and get unread badges and toasts.
- **Idle Codex agents get an opt-in typed nudge.** It is off by default, sent only when the agent is idle, not blocked and has an empty input box, and rate-limited. Without it, idle Codex mail waits for the agent's next turn.

## Design direction (details in the plan)

- **The server owns the mailbox.**
  - Message IDs and cursors use a per-Session `seq` that continues across restarts.
  - Delivery state per recipient: `queued → notified → read → acked`, or `undeliverable`. Reading never acknowledges a message.
  - Limits, `request_id` deduplication, per-sender rate limit, a hop budget for agent-to-agent conversations with pause and resume, and an explicit close.
- **The server works out the sender.** It uses the control connection's peer PID and walks its ancestors to the pane's agent. An agent can never send as `user`. An environment variable is routing context only.
- **CLI:** `draxul mail whoami|send|inbox|read|ack|wait|log|status|close|resume`, with capability `agent-mailbox-v1`.
- **Doorbell files.** A per-participant `.seq` file in the server runtime directory lets hooks check for mail in about 1ms. The file only signals new mail; message bodies stay behind the control API.
- **Participation goes through the user-level integration hooks Draxul already installs.**
  - `SessionStart` adds identity and messaging rules to the agent's context.
  - `PostToolUse` and `UserPromptSubmit` add a doorbell, announced once per `seq`.
  - `Stop` blocks once if there is unannounced mail.
  - Idle Claude: wake via `asyncRewake` or the inbox socket, chosen by spike.
  - This reaches manually launched agents in any repository.
- **UI:**
  - A session-stream `mailbox` channel carrying revision and unread counts.
  - Unread badges on the Agents rail and Space pill, and a toast for user mail.
  - A `--host mail` pane with threads, delivery state, jump to pane, compose, reply and close.

## Implementation slices and acceptance gates

### Slice 1 — Mailbox, attribution and CLI
- [ ] `ControlRequest.peer_process_id` on the posix and named-pipe transports; parent-PID helper.
- [ ] Mailbox model and journal codec in a reusable library; `ServerMailboxService` per `ServerSession`; `mail.*` methods; capability `agent-mailbox-v1`.
- [ ] Space-scoped addressing and broadcast expansion; sender attribution (agent, `user`, `unattributed_sender`); recipient lifecycle (restart keeps mail, a vanished discovered agent's mail becomes `undeliverable`).
- [ ] Journal persistence and restore, compaction, corrupt-archive handling; doorbell files.
- [ ] Limits, deduplication, rate limit, hop budget with pause and resume, close; structured errors.
- [ ] `draxul mail` CLI and help.
- [ ] Isolated-runtime integration tests for ordering and correlation, Space isolation, attribution, retries and repeated acks, Session isolation, limits and `cursor_expired`, wait timeout and cancellation, server restart and restore, hop budget.
- [ ] `docs/features.md` (CLI, storage lifetime, limits) and the SKILL.md command reference.

### Slice 2 — Participation hooks (busy agents)
- [ ] `draxul-agent-integration`: `SessionStart` identity and rules context; `PostToolUse`, `UserPromptSubmit` and `Stop` doorbells for Claude and Codex; upgrade and uninstall paths.
- [ ] Measure hook latency on macOS and Windows; add a native hook helper only if PowerShell cost requires it.
- [ ] `CLAUDE.md` rule (AGENTS.md and GEMINI.md keep pointing to it) and SKILL.md messaging section.
- [ ] Live evidence: a managed agent and a manually launched agent exchange correlated replies without screen scraping or prompt injection. Input drafts are kept, acks are explicit, close is respected and a user stop takes effect immediately. Record provider versions.

### Slice 3 — Idle wake
- [ ] Spike Claude idle wake (`asyncRewake` waiter versus inbox socket); ship one and record the evidence and versions.
- [ ] Opt-in idle nudge (`[agents.mailbox] idle_nudge`, per-profile override), with an eligibility policy (idle, not blocked, empty input or typing quiet period, once per `seq`, rate limit) and fake-runtime tests.
- [ ] Live evidence: idle Claude wakes; idle Codex with the nudge on and off. Document the idle-agent limitation when the nudge is off.

### Slice 4 — History UI and the user as participant
- [ ] Session-stream `mailbox` channel (protocol, poll and stream services, client state, App wiring).
- [ ] Unread badges on the Agents rail and Space pill; toast for user mail.
- [ ] `--host mail` pane: threads, delivery state, jump to pane, compose, reply, close; palette actions `open_mailbox` and `send_mail`.
- [ ] Channel round-trip tests; Mail pane render scenario.
- [ ] `docs/features.md` and the config reference.

### Every slice
- [ ] One core `do.py test debug` plus one same-cache `do.py smoke --skip-build`; record results and the validation cost here. Keep Windows paths valid and investigate CI failures. Finish the feature with the required Release startup confirmation.

## Status

Planning complete; implementation not started. Next step: Slice 1.
