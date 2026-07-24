# Herdr research and Draxul terminology alignment

**Status:** research and preliminary design direction  
**Date:** 2026-07-21  
**Implementation note:** the behaviour-neutral `Workspace` to `Tab`, `HostManager`
to `PaneManager`, snapshot-type renames, `TabController` extraction, and the
in-memory `Space`/`SpaceController` lifecycle were completed on 2026-07-22. Draxul
can now create, activate, rename, re-root, enumerate, and close multiple live Spaces;
inactive hosts keep pumping and retain their processes. The app-level lifecycle,
command-palette actions, and clickable multi-Space left rail are also implemented.
The rail now reserves a lower Agents section with its heading and an application-shell
divider; agent rows and persistence v2 remain future work. The source-backed
[multi-Space persistence plan](multi-space-session-persistence.md) corrects the earlier
"last active Space" shorthand: all loaded Spaces must be restored, with the saved active
Space applied only after the collection has been rebuilt. It also refines the agent
model so pane-owned identity is authoritative and the sidebar is a derived projection,
rather than a second durable registry.
**Scope:** local spaces, agent discovery and agent orchestration inside the Draxul process  
**Out of scope:** detach/reattach ownership, suspend/resume, background server handoff,
SSH, remote workspaces, and worktree management

## Summary

Herdr is an agent-aware terminal multiplexer. Its useful local model is not merely
two sidebar lists: a persistent session contains project-level workspaces, each
workspace contains tabs and terminal panes, and a pane may contain a recognised
agent. Herdr's sidebar calls the project-level entries **Spaces**, even though its
concept documentation calls the underlying object a **workspace**.

Draxul already has most of the lower-level substrate: tabs, split panes, stable pane
identities, ConPTY/PTY-backed shell hosts, terminal-grid state, shell working-directory
tracking, and file-backed topology restore. The main mismatch is terminology and one
missing hierarchy level:

```text
Before pass: Session -> Workspace -> Pane -> Host
Recommended: Session -> Space -> Tab -> Pane -> Host
                                              `-> Agent (optional occupant)
```

The highest-value preliminary rename is therefore Draxul `Workspace` to `Tab`.
A new `Space` can then mean one repository, task, or investigation without colliding
with the existing type. `Session` remains the outer Draxul runtime/restore namespace.

For a useful local-first implementation, multiple spaces should remain live while the
Draxul application is open. Switching spaces should change focus and rendering, not
load a different saved session and terminate the previous space's agents. Closing
Draxul may continue to end all child processes; persistence can initially restore the
last active space and respawn saved topology just as Draxul restores shell sessions
today.

## Herdr's concept model

### Session

A Herdr session is a persistent server namespace. A named session has its own server,
socket, workspaces, tabs, panes, processes, and saved runtime state. Named sessions are
intended for completely separate environments; the documentation recommends using
workspaces first.

### Space / workspace

A Herdr workspace is the top-level project container inside a session. It is intended
to represent one repository, task, or investigation and owns tabs and panes. Agent
state rolls up to this level so the sidebar can show which project needs attention.

The current sidebar uses **Space** as the presentation term. This is visible in the
configuration API: the agent-panel ordering value is `spaces`, with `workspaces`
accepted as an alias, and expanded Space rows consume workspace metadata tokens.

Consequently, a Herdr Space is **not** a collection of sessions:

```text
Herdr session
|- Space / workspace: project A
|  |- tab: agents
|  |  `- panes
|  `- tab: logs
`- Space / workspace: project B
   `- tabs and panes
