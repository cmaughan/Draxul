# Personal agents: native chat panel

Replace personal agents' terminal launcher with a quiet Messages-style conversation pane. Ordinary managed/discovered agents retain their existing PTY UI and controls.

## First vertical slice: Codex personal conversations

- [x] Server-owned structured Codex app-server connection, bounded asynchronous IO, error and cancellation handling.
- [x] Persist provider thread identity locally; closing/reopening a client pane reconnects without killing the conversation.
- [x] Native chat bubbles, composer, scrolling, copy, working state and explicit approval controls; tool/reasoning events stay out of the conversation.
- [x] Personal pill/create actions focus the matching conversation, without changing ordinary agents.
- [x] Integration tests with a fake structured provider, UI tests, aggregate validation and same-cache startup/render checks.
- [x] Document supported behavior and remaining provider work.

## Scope and decisions

The default personal provider is Codex. Implement a complete Codex slice first, with a clear error for personal profiles without a structured adapter (never silently launch a terminal). Claude structured personal chat is a subsequent slice; ordinary Claude/discovered terminals remain supported throughout. Shared Dropbox instructions and working data are retained; provider session IDs/transcripts stay machine-local. No credentials enter Dropbox. Existing rename/CLI alias changes in this working tree are preserved.

## Implementation notes

- Separate `PersonalChatService`/`PersonalChatProcess` workers use bounded JSON pipes (POSIX process groups / Windows restricted handles and a kill-on-close job). Codex startup and blocked writes have time limits; cancellation does not require GUI lifetime.
- `personal.chat.*` authenticated APIs and `PersonalAgentClient` carry snapshots and queued commands; ordinary agents continue through their previous PTY path. Personal prompts and aliases remain usable through the agent CLI; personal chat has an empty terminal ID and explicitly rejects terminal key/restart operations.
- The native NanoVG panel supports message bubbles, UTF-8/IME composition, multiline send, paste/select-all/editing, scroll/copy, stop, explicit approvals and retry. Commentary/reasoning/tool output never enters the answer transcript. Messages are plain text in this slice.
- Thread identity and bounded display history remain local. Executor locks prevent duplicate local servers; Dropbox continues to own instructions and working data only. Commands are not replayed after a connection loss. Rejected drafts are retained using request-ID correlation.
- A personal identity gets one reusable tab across Spaces. Deletion stops its process, closes its native panes and archives the backing folder. Existing rename/alias work is preserved.
- Claude adapter work is explicitly transferred to `kanban/pending/108 claude-personal-chat-adapter -feature.md`; unsupported personal providers get a clear error rather than a terminal fallback.
- Render fixtures are supplied by the render-test boundary only; the normal app never substitutes fixture messages. macOS has a recorded native chat snapshot; Windows exercises the same host and real pipe-process integration tests in normal CI.

## Validation so far

- Structured fake-provider integration: normal turns, hidden commentary/tool/reasoning, cross-client reuse, restart/resume, idempotence, busy rejection, stop, approvals/refusal, deletion and malformed/crashed providers.
- Native host input: multiline Unicode input, IME confirmation does not send, Stop and closing the pane does not stop its provider.
- Installed Codex live probe: initialize/thread/start and one sandboxed, tool-free turn succeeded; final-answer event returned exactly “Draxul chat ready.”
- Core aggregate passes and render inspection completed during iteration; final aggregate/smoke/render evidence follows below.

## Final validation and review point

- Final `python3 do.py test release`: **56/56 core CTest entries passed, 19.93 s**. One same-cache aggregate build target (`draxul-tests-core`) took **6.23 s**; configure/generate reused the cache on this last run. Earlier explicit reconfiguration registered the new render scenario.
- Same Release-cache personal-host startup against an explicitly isolated `/tmp/dc-*` server: **passed, 0.51 s**; explicit server shutdown also passed. The user's existing server/agents were left running.
- `draxul-render-personal-assistant`: **passed, 1.39 s**, after visual inspection and reference capture. Native chat reference is `tests/render/reference/personal-assistant.macos.bmp`; `do.py personal` / `do.py blesspersonal` own compare/bless.
- Earlier focused passes: server personal cases 7 cases / 2.67 s, then 8 cases / 3.12 s after replacing terminal expectations with structured chat; native host 2 cases / 0.02 s. These overlap the final aggregate because they were iteration checks at the process/UI boundaries.
- Aggregate iterations before the final gate: 24.61 s pass; 23.13 s pass; 21.35 s failure (new render inventory expectation plus existing checkpoint race); 20.87 s pass; 20.65 s failure (same checkpoint race). Compilation also caught missing header declarations during integration. The inventory expectation was updated and the checkpoint test now uses the same bounded asynchronous-file wait as its neighboring test, instead of asserting immediate persistence. No checkpoint production behavior changed.
- Two reference captures refined compact-window spacing; subsequent render comparisons passed. Windows execution/remote CI were not run here; ordinary CI covers the native pipe process and host tests. No separate routine Windows gate was added.
- Final review also fixed a create/open ordering race: the acknowledged creation definition opens the pane directly, without waiting for a possibly stale client metadata snapshot. Deleted client subscriptions are pruned and live conversation counts are bounded.

Review the chat layout, quiet answer filtering, explicit approval flow and close/reopen behavior before the next provider slice. Restart the Draxul server and GUI together to use the new `personal-chat-v1` capability; the existing live installation was not stopped. Changes are not committed or pushed in this turn.
