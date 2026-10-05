# Agent mailbox: research, decisions and implementation plan

**Status:** planned (not implemented)
**Date:** 2026-10-05
**Card:** `kanban/pending/103 session-agent-mailbox -feature.md`

## Goal

Agents in a Draxul Space — managed or manually launched, Claude or Codex — and the
user can message one participant or everyone in the Space. Agents notice new mail
promptly without busy polling and without typing into their input boxes. The user
can browse and add to the conversation history in a Mail pane.

## Decisions (2026-10-05)

| Question | Decision |
|---|---|
| History lifetime | **Persist.** A bounded journal per Session sits next to the Session checkpoint and survives server restart. |
| Idle Codex agents | **Opt-in typed nudge** is acceptable. It is off by default and enabled per config or profile. |
| Addressing scope | **Space.** Direct messages and `*` broadcast resolve within the sender's Space. |
| User participation | **Yes, first class.** The user is the participant `user`. They can send from the UI or CLI, receive, and has unread state. |

## Prior art and lessons

| System | Transport / store | How the agent is woken | History UI | Lessons |
|---|---|---|---|---|
| **herdr** | JSON over a Unix socket. `agent prompt` types into the target's terminal. | Typing only. Screen-regex status. Refuses while an approval dialog is up (`agent_blocked`). | None | No mailbox; replies are scraped from the screen. Its docs warn that a timeout does not prove non-delivery (a retry risks duplicates). |
| **Claude Code agent teams** | JSON inbox file per agent | Built into the harness | None | Early bugs where teammates only checked their inbox between turns, so mail sat unread at the first prompt. |
| **Claude Code cross-session messaging** (v2.1.224+) | Inbox socket per session, exported as `CLAUDE_CODE_MESSAGING_SOCKET`/`_TOKEN` | **Wakes idle sessions**; delivered between tool calls mid-turn | Preview line in the transcript | Per-sender rate limit, duplicate drop, 50-message queue. Peer messages cannot grant consent. |
| **Claude Code channels** (preview) | MCP `notifications/claude/channel` | Wakes idle sessions | — | Needs a `--channels` or dev flag, an allowlist, and claude.ai or Console auth. No acknowledgement. Not a good default. |
| **MCP Agent Mail** | HTTP MCP server, git plus SQLite | `PostToolUse` hook, unread count only. **Never wakes an idle agent.** | Web inbox, human "Overseer" compose screen | The reminder became a "standing nag" until they limited it to `unread_only`. Codex `notify` output never reaches the model. |
| **tmux orchestrators** (uzi, claude-squad, Tmux-Orchestrator) | `tmux send-keys` | Typing only | — | Races with drafts and dialogs. |

Provider hooks:

- **Claude Code**
  - `SessionStart`, `UserPromptSubmit`, `PostToolUse` and `Stop` can return `additionalContext` (capped at 10k characters).
  - `Stop` can block, which continues the turn.
  - `asyncRewake: true` runs a background command hook that wakes Claude on exit code 2. Behaviour while idle is not documented and needs a spike.
- **Codex CLI**
  - `SessionStart`, `UserPromptSubmit`, `PreToolUse`, `PostToolUse` and `Stop`, with `additionalContext` and Stop continuation.
  - No supported way to wake an idle terminal session.

## Existing seams to build on

- **Control plane** (`libs/draxul-control/`)
  - Token-authenticated local socket. Workers only queue requests; the state loop runs them (`ControlServer::process_pending`).
  - A worker waits at most about 5s (`kIoTimeout`), so handlers never block.
  - `ControlRequest` has no peer process identity today.
- **Server**
  - `ServerKernel::Impl::handle_request` (`server_kernel_requests.cpp`) owns the `agent.*` methods, with `request_id` deduplication through `completed_agent_mutations`.
  - Per-Session state lives in `ServerSession` (`server_kernel_impl.h`).
  - Checkpoints are written off-thread to `server_session_state_path(...)` (`server_kernel_sessions.cpp`).
- **Agents**
  - IDs per Session come from `AgentInstanceIds`. Managed identities live on `TopologyPane.agent`; discovered identities live in `ServerAgentService` and are transient.
  - Status is `Idle`/`Working`/`Blocked`/`Done`, from `agent_status.cpp` manifests.
  - Discovery already captures `AgentProcessObservation`, the pane's process list with parent PIDs (`unix_agent_process_probe.cpp`, `conpty_process.cpp`).