```

### Tab

A tab is a layout within a Space. Tabs separate views such as agents, logs, servers,
tests, or review work. A tab owns one or more split panes.

### Pane

A pane is a real terminal connected to a process through a PTY. It retains its shell,
output, terminal state, and process while the Herdr server remains alive. Ordinary
commands, servers, tests, and shells can occupy panes without being agents.

### Agent

An agent is a process Herdr recognises inside a pane. The agent is an occupant of the
pane, not the pane itself. This distinction allows the same pane to return to being an
ordinary shell after the agent exits.

Herdr exposes the semantic states:

| State | Meaning |
|---|---|
| `blocked` | The agent needs input, approval, or a decision. |
| `working` | The agent is actively running. |
| `done` | Work finished and has not yet been viewed. |
| `idle` | The agent is finished or waiting and has been seen. |
| `unknown` | Herdr cannot classify the state confidently. |

`done` is partly an attention state rather than a native process state. It remains
visible until the user views the relevant agent. Status rolls upward from agent to
pane, tab, and Space.

## How Herdr tracks agents

Herdr uses several complementary signals. It assigns one authority for semantic state
so different signal sources do not compete.

### Foreground-process identity

Herdr first inspects the foreground process in each pane. This identifies a known
agent executable when it is directly visible. Wrappers, nested multiplexers, VMs, and
sandbox processes can obscure the real process, so process inspection is evidence,
not a universal solution.

### Screen-manifest detection

For agents without complete lifecycle hooks, Herdr examines the live bottom of the
terminal buffer and evaluates TOML detection manifests. Rules may also use terminal
titles and progress sequences. Detection deliberately follows the live bottom buffer,
not a user's scrolled-back viewport.

Blocked detection is strict: Herdr only reports `blocked` when the current screen
matches a known approval, permission, question, or decision UI. If a known agent has
no matching rule, it falls back conservatively rather than inventing an interaction
state.

The manifests are bundled, may receive validated remote updates, and support local
overrides. Herdr also provides an explain operation showing the manifest, matching
rule, evidence, and fallback reason. The explainability is important because terminal
UI heuristics inevitably drift as agent products change.

### Hooks and plugins

Optional agent integrations have two distinct roles:

1. **Lifecycle authority**: sufficiently complete hooks or plugins report semantic
   state directly.
2. **Native session identity**: a hook reports the agent's own conversation/session
   reference so it can later be resumed.

Codex and Claude Code integrations report native session identity, but their semantic
state still comes from screen manifests because their hooks do not observe every
approval, interruption, or lifecycle transition. Other supported agents have more
complete lifecycle integrations and can make their hook reports authoritative.

This separation should be preserved in Draxul: an agent's conversation identifier is
not evidence that it is currently working or blocked.

### Local control plane

Herdr exposes its running model through a local socket API and CLI wrappers. The
control surface can list, inspect, start, focus, read, prompt, send input to, and wait
on agents. It can also subscribe to resource events and accept custom agent-state
reports.

This API is what changes Herdr from an agent dashboard into an agent harness: agents
and scripts can orchestrate the environment in which other agents run.

## Herdr persistence boundaries

Herdr distinguishes several mechanisms that should not be conflated:

| Mechanism | Processes survive? | Layout survives? | Agent conversation survives? |
|---|---:|---:|---:|
| Client detach / reattach | Yes | Yes | Yes, because the original process remains alive |
| Server restart snapshot | No | Yes | Only through native agent resume |
| Screen-history replay | No | Yes | No; it restores display history, not the process |
| Native agent resume | No | Yes | Yes, for integrations that reported a valid reference |
| Live server handoff | Best effort | Yes | Yes if process transfer succeeds |

The proposed Draxul work initially targets only snapshot-style restoration. There is
no background owner: closing the application ends its processes, and reopening it
respawns saved panes. Native Codex/Claude conversation restore can be added later
without first implementing detach, handoff, or SSH.

Terminal screen history should not be persisted by default because prompts and output
may contain credentials, tokens, source code, or other sensitive data.

## Draxul before the terminology pass

The former `Workspace` type, now [`Tab`](../app/tab.h), is explicitly one
self-contained pane layout and one top-bar tab. It owns a
[`PaneManager`](../app/pane_manager.h), which owns
the split tree, pane leaves, host instances, launch options, and stable pane strings.

The former `AppSessionState`, now [`SessionSnapshot`](../app/session_state.h), owns a
vector of `TabSnapshot`, an active tab ID, and the next tab ID. The live collection,
selection, navigation, pane-layout snapshot/restore, and shutdown boundary lives in
[`TabController`](../app/tab_controller.h). That controller is now owned by one default
[`Space`](../app/space.h), and [`SpaceController`](../app/space_controller.h) owns and
routes the active Space while `App` supplies host dependencies and orchestrates the
outer saved session. The version-1 snapshot is still the default Space's tabs without a
persisted Space wrapper. Its historical `workspace` and `host_manager` keys remain
unchanged for backward and forward compatibility; these are wire-format details rather
than live terminology.

The application already has several useful foundations:

- multiple live top-bar tabs, each with an independent split tree and host set;
- stable persisted pane strings in addition to internal split-tree `LeafId` values;
- saved host launch command, arguments, working directory, and startup commands;
- ConPTY on Windows and PTY-backed processes on macOS/Linux;
- terminal-grid contents and live activity timestamps;
- OSC 0/2 terminal titles and OSC 7 current-directory tracking;
- periodic file-backed shell topology checkpoints;
- command-palette actions to save and load named sessions.

There are also relevant constraints:

- [`App::can_snapshot_session_state()`](../app/app.cpp) currently rejects the whole
  session snapshot if any tab does not consist entirely of restorable shell
  hosts. Space-aware persistence should avoid allowing one non-restorable product
  space to block checkpoints for every agent space.
- `App` still owns saved-session transactions, while `SpaceController` owns the default
  Space and its `TabController`. Phase 1 can extend that controller instead of adding a
  Space hierarchy directly to `App`.
- [`ChromeHost`](../app/chrome_host.h) currently owns top-bar and pane-chrome layout.
  A substantial sidebar should use a dedicated controller and pure layout model rather
  than turning `ChromeHost` into the owner of space and agent domain state.
- Windows ConPTY does not expose the same simple foreground-process relationship as a
  Unix PTY. Explicit Draxul agent launches and hook reports should be primary identity
  signals; process-tree inference should remain best effort.

Two existing ice-box items are adjacent to this direction:

- [`22 app-tab-session-controllers -refactor.md`](../kanban/ice-box/22%20app-tab-session-controllers%20-refactor.md)
  proposes extracting the current tab and file-backed session ownership from
  `App`.
- [`42 global-session-tab-switcher -feature.md`](../kanban/ice-box/42%20global-session-tab-switcher%20-feature.md)
  proposes a searchable index of sessions, tabs, panes, directories, and
  titles. Its query model could also feed a sidebar instead of building two unrelated
  navigation indexes.

## Preliminary terminology alignment

The recommended user-facing hierarchy is:

```text
Session
`- Space
   `- Tab
      `- Pane
         |- Host
         `- Agent (optional)
