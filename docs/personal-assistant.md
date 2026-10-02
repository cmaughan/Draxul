# Personal Assistant: discovery slice

The built-in `personal-assistant` host inspects one server-owned collection.
It does not launch providers, schedule work, edit instructions, claim ownership,
or contact external services yet. An enabled field in a manifest does not change
that: execution is unconditionally disabled in this slice.

## Try it

Build with `py do.py build debug` (Windows) or `python3 do.py build debug`
(macOS). The host is built into core, not an optional plugin.

In the normal Draxul `config.toml`, add an absolute path under `[agents]`:

```toml
[agents]
personal_root = 'D:/dev/Draxul/examples/personal-assistant'
```

Use the equivalent absolute path on macOS. A locally available Dropbox folder
can be used instead; copy the example hierarchy there and change this setting.
The setting is read when the shared server starts. Reloading client configuration
does not change a running server's collection. Before deliberately restarting a
server, inspect its terminals and stop/checkpoint valuable work normally.

Start the newly built executable with `--host personal-assistant`, or use the
command palette's Personal Assistant new-tab/split entry. Repeated tab launches
focus the existing Personal Assistant tab, including across Spaces in the same
Session. Explicit splits remain separate. Without configuration,
the host displays setup guidance. A standalone client without a shared-server
connection displays a waiting state rather than reading Dropbox independently.

Configured collections also appear in a separate **Personal Agents** sidebar
section. Click a row to open/focus its detail host. Up/Down (or J/K) selects a
definition; Page Up/Page Down scrolls its instructions. The rail is clipped to
available space; the host's keyboard navigation reaches every definition.
The existing Agents section continues to describe ordinary pane-backed runs.
Personal entries marked `[off]` mean execution is unavailable, not that the
manifest's enabled setting has been changed. `[!]` indicates a collection or
definition error; open the detail host for the diagnostic.

## Current folder format

```text
collection.toml
agents/
  news/
    agent.toml
    instructions.md
```

The example fixes the current schema. Both manifests require `schema_version = 1`;
unknown fields and versions are rejected. Identities contain only lowercase ASCII
letters, digits and hyphens, at most 64 characters. Definition identity must match
its folder. Names, profile and model are nonempty bounded strings; revisions are
positive integers. `enabled` is a boolean. Intervals range from 60 seconds to one
year. Profile/model names are displayed, not resolved or executed in this slice.

The limits are 32 definitions, 4 KiB per manifest and 16 KiB of UTF-8 instructions
per definition. Extra memory/inbox/run directories inside a definition are left
untouched. Do not put unrelated entries directly inside `agents/`.

The server scans independently of its views, roughly once per second. Clients
read the shared projection asynchronously. Authenticated `personal.snapshot` and
`personal.get` (with `agent_id`) provide schema-versioned collection projections;
they are collection-scoped, not Session-scoped. The projection explicitly reports
`execution_enabled = false`. Closing a view neither deletes definitions nor
stops discovery. Client disconnection labels retained data as last-known.

Missing roots are never recreated. Malformed, unavailable, oversized, escaped-link
or recognizable Dropbox-conflicted definitions produce diagnostics. Last-known
data is retained for inspection while sync settles, including missing definitions;
there is no definitive deletion operation yet. Restarting the server discards
this in-memory last-known cache. Collection errors label all retained content
untrusted for execution. Filesystem checks defend ordinary link escapes, not
malicious concurrent replacement of files by another local process.

## Manual acceptance

1. Point a fresh server at the example and open the host in two clients using
   different named Sessions. Both should show the same News monitor definition.
2. Edit the sample instructions externally. Both views should converge after
   the next server/client polling cycle, without launching a provider.
3. Temporarily break `agent.toml` or remove `instructions.md`: retain the last
   good instructions with a visible diagnostic, then recover after restoration.
4. Close and reopen the host; the definition should remain available.
5. Disconnect the server: existing content is last-known, never presented as
   evidence of an active personal execution.

Creation/editing, run lifecycle, schedules and cross-machine ownership are later
slices in `plans/personal-assistant.md`. Dropbox is storage, not a distributed lock.