- **Environment** (`server_kernel_terminals.cpp`)
  - Every server terminal gets `DRAXUL_ENV`, `DRAXUL_SESSION_ID`, `_SPACE_ID`, `_TAB_ID`, `_PANE_ID`, `_SERVER_EPOCH`, `_RUNTIME_GENERATION` and `_SERVER_RUNTIME_DIR`.
  - Managed agents also get `DRAXUL_AGENT_INSTANCE_ID` and `DRAXUL_AGENT`.
- **Session stream** (`session_protocol.h`, `session_poll_service.cpp`, `session_stream_service.cpp`)
  - Has revisioned `topology` and `agents` channels, plus capability strings from `server_capabilities()`.
  - The client applies them via `RemoteSessionCoordinator`, then `App::apply_remote_agents`, then `AgentController`, then the rail (`chrome_layout.h`).
- **Integration** (`libs/draxul-agent-integration/`)
  - Installs a **user-level** `SessionStart` hook for Claude (`~/.claude/settings.json`) and Codex (`~/.codex/hooks.json`). It is a `sh` wrapper around `python3`, or PowerShell on Windows, and runs `draxul pane report-agent-session`.
- **UI template:** `modules/personal-assistant/` (`PersonalAssistantHost`, a read-only `GridHostBase` list) for a server-backed list pane.

## Design

### Participants and scope

- **Participants:** an agent instance ID such as `happy-otter`, or `user`.
- **Space scope.** Each message records the `space_id` of its sender at send time.
  - A direct recipient must be an agent currently in that Space, or `user`. Otherwise the send fails with `recipient_not_in_space`.
  - `*` expands, at send time, to every agent in the Space except the sender, plus `user`. The expansion is recorded so the history stays accurate if the Space later changes.
  - When the user sends, the Space is the UI's active Space, or the CLI pane's `DRAXUL_SPACE_ID`, or an explicit `--space`.
- **Moving agents.** An agent moved to another Space (`pane move --space`) keeps its pending mail; new mail follows its current Space.

### Message envelope

```json
{
  "id": "m-<seq>", "epoch": "<server epoch at send>", "seq": 1234,
  "space_id": "...", "conversation": "c-<seq>", "reply_to": "m-1200",
  "from": "happy-otter", "to": ["calm-heron"], "broadcast": false,
  "text": "...", "created_unix_ms": 0,
  "recipients": { "calm-heron": { "state": "queued|notified|read|acked|undeliverable", "updated_unix_ms": 0 } }
}
```

- `seq` increases monotonically per Session and **continues across restarts**: the journal restores the high-water mark. Cursors are therefore plain `seq` values. `epoch` only shows which server instance accepted a message.
- A new message without `--conversation` starts a new conversation. `--reply-to` inherits the conversation of the message it replies to.
- **Delivery state per recipient:**
  - `notified`: a doorbell or nudge reached the agent's integration.
  - `read`: the recipient fetched the body.
  - `acked`: explicit acknowledgement.
  - Reading never acknowledges a message. None of these states proves the requested work was done.

### Attributing the sender

An environment variable is routing context, not identity. The server derives `from`:

1. Add `peer_process_id` to `ControlRequest`, filled in by the transport:
   - macOS: `getsockopt(LOCAL_PEERPID)`
   - Linux: `SO_PEERCRED`
   - Windows: `GetNamedPipeClientProcessId`
2. For `mail.*` requests, walk that PID's ancestors (a bounded number of hops) until reaching a process that matches a pane's tracked agent process or the root of a pane's terminal process. This reuses the platform probes behind `AgentProcessObservation`, and adds one helper that returns the parent PID of any process.
3. If the walk reaches an agent in that pane, the sender is **that agent**. A request coming from inside an agent's process tree can never act as `user`.
4. If the walk reaches a pane with no agent, or the request is an attached UI client's request, the sender is **`user`**.
5. Anything else is `unattributed_sender`. The exception is the CLI run outside any pane, which may pass `--from user` explicitly. This is the same-user trust model used throughout, and is documented as such.

`draxul mail whoami` reports the result (participant, pane, Space, how it was resolved), so a manually started agent can learn its identity.

