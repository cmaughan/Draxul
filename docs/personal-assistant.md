# Personal agents: a pill and a conversation

A personal agent is an ordinary interactive Codex or Claude conversation with a
persistent backing folder. Click the **+** button below the Personal Agents pills
and chat in the new tab’s terminal. A blank row separates the button from agents,
and only the button itself is clickable. Add starts Codex with **GPT-6.1-Sol** (`gpt-6.1-sol`) by default. There is no definition form or
separate preview/run workflow. An explicit model in the Codex profile takes precedence; resumed conversations
keep their own model. Change models using the provider’s normal controls.
Authentication and permission settings follow the ordinary provider profile.

Click the agent's pill to return to its existing terminal. Double-click its pill
to edit its display name inline, using the same editor as tabs and Spaces. Enter
commits and Escape cancels. Naming is optional and never changes folder identity.
The pill remains available when the terminal is absent. Opening it starts a new
conversation that loads the same backing data. Existing managed-terminal Session
restore continues to use the provider's native resume mechanism when an official
session reference is available. Closing a GUI leaves the server-owned terminal
running; explicitly closing the terminal ends that process.

Right-click (or Control-click on macOS) a pill and choose **Delete agent…**. A separate confirmation defaults
to Cancel. Confirming stops its local conversation, removes its pill/tab, and moves
its complete backing folder into `Vault/PA/.deleted/<id>` for recovery. Other
agents and their tabs remain open. If this was the last tab, Draxul leaves a normal
terminal tab available. Provider-native history follows the provider’s own storage
rules. Closing the menu or cancelling the confirmation makes no changes.

The Personal Assistant palette/tab action is a small entry page: **N** starts an
agent and **Enter** opens the selected conversation. Agent chat stays in the
provider's normal terminal interface.

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

```text
Vault/PA/
  collection.toml
  agents/
    <automatically-generated-id>/
      agent.toml       # Draxul metadata: identity, name, provider profile, revision
      instructions.md # Standing instructions, owned by the agent/user
      state.md        # Optional durable task context maintained by the agent
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

## Execution and persistence

Personal agents use the same managed terminal runtime, status and native-session
integration as other agents. Their bootstrap is supplied at fresh launch and
Session restoration; conversation input is sent directly to the terminal. There
is no separate five-minute runner, execution pin, read-only adapter or job-history
view. Reopening a pill reuses its existing local Session terminal; a second Session
cannot silently launch another instance of the same personal agent and reports
where it is already open.

Dropbox stores instructions and working data. Provider authentication and native
chat history remain under the provider's own storage rules. A shared backing
folder does not transfer an active conversation or coordinate execution across
machines. Automatic schedules, messaging and handoff must be reconsidered against
this conversational model before further implementation.

## Headless access

`draxul personal snapshot --json` inspects the collection. Metadata mutations use
`personal submit --file command.json --json` and `personal result <request-id>`.
The JSON command specifies a unique `request_id`, current `authority` and
`collection_id`, and `action` (`create`, `rename` or `delete`). Create requires a definition
with a chosen profile; identity and default display name are generated by the
server. Rename supplies the original `expected` definition and the new name. Delete
requires `expected` and `confirmed: true`; it closes the matching local terminal
and archives the backing directory, with repeated request IDs returning the same
result.

The authenticated `agent.start` request accepts `personal_id` alongside
`profile_id` and normal Session routing. It validates the collection metadata,
reuses an existing local conversation, or starts the configured provider with its
backing-folder prefix. Subsequent `agent.send_text`, `agent.send_keys`, `pane.read`
and native resume operations are the existing managed-agent protocol.

The `personal-agents-v3` capability versions collection projection and metadata
commands. Metadata IO runs on a worker, clients poll asynchronously, and command
IDs are idempotent within one server lifetime. After an unconfirmed command,
inspect its result/collection rather than blindly repeating creation.
