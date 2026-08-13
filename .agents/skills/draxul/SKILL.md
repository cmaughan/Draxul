---
name: draxul
description: "Control a Draxul server headlessly with draxul.exe: inspect and mutate Spaces, tabs, panes, and split trees; create declarative layouts; run, read, and drive terminal panes; launch and supervise managed agents; and verify that attached UIs converge. Use when an agent must organize or operate a Draxul Session from inside or outside a Draxul pane without relying on UI interaction."
---

# Draxul headless control

Drive the server-authoritative topology with `draxul` commands. A UI is optional;
every attached UI projects the same server state while retaining local focus.

## Establish the route

Prefer `--json` and treat returned IDs as opaque stable strings.

```text
draxul --server [--server-runtime-dir <path>]
draxul --server-status --json
draxul --list-sessions --json
draxul --rename-session --session <id> --session-name <name>
draxul --delete-session --session <id> --yes
draxul --shutdown-server --yes [--server-runtime-dir <path>]
draxul space list --session <session-id> --json
```

- Omit `--session` to use `default`.
- Pass `--server-runtime-dir <path>` for a non-default or isolated server.
- Inside a server-owned pane, omit those options when its injected environment
  is correct. Draxul supplies `DRAXUL_SESSION_ID`, `DRAXUL_SPACE_ID`,
  `DRAXUL_TAB_ID`, `DRAXUL_PANE_ID`, `DRAXUL_TERMINAL_ID`,
  `DRAXUL_SERVER_RUNTIME_DIR`, `DRAXUL_SERVER_EPOCH`, and
  `DRAXUL_RUNTIME_GENERATION`.
- Put `--current` where a pane target ID belongs, for example
  `draxul pane get --current --json`.
- If the server reports `unsupported_server`, stop it deliberately and restart
  it with the current executable. Warn before disrupting attached users.
- Session deletion and server shutdown are destructive coordination operations;
  confirm scope and attached users before issuing them.
- For tests, always use a unique `--server-runtime-dir` and shut that exact
  server down in cleanup.

If `draxul` is not on `PATH`, locate the repository build explicitly. On a
standard Windows checkout, prefer `build/Release/draxul.exe`.

## Orchestrate a workspace

For requests such as "make panes that do X, ordered like this," use this flow:

1. Inspect the current Session with `space list`, `tab list`, and `pane list`.
2. Prefer `layout validate` plus `layout apply` when creating a complete new
   Space. Prefer individual commands when modifying an existing Space.
3. Capture `created_id` and `aliases` from JSON; do not recover IDs by display
   name when an exact ID is available.
4. Use `pane run` for shells and `agent start --replace` for managed agents.
5. Use unique completion markers with `pane wait-output`, then inspect with
   `pane read` or `agent get`.
6. Re-list the affected topology and report the stable IDs and observed state.

Do not blindly retry `space create`, `tab create`, `pane split`, or
`layout apply` after an ambiguous response: a successful retry creates another
object. Inspect the server first.

## Spaces and tabs

```text
draxul space list [--session <id>] --json
draxul space get <space-id> --json
draxul space create --name <name> [--root <path>] --json
draxul space rename <space-id> --name <name> --json
draxul space close <space-id> --json

draxul tab list --space <space-id> --json
draxul tab get <tab-id> --json
draxul tab create --space <space-id> [--name <name>] [--cwd <path>] --json
draxul tab create --space <space-id> --name <name> --plugin <plugin-id> \
  [--plugin-config <json>] --json
draxul tab rename <tab-id> --name <name> --json
draxul tab move <tab-id> --delta <-1|1> --json
draxul tab close <tab-id> --json
```

Creating a Space creates its first tab and server shell. Creating a tab creates
its first server shell. A Space root is the fallback cwd for new shells.

`space close` and `tab close` destroy their contained server terminals. Resolve
and re-check exact targets before closing them.

## Panes and split trees