### Recipient lifecycle

- **Managed agent restarted** (new runtime generation, same ID): mail stays queued and is delivered to the new runtime.
- **Discovered agent disappears**, after the existing probe-based removal: its pending messages become `undeliverable`. The sender sees this in `mail status` and in the Mail pane.
- **Server restart:** the journal restores history and per-recipient states.
  - Managed IDs survive Session restore, so their pending mail is still deliverable.
  - Discovered IDs do not survive; their pending mail becomes `undeliverable` once restore finishes.

### Limits and loop control

- **Text:** at most 64 KiB, matching the agent input limit.
- **Broadcast fan-out:** at most 64 recipients.
- **Journal:** at most 4096 messages per Space, with oldest-first eviction. Evicting an unacknowledged message is reported to its sender; it is never silent.
- **Cursors:** a cursor older than the oldest retained `seq` returns `cursor_expired` with the oldest available `seq`.
- **`request_id` deduplication on send:** reuse the bounded completed-mutation pattern, so a retried send never creates a duplicate.
- **Per-sender rate limit**, plus a rule that drops an identical text sent to the same recipient within 10s.
- **Agent-to-agent conversations have a hop budget**, by default 20 messages without a `user` message in the conversation. When it is reached, the conversation is `paused` until the user replies or runs `mail resume`. This bounds unsolicited reply loops.
- **`mail close <conversation>`** ends a conversation explicitly. Later replies to a closed conversation are rejected with `conversation_closed`.

### Persistence

- **Journal location:** `server_session_state_path(...)` with the suffix `.mail.jsonl`, mode 0600. It holds append-only records `{message}` and `{state change}`.
- **Writes happen off the state thread** on a small writer, with order preserved. When the journal exceeds its limits, it is compacted by rewriting it atomically. This uses whatever the atomic-replacement work in `kanban/pending/11 atomic-file-replacement -refactor.md` provides, and otherwise the same temporary-file-then-rename approach as checkpoints.
- **Loading:** the journal is read when the Session initializes, after topology restore. A truncated last line is tolerated; a corrupt file is archived, as checkpoints already are.
- **Privacy:** message bodies never appear in logs, in the sanitized agent projection, or in the session-stream payload beyond what the Mail pane requests.

### Control methods and CLI

Capability string: `agent-mailbox-v1`.

| CLI | Method | Notes |
|---|---|---|
| `draxul mail whoami` | `mail.whoami` | Participant, pane, Space and how it was resolved |
| `draxul mail send <agent\|user\|*> --text … [--reply-to] [--conversation] [--space]` | `mail.send` | Retries are idempotent. Returns `id`, recipients and the expanded broadcast list. |
| `draxul mail inbox [--unread] [--after <seq>] [--limit N]` | `mail.inbox` | Headers plus a preview of the first 200 bytes. Does not mark anything read. |
| `draxul mail read <id…>` | `mail.read` | Bodies; marks those messages `read` for the caller |
| `draxul mail ack <id…>` | `mail.ack` | Idempotent |
| `draxul mail wait [--timeout 30s] [--after <seq>]` | `mail.poll` | The CLI polls about every 250ms with a cursor, like `agent wait`. It is bounded and can be cancelled. |
| `draxul mail log [--space] [--conversation] [--follow]` | `mail.log` | History for the user, paged |
| `draxul mail status <id>` | `mail.status` | Delivery state per recipient |
| `draxul mail close/resume <conversation>` | `mail.conversation` | Loop control |

Everything supports `--json`. When several readers run as the same participant, `read` and `ack` are idempotent and states only move forward. Consumers must deduplicate on `id`; exactly-once execution of the agent's work is not promised.

### Doorbell files (cheap notification)

- **The file.** The server keeps `<runtime-dir>/mail/<session>/<participant>.seq`, holding the highest unread `seq` addressed to that participant. It is rewritten atomically on send, read or ack, off the state thread.
- **The hook check.** Hooks compare it with `<participant>.seen`, the last `seq` they announced. That is one or two file reads, about 1ms in `sh`, so the common no-mail path never starts Draxul or Python.
- **It is a doorbell only.** Message bodies never go to files; the mailbox is still read through the control API.

### Getting agents to participate (provider integration)

