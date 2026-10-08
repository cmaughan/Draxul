# Personal agents: native chat

Personal Codex agents use a Messages-style pane: your messages on the right,
answers on the left, and a multiline composer below. **Enter** sends;
**Shift+Enter** inserts a line. **Stop** or **Escape** interrupts an active turn.
Scroll to older messages; right-click a bubble to copy it, or use Copy to copy
the last answer. Paste, Select All, Unicode input and IME composition work in
the composer. Routine tool output, reasoning and commentary are hidden; approval
requests remain visible with explicit Allow/Decline controls. Assistant replies
support the Markdown formatting described below.

Click **+** below Personal Agents to create a Codex conversation (GPT-6.1-Sol by
default), or click a pill to focus its conversation. Each identity reuses its own
tab across Spaces. Double-click a pill to rename it. Right-click a pill and choose
**Delete agent…** to stop the local process, close its panes, and move its backing
folder into `Vault/PA/.deleted/<id>`. Cancellation makes no changes.

This UI applies only to personal agents. General managed and discovered agents
keep their provider terminals, keyboard controls and discovery behavior.
Codex is the first structured adapter. Claude personal profiles show an explicit
unsupported-provider message until the Claude adapter is implemented; ordinary
Claude terminals continue to work. No terminal fallback is launched by the panel.