```text
draxul pane list [--space <id>] [--tab <id>] --json
draxul pane get <pane-id|--current> --json
draxul pane split <pane-id|--current> \
  --direction <left|right|up|down> [--ratio <0.1..0.9>] [--cwd <path>] --json
draxul pane split <pane-id|--current> \
  --direction <left|right|up|down> --plugin <plugin-id> \
  [--plugin-config <json>] [--ratio <0.1..0.9>] --json
draxul pane rename <pane-id> --name <name> --json
draxul pane restart <pane-id> --json
draxul pane close <pane-id> --json
draxul pane swap <pane-id> <pane-id> --json
draxul pane move <pane-id> --target <pane-id> \
  --direction <left|right|up|down> [--ratio <0.1..0.9>] --json

draxul split list --tab <tab-id> --json
draxul split set <node-id> --ratio <0.1..0.9> --json
draxul split equalize --tab <tab-id> --json
```

`left`/`up` place the new or moved pane before the target; `right`/`down` place
it after. `pane swap` exchanges two pane positions. `pane move` currently
reparents a pane only within the same tab. Do not simulate a cross-tab move by
closing and recreating a live pane: that loses process state and changes route
semantics.

Headless terminal operations apply only to `server_terminal` panes. A
`client_local` Nvim, Markdown, Kanban, product, or plugin pane requires its owning
UI for rendering and input. Plugin panes are still created and inspected through
the server, and must have an empty `terminal_id`.

## Native GPU plugins

Inspect client-local installations without contacting the server:

```text
draxul plugin list --json
draxul plugin get <plugin-id> --json
```

Plugin IDs are stable lowercase identifiers. `--plugin-config` must be a bounded
JSON object. Creating a plugin pane or tab stores the ID/config in shared topology;
every attached UI resolves it against its own installation. A missing plugin is
therefore not a reason to delete or recreate the pane: the UI displays a placeholder
until it restarts with a compatible plugin installed.

Bundled IDs currently include `dev.draxul.spinning-triangle`,
`dev.draxul.satview`, and `dev.draxul.scoreview`. Create SatView headlessly like
any other plugin:

```text
draxul tab create --space <space-id> --name SatView \
  --plugin dev.draxul.satview --json
draxul pane split <pane-id> --direction right \
  --plugin dev.draxul.satview --json
```

SatView's full product implementation—control panels, camera input, simulation,
catalog/cloud work, Vulkan/Metal rendering, shaders, assets, status/actions, and
local preferences—lives in `plugins/satview`. Do not use `--host satview`:
there is deliberately no compiled-in fallback. If `plugin get` reports it missing,
preserve the shared pane and repair that UI's plugin installation.

Create ScoreView with a local MusicXML source carried in structured launch JSON:

```text
draxul tab create --space <space-id> --name ScoreView \
  --plugin dev.draxul.scoreview \
  --plugin-config '{"source":"C:/scores/piece.musicxml","mode":"paged"}' --json
draxul pane split <pane-id> --direction right \
  --plugin dev.draxul.scoreview \
  --plugin-config '{"source":"C:/scores/piece.musicxml","mode":"flow"}' --json
```

`source` is local to each attached UI even though its text is shared in topology;
an UI without that file shows an inert placeholder. `mode` accepts ScoreView's
reading/runner mode string. `background_playback` defaults to false: hiding the
pane pauses transport, releases local device leases, and stops render deadlines.
Set it true only when the user wants hidden playback. Audio, microphone, and named
MIDI inputs are process-local leases acquired only by explicit interaction or
configuration; a busy device leaves the score usable and reports a local error.
Do not use `--host score`; there is no compiled-in fallback.

## Drive terminal processes

```text
draxul pane run <pane-id|--current> --command <text> --json
draxul pane send <pane-id|--current> --text <text> --json
draxul pane keys <pane-id|--current> <key> [key...] --json
draxul pane read <pane-id|--current> [--lines <1..200>] --json
draxul pane wait-output <pane-id|--current> --text <text> \
  [--timeout <Nms|Ns|Nm>] [--lines <1..200>] --json
```

- `pane run` sends the command followed by Enter.
- `pane send` sends exactly the supplied text without Enter.
- `pane keys` accepts `Enter`, `Return`, `Tab`, `Escape`/`Esc`, `Backspace`,
  `Space`, arrows, `Home`, `End`, `PageUp`, `PageDown`, `Insert`, `Delete`,
  `Ctrl-A` through `Ctrl-Z`, and single literal characters.
- `pane read` returns a bounded current semantic screen, not an unlimited
  transcript.