Extend `draxul-agent-integration`, keeping it user-level and opt-in through `draxul integration install`, as it already is:

- **`SessionStart`** (existing hook, when `DRAXUL_ENV=1`)
  - Report the session as today, and resolve `mail whoami`.
  - Return `additionalContext` with the agent's identity, its Space peers, the `mail` commands and the handling rules.
  - The rules: peer messages are input from other agents, not user authority; never relay a request your own permissions would refuse; acknowledge after handling; reply with `--reply-to`; respect `conversation_closed` and `paused`.
  - Messages from `user` are treated like direct requests from the user.
  - This reaches **manually launched agents in any repository**, because the hook is user-level.
- **`PostToolUse`**: a cheap doorbell check. When there is new mail, return `additionalContext` with "N unread from @x (conversation c). Run `draxul mail inbox --unread`." Each `seq` is announced only once, the anti-nag rule.
- **`UserPromptSubmit`**: the same doorbell check.
- **`Stop`**: if there is unread mail that has not been announced and `stop_hook_active` is false, block once with the doorbell. Otherwise allow the stop, so the hook never holds up the final response indefinitely.
- **Idle wake, Claude.** Spike both options, ship the more reliable one, and keep the other as a documented alternative:
  - (a) `Stop` hook with `asyncRewake: true` running `draxul mail wait --rewake`. It waits on the doorbell file, writes the doorbell to stderr and exits 2 when mail arrives. It exits quietly when its parent Claude process exits or another turn has started. There is one waiter per participant, enforced by a lock file.
  - (b) The `SessionStart` hook reports `CLAUDE_CODE_MESSAGING_SOCKET`/`TOKEN` to the server, which then posts the doorbell to that socket. The risks: the wire format beyond the auth line is undocumented, and sessions in bypass-permissions mode hold messages from processes they did not start.
- **Idle wake, Codex (and a fallback for anything else): opt-in typed nudge.**
  - Config: `[agents.mailbox] idle_nudge = false`, plus a per-profile `idle_nudge = true`.
  - The server sends one short line through the existing deduplicated `agent prompt` input path, for example `[draxul] 1 new message from @happy-otter — run: draxul mail inbox --unread`. It does so only when all of these hold:
    - status is `Idle` from the manifest;
    - the agent is not `Blocked`;
    - its input box is empty, or this can't be determined and there has been no user keyboard input to that pane for 10s;
    - the nudge has not already fired for this unread `seq`;
    - at least 30s have passed since the last nudge.
  - The nudge sets recipient state to `notified`; delivery is still only confirmed by `read` or `ack`.
- **Repository guidance:** a short rule in `CLAUDE.md` (AGENTS.md and GEMINI.md keep pointing to it) and a messaging section in `.agents/skills/draxul/SKILL.md`. Agents outside Draxul panes or without the installed hooks are documented as unsupported.

### UI

- **Session stream.** Add a `mailbox` channel carrying the revision and unread counts per participant for each Space; message bodies are not included. Advertise it with the capability string.
- **Agents rail.** Show an unread badge on agent pills, and on the Space pill when `user` has unread mail.
- **Toast** when the user receives mail, respecting `enable_toast_notifications`.
- **Mail pane** (`--host mail`, a new `HostKind` modelled on `PersonalAssistantHost`)
  - It belongs to a Space and shows that Space's conversations as threads, newest first.
  - Each message shows sender, recipients and delivery state per recipient.
  - It fetches pages with `mail.log` when the channel revision changes.
  - Keys:
    - `Enter`: open or expand.
    - `g`: go to the agent's pane.
    - `r`: reply.
    - `n`: new message.
    - `x`: close the conversation.
- **Composing.** A one-line compose box at the bottom of the Mail pane; Enter sends as `user`.
- **Command palette:** `open_mailbox` and `send_mail`, both unbound by default.

## Slices

Each slice is a reviewable vertical outcome, followed by a handoff with validation evidence.

### Slice 1 — Mailbox, attribution and CLI

**Outcome:** two shells in panes, plus an agent process, can exchange correlated messages through `draxul mail`; senders are attributed by the server; history survives a server restart.