```

### Core renames

| Pre-rename Draxul name | Adopted replacement | Rationale |
|---|---|---|
| `Workspace` | `Tab` | The object is one top-bar tab and one pane layout. |
| `WorkspaceSessionState` | `TabSnapshot` | It is the persisted representation of one tab. |
| `workspaces_` | `tabs_` | Matches the object actually stored. |
| `active_workspace_` | `active_tab_id_` | Makes identifier semantics explicit. |
| `next_workspace_id_` | `next_tab_id_` | Matches the renamed object. |
| `create_workspace()` | `create_tab()` | Matches the existing `new_tab` UI action. |
| `activate_workspace()` | `activate_tab()` | Describes the visible operation. |
| `close_workspace()` | `close_tab()` | Describes the visible operation. |
| `find_active_workspace()` | `find_active_tab()` | Removes the workspace/tab ambiguity. |
| `AppSessionState` | `SessionSnapshot` | Separates the persisted snapshot from a future live `Session` model. |
| `HostManager` | `PaneManager` | It owns the split tree, panes, and their host occupants for one tab. |
| `HostManager::SessionState` | `PaneLayoutSnapshot` | It is a split-tree and pane snapshot, not a complete session. |
| `PaneSessionState` | `PaneSnapshot` | Straightforward persistence terminology. |
| `TabSnapshot::pane_manager` | `pane_layout` | Snapshot data describes a layout, not its runtime owner. |
| `PaneManager::session_state()` | `snapshot_layout()` | Names the specific state captured by the manager. |
| `PaneManager::restore_session_state()` | `restore_layout()` | Keeps session orchestration outside the pane owner. |

`Workspace` should disappear from normal Draxul UI and most application code. Herdr
uses the word in its documentation, but retaining Draxul's current meaning of
"workspace equals tab" would continue to conflict with both Herdr and common editor
terminology.

### Names to retain

- **Session** remains the outer selected Draxul environment and restore namespace.
- **Pane** remains the visible split and stable routing target.
- **Host** remains the implementation inside a pane: shell, Neovim, Markdown, Kanban,
  MegaCity, SatView, ScoreView, and so on.
- **LeafId** remains an internal split-tree identifier. It should not become a public
  `PaneId`; Draxul already has a separate stable pane identity suitable for persistence
  and APIs.

The `PaneManager` rename completes the preliminary ownership vocabulary. It remains a
pane-layout owner; it does not become the future `TabController` or `SpaceController`.

### New Space vocabulary

The new model should use:

| Name | Meaning |
|---|---|
| `SpaceId` | Stable identity for a project/task container. |
| `Space` | Live space owning tabs and active-tab selection. |
| `SpaceSnapshot` | Persisted space metadata and tab snapshots. |
| `SpaceController` | Create, close, activate, rename, and enumerate spaces. |
| `root_directory` | Default project/repository directory for new tabs and agents. |
| `active_space_id` | The space currently rendered and receiving navigation input. |

An initial shape is:

```cpp
struct Space
{
    SpaceId id;
    std::string name;
    std::filesystem::path root_directory;
    TabController tab_controller;
};
```

The Phase 0 default Space uses ID `0`, the name `default`, and the launch working
directory when one is supplied. It is deliberately not written into version-1 session
files.

### Agent vocabulary

| Name | Meaning |
|---|---|
| `AgentKind` | Canonical product kind such as Codex, Claude, Cursor, or custom. |
| `AgentTarget` | Stable agent identity plus its Space, tab, and pane routing. |
| `AgentStatus` | `working`, `blocked`, `done`, `idle`, or `unknown`. |
| `AgentLifecycle` | Starting, running, exited, or failed process lifecycle. |
| `AgentRegistry` | Collection and index of all currently known agents. |
| `AgentObservation` | Immutable process, screen, title, and hook evidence. |
| `AgentStateAuthority` | Signal source currently allowed to author semantic status. |
| `AgentSessionRef` | Optional native Codex/Claude conversation identifier. |
| `AgentController` | Start, focus, send, read, wait, and lifecycle operations. |

An agent must not be modelled as a special pane type. It is an optional recognised
occupant of a terminal pane. This permits ordinary commands and shells to continue
using the same terminal implementation and allows a pane to outlive an agent process.

## Session format migration

The next session format should make the new hierarchy explicit:

```text
SessionSnapshot
|- session_id
|- session_name
|- active_space_id
`- spaces[]
   `- SpaceSnapshot
      |- space_id
      |- name
      |- root_directory
      |- active_tab_id
      `- tabs[]
         `- TabSnapshot
            `- PaneLayoutSnapshot
```