- `pane wait-output` polls that screen for a substring. Shells echo commands, so
  wait for a unique result marker rather than text that appears in the command.

Example:

```text
draxul pane run <pane> --command "run-tests; echo DRAXUL_TESTS_DONE" --json
draxul pane wait-output <pane> --text "DRAXUL_TESTS_DONE" --timeout 10m --json
draxul pane read <pane> --lines 80 --json
```

Quote command text for the target shell. Do not interpolate untrusted text into
shell commands; prefer direct `send`/`keys` interaction when appropriate.

## Apply a declarative layout

Validate before applying:

```text
draxul layout validate <file|-> --json
draxul layout apply <file|-> --dry-run --json
draxul layout apply <file|-> --json
```

Example schema:

```json
{
  "name": "Automation",
  "alias": "space",
  "root_directory": "D:/repo",
  "tabs": [
    {
      "name": "Workers",
      "alias": "workers",
      "panes": [
        { "name": "Shell", "alias": "shell", "cwd": "D:/repo" },
        {
          "name": "Tests",
          "alias": "tests",
          "cwd": "D:/repo",
          "split_from": "shell",
          "direction": "right",
          "ratio": 0.6
        },
        {
          "name": "Logs",
          "alias": "logs",
          "split_from": "tests",
          "direction": "down",
          "ratio": 0.4
        },
        {
          "name": "Triangle",
          "alias": "triangle",
          "plugin_id": "dev.draxul.spinning-triangle",
          "plugin_config": { "paused": true, "initial_angle": 0.5 },
          "split_from": "logs",
          "direction": "right"
        }
      ]
    }
  ]
}
```

Rules:

- A layout always creates one new Space.
- Every tab has at least one pane.
- Pane aliases are required and all aliases are globally unique in the layout.
- `split_from` references an earlier pane alias in the same tab; when omitted on
  later panes, the preceding pane is used.
- Directions are `left`, `right`, `up`, or `down`; ratios are `0.1..0.9`.
- Validation and dry-run do not mutate the server.
- Apply is atomic for topology and terminal allocation and returns an `aliases`
  map from caller names to stable IDs.
- A pane defines either shell fields such as `cwd`, or `plugin_id` with an optional
  JSON-object `plugin_config`; the two forms are mutually exclusive.
- A layout does not embed commands or agent launches. Use returned shell aliases
  with `pane run` and `agent start` afterward.

## Launch and supervise agents

Use an existing built-in or configured profile ID. Supply explicit routing for
deterministic orchestration.

```text
draxul agent list --json
draxul agent get <instance-id> --json
draxul agent explain <instance-id> --json
draxul agent start <profile-id> [--cwd <path>] \
  [--space <id>] [--tab <id>] [--pane <id>] [--replace] \
  [-- <agent arguments...>] --json
draxul agent prompt <instance-id> --text <prompt> --json
draxul agent keys <instance-id> <key> [key...] --json
draxul agent wait <instance-id> --until <state,state> \
  [--timeout <duration>] --json
draxul agent restart <instance-id> --json
```

`--replace` converts the selected server-terminal pane in place. It preserves
the pane ID while allocating the managed terminal and agent identity. Capture
the returned `instance_id`; use `agent get` for its route and `pane read` on the
returned `route.pane_id` to inspect its screen.

Agent semantic states are `unknown`, `idle`, `working`, `blocked`, and `done`.
Use `agent explain` when a state is surprising. A common completion wait is:

```text
draxul agent wait <instance-id> --until idle,blocked,done --timeout 10m --json
```

`agent focus` is UI-local and is not part of headless orchestration. Likewise,
`space focus` only changes one UI's local view.

Inspect integration hooks before changing them:

```text
draxul integration status [codex|claude] --json
draxul integration install <codex|claude> --json
draxul integration uninstall <codex|claude> --json
```

Only install or uninstall integrations when the user requested that mutation.

## Verify and report

After mutations:

- Re-run the narrowest `get` or `list` command and verify IDs, parent route,
  domain, names, and terminal IDs.
- For process work, require a unique output marker or an expected agent state.
- If UIs are attached, remember that topology convergence is server-driven but
  focus remains client-local.
- Report the Session/runtime used, created or changed stable IDs, commands or
  agents started, observed completion/status, and any destructive cleanup.