An explicit `--model`/`-m` in the Codex profile overrides the fresh-conversation
default. Resuming otherwise keeps the thread's model. Profile `-c`/`--config`,
`--enable` and `--disable` options are passed to app-server; terminal-only flags
are rejected with an explanation. Existing Codex sign-in applies. Personal threads start and resume with **Approve for me**:
`workspace-write` sandbox, `on-request` approval policy, and `auto_review` reviewer.
Eligible approval requests go through Codex's automatic reviewer; requests still
requiring user input appear in the panel. This does not grant unrestricted access
or change permissions for ordinary terminal agents. Explicit filesystem/network
permission requests also appear in the chat's Allow/Decline controls. Allow grants
exactly the displayed permissions for the current turn; Decline grants none.
macOS privacy consent and provider/account sign-in remain separate from Codex
approval policy: Draxul cannot grant OS access or repair expired credentials on
its own. See [Codex permissions](https://learn.chatgpt.com/docs/sandboxing).

## Shared backing folder

All machines use **`Vault/PA` under their local Dropbox root**. On this Mac:

```toml
[agents]
personal_root = '/Users/cmaughan/Dropbox/Vault/PA'
```

Other machines set the same key to their own absolute Dropbox path. Keep the
shared collection identity unchanged. The server reads configuration at startup;
restarting only the GUI does not load a changed root. This Mac's collection has
been initialized, without adding example agents.

On Windows, set the key in `%APPDATA%/draxul/config.toml`:

```toml
[agents]
personal_root = 'C:/Users/cmaughan/Dropbox/Vault/PA'
```

After rebuilding, inspect `draxul --server-status --json` and any live terminals
before restarting the server. Use `draxul --shutdown-server --yes`, then launch
the rebuilt `draxul.exe --server` or GUI. The client refreshes the Windows server
helper during launch; starting an old `draxul-server.exe` directly retains its
old implementation. A configured example collection does not select the Dropbox
collection.

```text
Vault/PA/
  collection.toml
  agents/
    <automatically-generated-id>/
      agent.toml       # Draxul metadata: identity, name, provider profile, revision
      instructions.md # Standing instructions, owned by the agent/user
      schedule.md     # User-requested schedules and timezone, maintained by the agent
      state.md        # Durable task context and completed/next scheduled work
      data/           # Working files
```

Creating an agent stages the folder and publishes it by rename. Draxul seeds a
minimal `instructions.md` and creates `data/`; it does not invent a task for the
agent. The startup prefix gives the absolute backing-folder location and tells
the provider to read its instructions and existing state, initialize missing
working files if necessary, preserve existing data, and follow the user's chat.
Ask the agent to change its standing instructions through the same conversation.
Renaming changes only Draxul metadata and preserves these agent-owned files.

Both manifests use **schema version 3**. IDs and revisions are internal details.
An agent's metadata contains `id`, `name`, `profile` and `revision`; model choice
belongs to the ordinary provider configuration/chat. The collection is limited
to 32 agents, metadata to 4 KiB and displayed/scanned instructions to 16 KiB.
Invalid manifests, unsupported versions, escaped paths and recognizable Dropbox
conflicts are surfaced and block a new launch. Missing instruction files are
allowed so the bootstrap can initialize them. A missing collection is never
silently replaced.

## Formatted replies

Assistant replies use Draxul's existing Markdown parser, layout and GPU text
renderer inside the chat bubbles, including bold/italic text, headings, lists,
quotes, tables and fenced code. Message layout is cached and refreshed as an
answer streams or the pane width/font size changes. User messages and the
composer remain literal text. Right-clicking a bubble copies its original source,
including Markdown, so formatting survives pasting into another editor.
HTTP/HTTPS link labels appear blue and underlined with a hand cursor. Left-click
opens the destination in the system's default browser; right-click still copies
the original message. Wrapped and emphasized links retain the same destination.
Other URI schemes and relative paths are not launched from personal chats.

Markdown images in assistant paragraphs and lists render inside the bubble with
proportional sizing and scroll clipping. Local PNG, JPEG, BMP and GIF files (first
frame) work with absolute paths, local `file://` URLs, or paths relative to that
agent's `agents/<id>/` folder. Percent-encoded paths and Unicode filenames work.
Image loading and decoding run off the UI thread, one visible image at a time.
Hidden image textures are released; decoded dimensions remain cached for layout.
The loader limits files to 16 MiB, images to 8 megapixels and visible textures to
64 MiB per chat. Missing, damaged, oversized or unsupported images show a labeled
placeholder. HTTP image URLs, SVG and data URLs are not fetched/rendered yet;
ordinary HTTP/HTTPS text links still open in the browser. Document and Kanban
Markdown viewers retain their existing image-alt-text presentation.


## Execution and persistence

The Draxul server owns one `codex app-server --listen stdio://` process per configured
personal identity, speaking structured JSON over pipes. Closing a tab or the GUI
leaves the conversation running. Reopening observes the same conversation and
never resends a message. Server startup prepares the configured personal identities
without requiring an open tab and resumes saved Codex threads where available.
Unstarted conversations have no durable provider history yet, so their transient
IDs are not saved for resume; reopening creates a fresh thread from the same
backing instructions. Provider exits, broken pipes and malformed provider output
trigger an automatic replacement with delays of 1, 2, 4, 8, 16 and then at most
30 seconds between attempts. If Codex reports that a saved rollout is missing, Draxul starts a fresh
thread in the same folder and keeps the displayed conversation. Each replacement
receives the current backing-folder instructions. Interrupted requests are kept
visible with a notice instead of replaying possibly completed side effects.

Temporary recovery shows “Starting assistant…” and needs no Retry click.
Persistent provider failures show an actionable waiting reason while retries
continue. Invalid metadata, inaccessible files, invalid local state and unsupported
providers still require attention: Draxul never fabricates an empty Dropbox
collection or silently ignores account/OS access requirements. All of this
lifecycle is confined to personal conversations; ordinary terminal agents keep
their existing launch/discovery controls.

The server stores the thread ID and a bounded display transcript under its local
user-data `draxul/personal-chat/<collection-id>/<agent-id>.json` directory (or the
configured personal local state's `chat` directory). Provider-native history stays
with Codex. A per-agent local executor lock prevents two servers from driving the
same local conversation. The panel retains up to 128 messages / 192 KiB of text;
individual answers are limited to 64 KiB and prompts to 16 KiB. Trimming the display
history does not trim Codex's thread context. Malformed/oversized provider output,
startup failure and unsupported interactions are surfaced in the panel. Unsupported
provider requests are rejected, never silently approved.

Dropbox stores only the shared backing instructions and working data described
above. It does not transfer an active provider thread or coordinate concurrent
execution on different machines. Run personal scheduling on one machine at a time;
cross-machine ownership remains separate work.

## Background schedule checks

Every five minutes after server startup, the server wakes every valid personal
Codex agent, including agents whose chats have never been opened. It resumes the
same local thread when available. Closing the UI does not disable this timer.
Busy or approval-blocked agents retain at most one deferred check; it runs when
the current turn finishes. Stop cancels a deferred check. Missing/invalid/deleting
identities are skipped. Recovering providers keep at most one pending schedule
check; their ordinary replacement policy applies.

When you request scheduled work in chat, the agent is instructed to record the
recurrence, timezone and next due time in `schedule.md`, and completions in
`state.md`. Each wake supplies the current UTC time and asks the agent to inspect
those files and standing instructions, perform only due authorized work, and
record progress. Existing external timers remain responsible for their own work;
the agent is told to avoid duplicate execution or delivery. Draxul supplies the
timer; the model interprets the schedule.
Existing instructions/state are also checked, so previously saved schedules can
be discovered. A promise that was never saved in the agent's files may need to be
restated in the chat.

Internal wake prompts and successful no-op results are hidden. Meaningful results
become assistant messages; provider failures and invalid schedule responses remain
visible as errors. `personal.chat.snapshot` includes `schedule_checks` (attempts
started in this process) and `last_schedule_check` (UTC) for inspection.

Checks require the server and computer to be running and Codex to be available.
They use model capacity even when nothing is due. Timing is approximate: busy
agents, sleeping computers and unavailable services delay checks, with no burst
of missed timer ticks on recovery. This is not an OS alarm or an exact-time job
scheduler. Claude checks await its structured adapter.

## Headless access

`draxul personal snapshot --json` inspects the collection. Metadata mutations use
`personal submit --file command.json --json` and `personal result <request-id>`.
The JSON command specifies a unique `request_id`, current `authority`,
`collection_id`, and `action` (`create`, `rename` or `delete`). Rename supplies the
original `expected` definition and new name; delete requires `confirmed: true`.

An authenticated `agent.start` request with `personal_id` and its `profile_id`
opens the native conversation and focuses/reuses its topology tab. `agent list`,
`get`, `explain`, `wait` and named aliases expose its structured status. A personal
`agent prompt`/`send` submits a complete message; no Enter key is necessary.
Personal chats have no terminal ID and reject terminal-key/restart operations.
Ordinary agent CLI behavior is unchanged.

The `personal-chat-v1` capability identifies this structured protocol. Native clients use `personal.chat.open`, `personal.chat.snapshot`,
`personal.chat.command` (actions `send`, `stop`, `approve`, `decline`) and
`personal.chat.reconnect`. Commands require `agent_id` and a unique `request_id`;
approval decisions also echo `approval_id`. Mutation transport is never replayed
into a new server epoch. Metadata and provider IO run off the kernel/UI threads,
and clients poll immutable snapshots asynchronously.