Version-1 Draxul session files should load by wrapping their existing `workspaces`
array as `tabs` in one generated default Space. The version-1 reader must remain
available; version 2 should write only the new hierarchy. Migration should not rewrite
a user's saved file merely because it was inspected or listed.

Agent metadata should be separated into durable and ephemeral fields:

- Persist agent kind, display name, launch descriptor, routing identity, and an
  opt-in native session reference.
- Do not persist semantic `working` or `blocked` state as authoritative; re-detect it
  after launch.
- Do not persist screen text by default.
- Treat missing, duplicated, stale, or unsupported agent session references as normal
  shell restoration rather than an unsafe guessed resume.

## Recommended local-first implementation

### Phase 0: ownership and naming boundary

The behaviour-neutral `Workspace` to `Tab`, `HostManager` to `PaneManager`, snapshot
terminology renames, live tab ownership extraction into `TabController`, and the single
default `Space`/`SpaceController` ownership boundary are complete. `App` and `ChromeHost`
resolve tabs through the active Space while the version-1 session format remains
unchanged.

**Exit condition:** existing tabs, panes, session files, focus behaviour, and shell
restore work unchanged; version-1 session tests still pass.

### Phase 1: live Spaces and sidebar

Allow one Draxul process to own multiple live Spaces. Add a left rail with a Spaces
section, Space creation/rename/close/activation, root-directory metadata, and persisted
last-active selection. Existing top-bar tabs belong to the selected Space.