- `ControlRequest.peer_process_id` on posix and Windows transports, plus a parent-PID helper in `draxul-terminal-process`.
- `ServerMailboxService`, a new source in `libs/draxul-server/src/` owned per `ServerSession`. Pure model types and the journal codec go in a small reusable library, for example `libs/draxul-mailbox/`, so they can be tested without the server and kept out of `app/`.
- `mail.*` methods, capability `agent-mailbox-v1`, the `mail` noun in `libs/draxul-app-cli/src/control_cli.cpp`, and `--help` text.
- Journal writer and loader, compaction, doorbell files.
- Limits, loop control, structured errors.
- **Tests:** isolated-runtime server integration tests (the existing detached-server test harness). They cover:
  - send, inbox, read and ack ordering;
  - broadcasts limited to the Space, and `recipient_not_in_space`;
  - attribution: agent, `user`, unattributed;
  - retried sends and repeated acks;
  - Session isolation;
  - limits and `cursor_expired`;
  - wait timeout and cancellation;
  - restart with journal restore, and discovered recipients becoming `undeliverable`;
  - hop budget and pause.
- **Docs:** `docs/features.md` (CLI, storage lifetime, limits), and the skill's command reference.

### Slice 2 — Participation hooks (busy agents)

**Outcome:** live Claude and Codex agents in the same Space, including one launched by hand, learn their identity at startup and receive doorbells mid-turn and at stop. They exchange correlated replies without screen scraping or prompt injection.

- Extend the `draxul-agent-integration` hook installation (`SessionStart` context, plus `PostToolUse`, `UserPromptSubmit` and `Stop` doorbells), with an upgrade path for existing installs and idempotent uninstall.
- Hook scripts with a `sh` fast path. Measure hook latency on macOS and Windows; if PowerShell startup cost is unacceptable per tool call, add a minimal native `draxul-hook` helper instead.
- `CLAUDE.md` rule and SKILL.md messaging section.
- **Evidence:** a live two-agent exchange, with provider versions recorded. Check that existing input drafts are kept, that the startup identity context is present, that acks are explicit, and that a closed conversation is respected and a user stop takes effect immediately.

### Slice 3 — Idle wake

**Outcome:** an idle agent picks up new mail within a few seconds.

- Claude spike: `asyncRewake` waiter versus inbox socket. Ship the reliable one and record evidence and versions.
- Opt-in idle nudge for Codex and other providers. Check for an empty input box per manifest where possible; config keys; rate limiting.
- **Tests:** server-side nudge-eligibility policy (status, blocked, typing quiet period, rate limit) using fake terminal runtimes.
- **Evidence:** live idle wake for Claude, and for Codex with the nudge on and off. Document that without the nudge, idle Codex mail waits for its next turn.

### Slice 4 — History UI and the user as participant

**Outcome:** the user sees unread badges and toasts, browses threads in the Mail pane, and sends, replies or broadcasts to the Space.

- Session-stream `mailbox` channel (protocol, poll and stream services, client published state, `App` wiring).
- Rail badges and toast.
- `--host mail` pane with threads, delivery state, jump to pane, compose, reply and close; plus palette actions.
- **Tests:** session poll/stream channel round trip, and a render snapshot scenario for the Mail pane plus the rail badge (`draxul-render-*`).
- **Docs:** `docs/features.md` and the config reference.

## Validation (every slice)

- Iterate with `do.py build debug` and focused `--target … --catch` only while diagnosing.
- **Handoff gate:** one `do.py test debug` plus one `do.py smoke --skip-build` from the same cache. Slice 4 adds the Mail pane render scenario.
- When the feature is complete, run `do.py run release` and confirm startup.
- Keep Windows paths valid (named-pipe peer PID, PowerShell hooks, ConPTY ancestry); CI covers the platform that isn't available locally.
- Record a validation-cost summary per slice in the card.

## Risks and open items

- **`asyncRewake` while idle** is undocumented, and the inbox socket's wire format is undocumented. Both are spiked before Slice 3 commits to either.
- **The hook cost per tool call on Windows** may need the native helper.
- **Peer-PID ancestry can break:** a CLI started through `nohup` or `setsid`, or after its parent exits, loses its ancestry. In that case the sender is `unattributed_sender`, never silently `user`.
- **Empty-input detection for the Codex nudge** depends on the manifests. Fall back to the typing quiet period and document the residual risk of the nudge landing in a draft.