Switching Spaces must leave inactive Space processes running. Closing a Space is a
process-terminating action and should clearly communicate that effect.

**Implemented local UI slice (2026-07-22):** `SpaceController` supplies stable IDs,
metadata mutation, guarded activation, focus handoff, and close semantics. `App` pumps
hosts in inactive tabs and Spaces, observes their deadlines, applies config/font/layout changes to
every Space, and requests close from every host at application exit. A newly created
Space may be empty while its first tab is assembled, but it cannot become active until
that tab exists. Version-1 session snapshots are deliberately refused while multiple
Spaces exist so inactive Spaces cannot be silently discarded. `App` now exposes
create/activate/rename/close transactions, Space roots seed new host working directories,
the command palette exposes the four lifecycle actions, and a clickable left rail appears
when multiple Spaces exist without changing the single-Space layout.

**Remaining in this phase:** define version-2 persistence and restore the last-active
Space.

The shipped sidebar currently reserves its width through `App` arithmetic and paints a
decorative boundary rather than participating in an explicit root layout. The focused
[app-shell layout plan](app-shell-layout.md) makes that boundary a draggable
application-level splitter while deliberately keeping it outside each tab's pane
`SplitTree`.

**Exit condition:** users can switch between multiple local Spaces without terminating
their panes, and restarting Draxul restores the saved topology and last active Space.

### Phase 2: explicit agent targets

Add pane-owned `AgentIdentity`, a derived agent index/projection, and `AgentController`.
Provide an Open Agent action with profiles for Codex, Claude, Cursor, and configurable
commands. Launch the agent directly into a new or selected terminal pane with a known
Space, tab, pane, command, and working directory. Any lookup index must be rebuilt from
pane occupants rather than persisted as a second source of truth.

Because Draxul performs the launch, initial identity is reliable without foreground
process inference. The Agents section can list, focus, rename, and show basic lifecycle
for these known targets.

**Implemented UI shell (2026-07-23):** the root application layout now divides the left
rail into explicit Spaces and Agents regions. A horizontal separator follows the visible
Space list, keeps enough lower-rail room for the `AGENTS` heading at constrained window
heights, and leaves agent rows deliberately empty until the registry exists.

**Exit condition:** a user can start several agents across Spaces and jump directly to
the correct Space, tab, and pane from the sidebar.

### Phase 3: semantic status detection

Add a narrow terminal-observation capability that produces a sanitized live
bottom-buffer snapshot, terminal title, activity time, and available process metadata.
Evaluate data-driven manifests on terminal activity rather than on every render frame.

Implement Herdr-like status authority, conservative blocked detection, the unseen
`done` attention latch, rollup to Space state, and an Explain Agent State diagnostic.
Add best-effort discovery for manually launched agents only after explicit targets are
solid.

**Exit condition:** fixture-driven Codex and Claude screens classify working, blocked,
done/idle, and unknown states with explainable evidence and controlled false-positive
behaviour.

### Phase 4: local harness API

Expose a versioned same-user control endpoint through a named pipe on Windows and a
Unix-domain socket on macOS/Linux. Add matching CLI operations:

```text
draxul space list
draxul space focus <space>
draxul agent list
draxul agent get <agent>
draxul agent start <name> --cwd <path> -- <argv...>
draxul agent focus <agent>
draxul agent read <agent>
draxul agent send <agent> <text>
draxul agent wait <agent> --until <status>
```

Input must be structured rather than interpolated through a shell. Waits should pin
the resolved agent occupant so a replacement process cannot accidentally satisfy an
old wait. The endpoint needs same-user permissions, a per-session authentication
token, stable identifiers, serialized pane input, and event subscriptions.

**Exit condition:** scripts and agents can safely inspect and coordinate local Draxul
agents without UI automation.

### Phase 5: optional native agent integrations

Add opt-in hooks for supported agents that report native conversation references and,
where sufficiently complete, lifecycle events to the local endpoint. Keep session
identity separate from state authority. Resume eligible panes with official agent
commands only when a current integration supplied a valid unique reference.

This phase does not require a background owner, suspend/resume, handoff, SSH, or remote
process control.

**Exit condition:** a normal Draxul restart can rebuild local topology and optionally
resume supported agent conversations without persisting terminal screen contents.

## Risks and guardrails

### Terminology migration risk

A broad mechanical rename can obscure behavioural changes. Land `Workspace` to `Tab`
as a behaviour-neutral step, then add Space semantics separately. Keep versioned
persistence readers and explicit migration tests.

### Main-loop and ownership risk

Terminal state and rendering are main-thread-owned. Detectors should consume immutable
observations and publish results back without directly touching grids or render state
from workers. Domain controllers should not own renderer or window objects.

### Detection drift

Agent TUIs change frequently. Use external manifests, recorded fixtures, rule versioning,
local overrides, conservative fallbacks, and explain output. Never use a semantic
heuristic to send input or approve an operation automatically.

### Windows process ambiguity

ConPTY process trees may reveal descendants without reliably identifying the currently
interactive foreground process. Prefer explicit Draxul launches and hook reports.
Treat Windows process-tree discovery as fallible evidence.

### Persistence and secrets

Do not save screen contents by default. Protect native agent session references and
local endpoint credentials as sensitive state. Avoid placing access tokens or prompt
text in logs, manifests, or diagnostics.

### Misleading liveness

Until Draxul has a background owner, closing the application ends all agent processes.
The UI and documentation must not imply Herdr-style detach/reattach persistence. Space
switching within an open Draxul process can preserve agents; application exit cannot.

## Preliminary decisions

1. Keep `Session` as the outer Draxul namespace.
2. Introduce `Space` as the project/task container within a Session.
3. Keep the adopted `Tab` vocabulary when adding the new Space hierarchy.
4. Keep `Pane`, `Host`, and internal `LeafId` distinct.
5. Model an Agent as an optional pane occupant with separate lifecycle, semantic status,
   status authority, and native conversation identity.
6. Keep all Spaces live while Draxul remains open.
7. Restore topology on restart first; defer daemon ownership, suspend/resume, SSH, and
   remote attachment.
8. Make explicit agent launches reliable before adding heuristic discovery.
9. Add a local API to make Draxul an agent harness rather than only an agent dashboard.
10. Permit an empty Space during construction, but require an active tab before it can
    become the active Space.

## Open design questions

- Should the Agents section default to the active Space or show all agents grouped by
  Space? A global priority view is useful, but active-Space scope is less noisy.
- Should a Space be repository-rooted, arbitrary-directory-rooted, or support both with
  optional repository metadata?
- Should closing a non-empty Space require confirmation, or is a clear count of running
  processes plus an undo/reopen topology record sufficient?
- How should non-terminal product hosts participate in Space persistence without
  blocking shell/agent checkpoints?
- Which semantic detector should be the first supported baseline: Codex, Claude Code,
  or both together with shared fixture infrastructure?

## Sources

Primary Herdr documentation, read 2026-07-21:

- [Herdr overview](https://herdr.dev/)
- [Concepts](https://herdr.dev/docs/concepts/)
- [Agents and status authority](https://herdr.dev/docs/agents/)
- [Agent integrations](https://herdr.dev/docs/integrations/)
- [Session state and restore](https://herdr.dev/docs/session-state/)
- [Socket API](https://herdr.dev/docs/socket-api/)
- [CLI reference](https://herdr.dev/docs/cli-reference/)
- [Configuration reference](https://herdr.dev/docs/config-reference/)
- [Herdr source repository](https://github.com/ogulcancelik/herdr)

Confidence is **high** for the documented Herdr hierarchy, state-authority model,
persistence distinctions, and API capabilities. The Draxul naming and implementation
sections are preliminary design recommendations derived from the current Draxul tree;
they are not claims about Herdr internals beyond the cited public documentation.
