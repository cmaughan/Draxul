# Server/client terminal runtime

**Status:** active  
**Date:** 2026-07-28  
**Implementation branch:** `codex/server-client-runtime`  
**Scope:** persistent local terminal and agent ownership behind one Draxul server,
with one or more GPU UI clients  
**Related:** [Herdr agent-harness research](herdr-agent-harness-research.md),
[multi-Space persistence](multi-space-session-persistence.md), and
[local agent harness](local-agent-harness-implementation.md)

## Goal

Split Draxul into two modes of the same executable:

```text
draxul --server
    one per-user server process
    owns terminal processes, terminal state, Sessions, Spaces, agents, and persistence

draxul
    GPU UI client
    discovers or launches the server
    renders server-owned terminal state and sends commands/input
```

Closing or crashing a UI must not stop its shells. A later UI must be able to attach
to the same running server and recover the current terminal grids, scrollback,
topology, and agent projections.

Two UI instances attached to one server are a first-class development and acceptance
scenario. Mutating shared state in either UI must be reflected in the other without
making ordinary navigation, window geometry, or selection unexpectedly global.

Explicit self-contained hosts such as SatView, MegaCity, ScoreView, Markdown, Kanban,
and initially Neovim remain ordinary client-local applications. They do not acquire
server lifecycle or persistence semantics merely because they are built into the same
executable.

## Decision summary

1. The server owns the shell process, PTY/ConPTY, VT parser, terminal grid,
   scrollback, terminal modes, and agent observation.
2. The UI does not replay raw PTY history. It receives versioned semantic terminal
   snapshots and deltas.
3. `IHost` remains a UI abstraction. Server libraries must not depend on `IHost`,
   SDL, ImGui, a renderer, a window, fonts, or GPU cell types.
4. The server becomes the single writer of Session snapshots. UI clients send
   mutations and consume authoritative events.
5. Shared topology and terminal contents are server state. Client route, selection,
   viewport, font, window, clipboard, and other presentation choices are client state.
6. One client at a time controls input and resize for a terminal in the first
   multi-client implementation. Other clients observe and may explicitly take over.
7. A shared pane descriptor records whether its runtime is `server_terminal` or
   `client_local`. Neovim remains client-local even when its pane participates in a
   shared split tree.
8. Local IPC is implemented first, behind an experimental path until it is proven.
   The protocol and data model must nevertheless remain suitable for a future SSH
   transport.
9. The server kernel may expose an optional Windows notification-area or macOS
   menu-bar status surface. It must still run without a graphical login session.
10. A server crash is initially a process-loss boundary. UI reconnect preserves live
   shells; a cold server restart restores topology and relaunches according to saved
   policy but cannot preserve the old PTY process.

## Success criteria

The project is ready to make server mode the default when all of these are true:

- starting two ordinary Draxul UIs produces one server process;
- a shell launched from either UI is a child of the server, not the UI;
- both UIs see the same ordered Spaces, tabs, split topology, pane names, terminal
  output, shell lifecycle, and agent state;
- a topology mutation in one UI is applied once and appears in the other;
- each UI can navigate Spaces and tabs independently;
- terminal input and resize ownership is visible and deterministic;
- closing either UI leaves the server, shells, and other UI functioning;
- reconnecting sends a bounded current snapshot followed by ordered deltas;
- slow, disconnected, or incompatible clients cannot stall PTY draining or corrupt
  server state;
- server persistence restores every saved Session and Space transactionally;
- Windows and macOS use the same protocol and ownership model;
- standalone product hosts and Neovim retain their current local behavior; and
- the legacy local terminal path remains available as a temporary recovery mechanism
  until the new path has completed a confidence period.

## Non-goals for the local-first implementation

- preserving live shells across a server process crash or operating-system restart;
- accepting unauthenticated network connections;
- exposing a public TCP port;
- simultaneous character-level input from multiple writers;
- merging conflicting terminal sizes from multiple clients;
- server-owning Neovim, SatView, MegaCity, ScoreView, Markdown, or Kanban;
- moving GPU application chrome rendering into the server;
- persisting terminal screen contents across a cold server restart;
- SSH file synchronization, remote desktop, remote clipboard history, or remote
  product-host rendering; and
- automatic binary upgrade or server handoff while live shells exist.

These are not rejected permanently. The boundaries below ensure that SSH transport,
native encrypted transport, Neovim server ownership, and process handoff can be added
without replacing the local protocol or Session model.

## Why raw PTY relay is not sufficient

The server cannot own only the child process and forward PTY bytes to an attached UI.
While every UI is disconnected, a shell may:

- enter or leave the alternate screen;
- resize and redraw;
- clear or overwrite content;
- change cursor, keyboard, mouse, hyperlink, title, or synchronized-output modes; and
- produce scrollback that exceeds a pipe buffer.

A new client cannot infer the current terminal state from only subsequent output.
Replaying all bytes from process launch would require an unbounded event log containing
every output chunk and resize in exact order, and reconnect time would grow with
terminal age.

The server therefore maintains the authoritative emulated terminal. The UI receives a
current semantic representation and never needs the process's historical byte stream.

## Target ownership

| Concern | Authoritative owner | Notes |
|---|---|---|
| Server instance and client registry | Server | One per user and runtime namespace |
| Session, Space, tab, and pane descriptors | Server | Stable shared topology and execution domain |
| Split tree and pane ratios | Server | Mutations serialized and broadcast |
| Shell process and exit status | Server | PTY/ConPTY never belongs to UI |
| VT parser, modes, grid, and scrollback | Server | Renderer-free terminal core |
| Terminal title and current working directory | Server | Derived from process evidence |
| Agent discovery, status, hooks, and native reference | Server | Agent remains a pane occupant |
| Session checkpoint writes | Server | One writer; existing safe replacement retained |
| Window, renderer, fonts, palette, and DPI | Client | Never serialized over the wire |
| Active viewed Space/tab/pane | Client | Independent navigation by default |
| Selection, copy-mode cursor, and scroll viewport | Client | Scrollback content comes from server |
| Clipboard, URL opening, notifications, and tray actions | Client/status surface | Capability-gated |
| Neovim and self-contained host runtimes | Client | One runtime per client; existing lifecycle retained |

### Shared state versus per-client state

The two-client test must not accidentally make every UI action global.

Shared and reflected:

- creation, close, rename, reorder, and root directory of Spaces;
- creation, close, rename, reorder, and split topology of tabs and panes;
- split ratios and terminal dimensions chosen by the current controller;
- pane host descriptors and whether their runtime is server-owned or client-local;
- terminal contents, cursor, title, modes, scrollback, process lifecycle, and current
  working directory;
- agent identities, lifecycle, semantic status, attention, and server-side operations;
- saved Session identity and durable restore policy.

Per client:

- which Session, Space, tab, and pane the window is currently viewing;
- application window size, location, display, DPI, font size, and palette;
- selection, hover, copy-mode cursor, local scroll position, and command palette;
- diagnostics-panel visibility and transient toasts;
- sidebar width and other UI preferences unless deliberately promoted later.

Client-local host runtime state is neither category of shared runtime state. The pane
descriptor and its position are shared, but each client independently instantiates,
renders, and stops its own Neovim or product host for that pane. Its buffers, RPC
state, camera, playback, and other live content are not reflected to other clients.

The server retains a durable default route for a future fresh client, but an attached
client changing its viewed Space does not pull every other client to that Space.

## Target component graph

```text
draxul executable
|- early command-line dispatch
|  |- ServerMain
|  |- ClientMain
|  `- StandaloneHostMain
|
|- draxul-server
|  |- draxul-session-model
|  |- draxul-terminal-runtime
|  |- draxul-agent
|  |- draxul-control
|  `- draxul-transport
|
|- draxul-client
|  |- draxul-protocol
|  |- draxul-transport
|  `- RemoteTerminalHost adapter
|
`- draxul-app
   |- renderer/window/font/gui libraries
   |- draxul-client
   `- local product-host registration
```

### Proposed libraries

#### `draxul-protocol`

Neutral values shared by server and client:

- opaque identifiers;
- protocol version and capability values;
- request, response, command, event, snapshot, and delta structures;
- terminal cells, cursor, modes, title, and scrollback page values;
- topology and agent projection values;
- framing limits and validation; and
- exhaustive enum/string handling where behavior branches.

It has no transport, filesystem, process, renderer, or application dependency.

#### `draxul-transport`

- named-pipe connection/listener on Windows;
- Unix-domain connection/listener on macOS;
- bounded binary framing;
- connection authentication;
- endpoint discovery and stale-endpoint handling;
- cancellable reader/writer loops; and
- a transport interface that a future SSH stdio connection can implement.

#### `draxul-terminal-core`

A renderer- and process-free terminal state machine extracted from the current
terminal host:

- VT parsing and state;
- main and alternate grids;
- attribute and hyperlink tables;
- cursor and synchronized-output presentation state;
- scrollback and shell marks;
- resize behavior;
- terminal title, current working directory, mouse, keyboard, focus, and clipboard
  modes;
- bounded observations used by agent detection; and
- semantic snapshots and dirty-state extraction.

The existing local terminal path and the new server runtime both compose this same
core during migration.

#### `draxul-terminal-runtime`

- ConPTY and Unix PTY process adapters;
- launch validation and environment;
- output draining into `TerminalCore`;
- input, resize, close, kill, and exit handling;
- terminal ID and runtime generation;
- detached lifecycle; and
- server-facing immutable observations.

It has no window or renderer dependency.

#### `draxul-session-model`

- Session, Space, tab, pane, and split-tree values;
- a pane execution domain (`server_terminal` or `client_local`);
- a durable host descriptor separate from either kind of live runtime;
- stable ID allocation;
- pure mutation functions;
- snapshot validation and codec;
- transactional restore policy; and
- authoritative topology revision.

The reusable snapshot model moves out of `app/`. The first move need not change the
existing version-3 TOML format.

#### `draxul-server`

- one authoritative state/event loop;
- Session and terminal registries;
- client subscriptions and controller leases;
- persistence scheduling;
- agent controller and control-request routing;
- tray/menu-bar status projection;
- logging and diagnostics; and
- graceful shutdown.

#### `draxul-client`

- discovery, auto-launch, handshake, attach, reconnect, and resync;
- immutable local projection cache;
- topology command submission;
- terminal input/resize ownership;
- mapping semantic cells into the current grid renderer; and
- `RemoteTerminalHost`, which satisfies the UI-facing host contract without owning a
  process or VT parser.

### Dependency rules

These rules are architectural tests, not suggestions:

1. `draxul-server` must not link `draxul-app`, renderer, window, font, GUI, or product
   modules.
2. `draxul-protocol` must not include platform IPC, filesystem, process, `IHost`, SDL,
   GLM, or GPU types.
3. Server terminal state must not contain `GpuCell`, glyph, atlas, pixel, DPI, or
   renderer handles.
4. Client code must not mutate server-authoritative models optimistically. It may
   display pending state, but commits only events accepted by the server.
5. PTY readers and client I/O threads enqueue bounded messages. Only the server state
   thread mutates topology, terminal cores, persistence state, or agents.
6. Only the client main thread mutates UI projection objects consumed by the renderer.
7. Standalone product hosts must not acquire a dependency on `draxul-server`.

## Mixed terminal and client-local panes

Server-owned topology must not imply that every pane runtime belongs to the server.
Represent the distinction explicitly:

```text
PaneDescriptor
|- PaneId
|- name and durable launch descriptor
|- execution_domain
|  |- server_terminal -> TerminalId and server runtime
|  `- client_local    -> each UI creates its own local IHost
`- optional agent identity only when valid for that host
```

For a `server_terminal` pane:

- every client renders the same terminal state;
- the server owns lifecycle, dimensions, scrollback, and agents; and
- disconnecting all clients changes nothing.

For a `client_local` pane:

- every client sees the same pane in the same shared split tree;
- each capable client creates an independent local runtime from the descriptor;
- closing one client closes only that client's runtime;
- a client without the compiled host provider shows an unavailable placeholder rather
  than corrupting or removing shared topology; and
- runtime contents are neither streamed nor claimed to match between clients.

This accommodates Neovim without prematurely serverizing msgpack-RPC. It also makes
optional product-module builds honest: topology can describe a host that one client
does not contain.

The first remote-terminal slices may restrict experimental remote Sessions to
`server_terminal` panes. Hybrid descriptors must land before server-owned topology
becomes the normal Draxul path.

Add link-isolation tests for the public headers and keep the CMake graph in
`docs/module-map.md` current as libraries land.

## Executable modes

| Invocation | Behavior |
|---|---|
| `draxul --server` | Run the server kernel; show a status surface when available |
| `draxul` | Discover or launch the server, then run a GPU UI client |
| `draxul --no-server` | Temporary legacy/recovery path using local terminals |
| `draxul --host satview` and other standalone products | Existing local single-process path |
| `draxul --server-status` | Query server status without launching a UI |
| `draxul --shutdown-server` | Request graceful server shutdown |
| future `draxul --server-bridge` | Bridge framed protocol over stdin/stdout for SSH |

Exact user-facing spelling should be settled when each command ships, but mode parsing
must occur before SDL, renderer, or product-host initialization.

### Discovery and singleton startup

Use one per-user endpoint namespace, separate from the current per-Session control
endpoint:

1. A client reads owner-restricted runtime metadata.
2. It attempts a real handshake with the global server endpoint.
3. If no compatible server answers, it starts the current executable with `--server`.
4. It waits for a successful handshake with a bounded timeout.
5. Concurrent launchers may all attempt startup, but only one server may bind the
   endpoint. Losing processes recognize the live winner and exit successfully.
6. A stale metadata file or stale Unix socket is removed only after proving that no
   owner server is listening.

The metadata records endpoint, server PID for diagnostics only, server instance epoch,
protocol version, build version, and authentication-token location. A PID is never
treated as proof that the expected server owns the endpoint.

On Windows the spawned process must be hidden and independent of the client's console
or process job lifetime. On macOS it must survive client exit and integrate correctly
with an application bundle or login-session launch.

### Server lifetime

- The server remains alive after the final UI disconnects while it owns Sessions or
  terminal runtimes.
- UI Quit means detach.
- Server Quit is explicit and warns if live shells exist.
- Graceful shutdown stops new mutations, notifies clients, checkpoints durable state,
  requests child termination, applies a bounded escalation policy, removes owned
  endpoints, and exits.
- A server launched without a graphical login continues without a tray/menu-bar
  surface.
- Idle auto-exit may be considered only after live detach/reattach is mature.

## Optional server status surface

The server kernel publishes a neutral `ServerStatusSnapshot`; platform surfaces render
it but do not own server state.

Initial status values:

- server build, protocol, PID, uptime, and endpoint;
- connected client count;
- Session, Space, terminal, and agent counts;
- live and exited shell counts;
- last checkpoint result;
- compatibility or degraded-state warning; and
- log location.

Initial menu:

- Open Draxul;
- show server status;
- open diagnostics or log;
- list connected clients and running Sessions;
- one guarded Stop Server action, with Force Stop offered only if graceful
  shutdown fails.

Windows uses a notification-area icon backed by a minimal hidden message window.
macOS uses a menu-bar status item. This surface is optional: `--no-tray`, an SSH
session, or the absence of a graphical desktop must not prevent server startup.

## Protocol design

### Logical channels

Use two logical channels so terminal traffic cannot starve administrative requests:

1. **Control channel**
   - builds on the current authenticated JSON request/response control plane;
   - discovery, status, Session and agent commands, diagnostics, and server shutdown;
   - human-readable CLI errors and structured JSON output.

2. **Client stream**
   - length-prefixed binary frames;
   - handshake, subscriptions, topology commands/events, terminal snapshots/deltas,
     input, resize, scrollback, capabilities, and resync;
   - bounded independent reader/writer queues.

They may use separate local endpoints while sharing authentication and server
identity. A future SSH bridge multiplexes both over one framed stdio connection.

### Handshake

Client hello:

- protocol major/minor and build version;
- client-generated ID and command-retry namespace;
- supported encodings, compression, clipboard, URL, notification, and image
  capabilities;
- requested Session and attach mode;
- client type (`gpu_ui`, `cli`, `probe`, or future `ssh_bridge`);
- authentication proof; and
- bounded initial terminal dimensions where relevant.

Server welcome:

- accepted protocol and capabilities;
- server instance epoch and build version;
- assigned connection ID;
- current Session/topology revision;
- frame and queue limits;
- current controller lease information; and
- actionable incompatibility error when rejected.

An old server with live terminals is never killed automatically because a newer client
cannot speak its protocol.

### Identity and ordering

Use opaque values rather than vector positions or process IDs:

- `ServerEpoch`
- `ClientId`
- `ConnectionId`
- `SessionId`
- `SpaceId`
- `TabId`
- `PaneId`
- `TerminalId`
- `RuntimeGeneration`
- `TopologyRevision`
- `TerminalSequence`
- `CommandId`

`ServerEpoch + TerminalId + RuntimeGeneration + TerminalSequence` determines whether a
terminal delta can be applied. A different epoch or generation requires a new
snapshot.

Every topology command contains the revision it was based on and a stable `CommandId`.
The server serializes commands, applies each ID at most once, and broadcasts the
authoritative resulting event and new revision. Stale but commutative commands may be
rebased deliberately; destructive conflicts return a structured rejection and current
revision.

### Terminal stream

Client to server:

- attach/detach terminal;
- acquire/release/take over controller lease;
- input bytes or structured input event;
- resize in columns and rows;
- focus state;
- scrollback range request;
- snapshot/resync request;
- clipboard response; and
- acknowledgement/last applied sequence.

Server to client:

- terminal metadata and lifecycle;
- complete grid snapshot;
- dirty row/cell delta;
- scrollback page;
- title, current working directory, cursor, modes, bell, and clipboard request;
- controller lease change;
- agent projection event;
- topology snapshot/event;
- resync required; and
- server shutdown/error.

Wire cells contain terminal semantics, not renderer structures:

- grapheme cluster and cell width;
- foreground/background color;
- style flags;
- hyperlink table reference;
- continuation/skip information; and
- optional terminal metadata that affects hit-testing.

Start the real-shell slice with complete snapshots if necessary. Before remote mode is
the default, implement dirty-region deltas, coalescing, compression negotiation, and
snapshot fallback.

### Backpressure

PTY output must always be drained even when no client can keep up.

- Terminal state advances on the server independently of subscriptions.
- Each client has a bounded high-priority control queue and a bounded/coalescing render
  queue.
- A newer delta may replace unsent deltas only by marking that terminal for a complete
  snapshot.
- A slow client is never allowed to block the server state loop or a PTY reader.
- Oversized frames, strings, arrays, grids, scrollback requests, and input are rejected
  before allocation where possible.
- Metrics record queue saturation, dropped/coalesced deltas, snapshot frequency, frame
  sizes, and resyncs without logging terminal contents.

## Multi-client behavior

Multiple UI connections are useful immediately because they expose ownership mistakes
that a reconnect-only test can hide.

### Topology

Any attached UI may submit an allowed topology mutation. The server applies it on the
state thread and publishes one event to every subscribed client, including the
originator. The originator does not directly commit an optimistic model mutation.

Examples:

- create a Space in client A: A and B receive the same new Space ID and order;
- rename a tab in client B: both clients receive the same name and topology revision;
- split a terminal pane in A: the server creates the pane and shell, then both clients
  receive the same split node and terminal IDs;
- close a pane in B: the server applies the close once, and both projections remove
  it.

### Navigation

Client A may view Space 1 while client B views Space 2. Focus and selection are
therefore routes attached to a client, not one global active route. Agent navigation
changes only the requesting client unless an explicit collaborative-follow feature is
added later.

The server may update a durable default route after accepted navigation or lifecycle
events, but this does not become a broadcast command to attached clients.

### Terminal controller lease

The first attached and focused client may acquire the terminal's input/resize lease.
Only the lease owner can:

- send terminal input;
- determine PTY columns and rows;
- send mouse-reporting input; and
- answer interactive clipboard requests.

Observers render the same terminal and see a small read-only/controller indicator.
They may request takeover. For the local first version, takeover is immediate and
broadcast; a later remote policy may prompt the current controller or use an idle
timeout.

When the controller disconnects:

- the terminal retains its last size;
- the lease becomes vacant;
- another client may acquire it; and
- the shell continues to run.

This avoids two windows continuously resizing the same PTY and makes two-client tests
deterministic.

## Threading model

### Server

One state thread owns:

- Session topology;
- terminal-core mutation;
- controller leases;
- agent runtime state;
- persistence dirtiness and immutable snapshot capture; and
- event sequence allocation.

Supporting threads:

- listener/reader tasks validate and enqueue client messages;
- per-client writers consume bounded queues;
- PTY readers enqueue bounded output chunks;
- persistence workers serialize and atomically replace files from immutable values;
- optional status-surface thread or platform main-loop integration sends commands
  back to the state thread.

No worker mutates live topology, a terminal grid, an agent controller, or renderer
state directly.

### Client

- The IPC reader validates frames and builds immutable update batches.
- The UI main thread applies batches to the client projection and requests a frame.
- `RemoteTerminalHost` reads only main-thread-owned projection state while drawing.
- Client writes are serialized and bounded; disconnect cancels outstanding operations
  without blocking UI shutdown.

Run macOS TSan against the client/server integration path once both sides are active.

## Persistence and restore

The server is the only writer of Session files.

1. Load and validate all existing version-3 Session snapshots before launching shells.
2. Build a complete candidate topology transactionally.
3. Launch server-supported shell panes after the candidate topology is valid.
4. Keep client-local and unsupported host kinds on their existing standalone path;
   after hybrid descriptors land, preserve them as `client_local` panes and do not
   silently convert or discard them.
5. Capture immutable all-Session snapshots from the server state thread.
6. Retain the existing bounded validation, debounced save, dirty-follow-up, temporary
   file, and Windows-safe atomic replacement behavior.
7. Publish checkpoint success/failure in server status and diagnostics.

Two restore cases remain distinct:

| Event | Terminal process | Terminal screen | Agent conversation |
|---|---|---|---|
| UI disconnect/reconnect | Same process | Same live server state | Same process/conversation |
| Server cold restart | New process | Fresh terminal state | Resumed only when saved native policy succeeds |

Do not persist terminal grids merely to make cold restart look like live reconnect.
Screen-history persistence can be a separate, bounded format later.

The first server migration should read the current snapshot path and schema without
rewriting it at startup. Only bump the snapshot version if durable fields actually
change. Preserve a tested `--no-server` reader during the migration period.

## Agent ownership after migration

Move these responsibilities to the server after terminal and topology ownership is
proven:

- foreground/descendant process discovery;
- terminal observation and manifest evaluation;
- hook event routing and authority;
- lifecycle, semantic status, attention, and runtime generation;
- registered agent launch and restart;
- native session references and restore plans;
- control API focus/restart/send/wait operations; and
- the derived all-Space Agents projection.

Agent identity remains attached to a pane occupant. The Agents sidebar remains a
projection, now received from the server rather than recomputed from client-local
hosts. Terminal contents and detection evidence are still excluded from persistence
and logs.

## Remote-safe extension point

Local named pipes and Unix sockets are the first transport, but protocol structures
must not assume:

- a shared filesystem or path syntax;
- meaningful remote PIDs or handles;
- matching fonts, palettes, DPI, cell pixels, clocks, or locale;
- server access to the client clipboard, browser, desktop, or notification service;
- negligible latency; or
- unlimited bandwidth.

The preferred first remote transport is:

```text
local Draxul UI
    -> ssh remote-host draxul --server-bridge
    -> framed protocol over SSH stdin/stdout
    -> remote machine's local Draxul server endpoint
```

SSH supplies encryption, authentication, host verification, and access policy. The
bridge performs no terminal emulation and owns no Session state. Native TLS listening
can be considered later if there is a concrete need.

Remote readiness requires:

- negotiated compression;
- snapshot plus ordered-delta resync;
- paged scrollback;
- explicit client capabilities;
- server-side paths represented as opaque/display values;
- latency-tolerant controller leases; and
- no raw OS handles or renderer types in the wire schema.

## Vertical implementation slices

Each slice keeps a working application, has a visible demonstration, and has an
automated exit gate. Do not begin a later ownership migration merely because the
previous code compiles.

### Slice 0: contracts and characterization

**Status:** complete (2026-07-28)

**Outcome:** current behavior is pinned down before extraction.

Implementation record:

- added opaque `TerminalId`, `TerminalRuntimeGeneration`, and `TerminalSequence`
  values plus explicit terminal/frame/input/string/restore limits;
- added renderer-neutral full and dirty semantic snapshots with a stable semantic
  digest;
- locked replay behavior across PTY chunk boundaries, Unicode/wide cells, SGR,
  OSC 7/8/52, hyperlinks, mouse/application/bracketed-paste modes, shell marks,
  alternate screen, and resize;
- retained the existing raw-PTY capture/replay, scrollback, cursor/synchronized
  output, local lifecycle/exit, selection/copy-mode, and agent discovery coverage;
  and
- left production Session persistence and visible behavior unchanged.

Work:

- record the ownership and dependency rules above in code-facing architecture notes;
- add stable opaque ID and terminal semantic snapshot value tests;
- add deterministic raw-PTY replay fixtures covering ordinary output, Unicode,
  combining/wide cells, alternate screen, resize, scrollback, OSC 7/8/52, mouse and
  keyboard modes, cursor shapes, synchronized output, and shell marks;
- calculate a semantic grid digest independent of renderer/GPU representation;
- characterize local terminal lifecycle, process exit, selection, copy mode, agent
  observations, and snapshot capture;
- add expected process-tree tests using deterministic shell helper executables; and
- define frame, string, collection, queue, and restore limits before a decoder exists.

Demonstration:

- replay the same fixture through the current local host and print a stable digest and
  selected semantic state.

Automated exit gate:

- all current tests pass;
- every new replay fixture is deterministic on repeated runs;
- no production behavior or snapshot format changes.

Rollback:

- tests and neutral value types can remain even if extraction is delayed.

### Slice 1: renderer-free terminal core in the local application

**Status:** complete (2026-07-28)

**Outcome:** the current local shell uses `TerminalCore` by composition, with no IPC.

Implementation record:

- `draxul-terminal-core` now owns VT parsing/state, grid mutation, attributes,
  hyperlinks, alternate-screen behavior, modes, metadata, semantic snapshots, and
  reusable scrollback storage;
- `TerminalHostBase` is now a composition adapter over `TerminalCore`; it retains
  process lifecycle, window/clipboard integration, renderer publication, and input
  routing, while `LocalTerminalHost` retains selection, copy mode, mouse projection,
  agent observation, and the client scrollback viewport;
- both existing ConPTY and Unix PTY hosts continue through that same adapter;
- CMake dependency checks and a standalone public-header/link test enforce a core
  boundary with no window, renderer, SDL, `IHost`, or process dependency; and
- the semantic digests, full test inventory, app smoke, and terminal render checks
  pass unchanged.

Work:

- extract VT/grid/scrollback/title/mode behavior from `TerminalHostBase` and
  `LocalTerminalHost`;
- define a small process adapter interface consumed outside the core;
- keep selection, client viewport, clipboard access, URL opening, and drawing in the
  local host adapter;
- make both ConPTY and Unix PTY paths feed the same core;
- expose complete semantic snapshot and dirty-state APIs; and
- add link-isolation tests proving the core has no UI/platform-render dependencies.

Demonstration:

- run PowerShell on Windows and Zsh/Bash on macOS through the extracted core with no
  visible behavioral change.

Automated exit gate:

- replay digests exactly match Slice 0;
- terminal, input, process, and render tests pass;
- app build and smoke pass;
- relevant render snapshots are unchanged.

Rollback:

- the composed local adapter remains the production path and can be retained as
  `--no-server`.

### Slice 2: real singleton server bootstrap, no remote shells

**Status:** complete (2026-07-28)

**Work item:** [11 singleton-server-bootstrap -feature.md](../kanban/done/11%20singleton-server-bootstrap%20-feature.md)

**Outcome:** `draxul --server` and client discovery work end to end while terminals
remain local.

Implementation record:

- added renderer-neutral `draxul-protocol`, `draxul-client`, and `draxul-server`
  targets with configure-time dependency guards against UI, renderer, host, Neovim,
  SDL, and product modules;
- added early `--server`, `--server-status`, and `--shutdown-server` dispatch before
  logging/host registration/application initialization, plus isolated
  `--server-runtime-dir` namespaces;
- reused the owner-restricted authenticated control transport for hello/welcome,
  capability/version negotiation, status, readiness, client registration, and
  graceful shutdown;
- added detached launch and concurrent-start arbitration behind
  `--experimental-server-client`; ordinary startup, explicit hosts, smoke, and render
  paths remain local and do not auto-start a server;
- diagnostics report the server PID, epoch, build, and protocol while explicitly
  labelling terminals as local; and
- process coverage proves ten concurrent clients converge on one server epoch,
  alongside absent, starting, ready, stale, busy, incompatible, crashed, isolated
  namespace, and graceful-stop coverage.

Work:

- add early executable mode dispatch;
- add the server kernel/event loop without SDL or renderer initialization;
- add per-user endpoint metadata, authentication, hello/welcome, status, logging,
  readiness, and graceful stop;
- add detached auto-launch and concurrent-start arbitration;
- add protocol/version/capability negotiation;
- add isolated runtime-directory overrides for tests; and
- add a client connection indicator to diagnostics, not yet the final tray.

Demonstration:

- launch two Draxul UIs and show that both report the same server PID/epoch and that
  only one server exists; terminals are still clearly marked local.

Automated exit gate:

- process integration tests cover absent, starting, ready, stale, busy, incompatible,
  crashed, and graceful-stop states;
- 10 concurrent launch probes result in one listener/server epoch;
- server mode does not initialize SDL, a renderer, Neovim, or product modules;
- standalone `--host` smoke proves it does not start the server.

Rollback:

- server auto-connect remains behind `--experimental-server-client`; ordinary startup
  can continue directly to the local app.

### Slice 3: deterministic fake remote terminal with two clients

**Status:** complete (2026-07-28)

**Work item:** [11 fake-remote-terminal-two-client -feature.md](../kanban/done/11%20fake-remote-terminal-two-client%20-feature.md)

**Outcome:** the complete client/server/render boundary works before a real shell is
entrusted to it.

Implementation record:

- the headless server owns one deterministic `TerminalCore` and pane descriptor;
- renderer-neutral protocol values encode complete semantic snapshots, dirty deltas,
  controller changes, server epoch, terminal generation, and sequence;
- the shared client projection rejects stale identity/version/order and supplies both
  headless probes and `RemoteTerminalHost`;
- per-client event queues are bounded at 32 entries and saturate into a current
  snapshot without blocking the controller;
- a background UI adapter polls off the render thread, coalesces immutable projection
  snapshots, wakes the window, and mutates grid/GPU-facing state only in `pump()`;
- `--experimental-remote-terminal` starts or discovers the singleton and disables
  local Session restore for that window; and
- `take_terminal_control` is available from the command palette for explicit
  observer takeover.

Work:

- implement `RemoteTerminalHost`;
- add a deterministic server terminal that echoes input and produces scripted output;
- implement attach, complete snapshot, input, resize, cursor/title, disconnect, and
  resync;
- implement client subscriptions and bounded writer queues;
- implement one controller lease and one observer;
- expose a test/probe client using the same client library without requiring a GPU;
- add topology snapshot/event scaffolding sufficient to identify the fake pane; and
- wake both UI windows when shared output changes.

Demonstration:

1. Open clients A and B against one server.
2. Both display the same fake terminal.
3. Type in A; both display the echoed text.
4. B is read-only until it takes over.
5. Resize B after takeover; both show the resulting dimensions.
6. Close A, produce more output, reopen A, and recover the current snapshot.

Automated exit gate:

- two probe clients converge on identical semantic grid digests;
- reconnect after missed updates receives one valid snapshot and then ordered deltas;
- stale epoch/generation/sequence data is rejected;
- slow observer saturation does not delay the controller or server state loop;
- ownership and takeover events are identical in both projections.

Rollback:

- fake host and protocol tests remain useful; no real shell behavior has moved.

### Slice 4: one real server-owned shell behind an opt-in flag

**Status:** complete (2026-07-28)

**Work item:** [11 server-owned-shell-runtime -feature.md](../kanban/done/11%20server-owned-shell-runtime%20-feature.md)

**Outcome:** a single shell pane survives UI detach and reconnect.

Implementation record:

- PTY/ConPTY ownership moved into the UI-free `draxul-terminal-process` target,
  shared by local hosts and the server runtime.
- `--experimental-remote-shell` selects a real `terminal.*` endpoint while the
  existing fake endpoint and local terminal defaults remain unchanged.
- The server lazily owns the shell process, `TerminalCore`, controller lease,
  event queues, process ID, terminal generation, and zero-client output draining.
- A generation-resync snapshot lets attached projections recover after process
  restart without changing the server epoch.
- Windows integration coverage proves server parentage, shared input/resize,
  zero-client delayed output, same-PID reconnect, and a new generation after the
  shell exits and is recreated.
- Two actual Release UI processes attached to one isolated server, exited, and a
  third UI reattached while the same server epoch and real terminal remained live.
- Slice 4 preserves a bounded current screen; server-owned scrollback remains a
  deliberate Slice 5 addition.

#### Manual retest: two UIs, detach, and reconnect

Use an isolated runtime so this test does not touch the normal Draxul server. Run
these setup commands from the repository root:

```powershell
py do.py build release
$exe = (Resolve-Path .\build-ninja-release\draxul.exe).Path
$runtime = Join-Path $env:TEMP "draxul-slice4-manual"
& $exe --shutdown-server --server-runtime-dir $runtime 2>$null
```

The initial shutdown can report that no server exists; that is harmless. Keep the
same `$exe` and `$runtime` values in each PowerShell window below.

1. Start client A:

   ```powershell
   & $exe --experimental-remote-shell --server-runtime-dir $runtime
   ```

2. Start client B from a second PowerShell window with the same command. Both Draxul
   windows should show the same PowerShell screen. Client A initially owns input.

3. In the shared shell, run:

   ```powershell
   $PID
   Write-Output "__DRAXUL_SHARED__"
   ```

   Record `$PID`; the command and output should appear in both Draxul windows. In
   client B, run `take_terminal_control` from the command palette, then type another
   command to verify that control moved without closing client A.

4. In whichever client owns control, start delayed output:

   ```powershell
   Start-Sleep -Seconds 3; Write-Output "__DRAXUL_DETACHED__"
   ```

   Immediately close both Draxul windows, wait at least five seconds, then start a
   third client with the same command from step 1.

5. The reopened UI should already contain `__DRAXUL_DETACHED__`. Run `$PID` again;
   it should match the value recorded before both UIs closed. Keep this output short:
   Slice 4 preserves the current terminal screen, while server-owned scrollback is a
   Slice 5 feature.

6. Optional status check from another PowerShell window:

   ```powershell
   & $exe --server-status --server-runtime-dir $runtime --json
   ```

   Expect `"state":"ready"` and `"terminals":2` (the always-present fake diagnostic
   terminal plus the real shell). `connected_clients` is cumulative registration
   data in this slice, so it is not a live attached-window count.

7. Shut down the isolated server when finished; this also ends its shell:

   ```powershell
   & $exe --shutdown-server --server-runtime-dir $runtime
   ```

For an exact automated check of process parentage, PID/generation continuity,
zero-client output draining, resize propagation, and shell recreation, run:

```powershell
cmake --build build --config Release --target draxul-test-core
.\build\tests\Release\draxul-test-core.exe "[server][remote-terminal][process]"
```

Work:

- compose `TerminalRuntime` from the extracted core and platform process adapter;
- let the server create one PowerShell test shell on Windows and one Bash/Zsh test
  shell on macOS;
- stream complete snapshots first, plus lifecycle and title/cwd/mode changes;
- route controller input and resize to the server process;
- keep PTY draining and terminal emulation active with zero clients;
- preserve runtime generation across client reconnect but change it on process restart;
- route clipboard/URL/notification requests through client capabilities; and
- retain local terminal creation as the default path.

Demonstration:

1. Start a shell from client A and record its PID/terminal ID.
2. Attach client B and observe the same shell.
3. Run a delayed-output command.
4. Close both UIs before output arrives.
5. Reopen a UI and see the output with the same shell PID and terminal generation.

Automated exit gate:

- process parentage proves the shell belongs to the server;
- killing a UI does not end the shell;
- killing/restarting the shell changes only its runtime generation;
- detached output and the bounded current screen remain available;
- Windows and macOS process/PTY integration tests pass.

Rollback:

- remote shell creation requires an explicit experimental flag; existing local shell
  creation remains unchanged.

### Slice 5: terminal parity, deltas, scrollback, and flow control

**Outcome:** every supported shell behavior is practical through the remote path.

**Implementation status (2026-07-29):** implemented on
`codex/server-client-runtime`; the experimental path remains the rollback boundary.
The local transport advertises an explicit uncompressed fallback rather than paying
compression CPU/latency before measurements justify it.

**Work item:** [11 remote-terminal-parity-scrollback -feature.md](../kanban/pending/11%20remote-terminal-parity-scrollback%20-feature.md)

Work:

- add dirty row/cell deltas with sequence numbers and snapshot fallback;
- add coalescing and optional compression negotiation;
- add paged scrollback and client-local viewport/selection;
- finish cursor, alternate-screen, mouse, keyboard, focus, hyperlink, shell-mark,
  synchronized-output, clipboard, notification, title, and working-directory paths;
- support PowerShell, Bash, Zsh, and WSL host kinds;
- make config ownership explicit: server consumes launch/process/terminal limits while
  client consumes fonts/palette/render preferences;
- add metrics for frame sizes, delta ratio, queue pressure, and reconnect time; and
- retain sanitized tracing with no terminal content.

Demonstration:

- run interactive full-screen applications and agent CLIs in two clients, transfer
  control, scroll independently, detach, and reconnect without redraw corruption.

Automated exit gate:

- local and remote replay fixtures produce the same semantic digest;
- randomized snapshot/delta application converges or requests resync;
- slow-client and high-output stress remain within explicit memory/latency limits;
- alternate-screen resize, Unicode, scrollback, and synchronized-output tests pass;
- render snapshots cover a remote terminal scenario.

Rollback:

- the complete-snapshot remote mode remains available for diagnosis; local mode remains
  available until Slice 9.

Implementation notes:

- `ScrollbackBuffer` remains the one renderer-neutral history store. The real server
  runtime captures rows once; `terminal.scrollback` returns bounded semantic pages.
- `RemoteTerminalHost` composes a history page with the live screen without mutating
  the server projection. Scroll offset, selection, copy/paste confirmation, pointer
  state, and renderer palette remain client-local.
- Keyboard copy mode and Shift+PageUp/PageDown/Home/End now operate on that local
  projection for controllers and observers alike. Observer text/paste produces one
  take-control hint; a stale history page from an earlier column count returns to live.
- Remote mode now forwards keyboard, focus, DEC mouse modes, bracketed paste, OSC 8
  links, controller-directed OSC 52 writes, cursor/mode/title/cwd/shell-mark state,
  and withholds synchronized-output deltas until the batch ends.
- First server launch owns shell kind/command/arguments/environment/cwd/history limits;
  the controller continues to own terminal dimensions. UI fonts, palette, selection,
  and rendering options never enter the server library.
- Server input admission is non-blocking: each terminal has a bounded writer queue,
  PTY/ConPTY writes run on its own writer thread, and output readers pause at a bounded
  byte cap. Input rejection is a visible drop rather than a worker exit, unexpected
  poll failures reattach with bounded backoff, and only authoritative terminal removal
  ends a remote host.
- `terminal.metrics` is intentionally content-free. It reports frame counts/bytes,
  delta density, queue depth/resyncs, subscriber count, and scrollback request volume;
  each client separately records attach/reconnect latency.
- Server terminal runtimes are constructed on the server state thread. A Debug
  thread-affinity assertion caught the earlier construction on the caller/test thread;
  the focused host regression and Release remote-terminal gate pass after the fix.

Validation checkpoint (2026-07-29):

- affected Release libraries build;
- Release `[remote-terminal]` tests pass (1,051 assertions, 16 cases);
- Release `[cli][server][remote-terminal]` tests pass (22 assertions, 3 cases);
- an alternate-output Release executable links and its smoke test passes;
- the initial Debug CTest run passed 21 of 22 entries, then the focused Debug
  thread-affinity regression passed after the server-thread ownership fix; and
- a clean full CTest rerun plus the live two-client gate below remain outstanding.

Windows two-client manual gate:

```powershell
$exe = Resolve-Path .\build-ninja-release\draxul.exe
$runtime = Join-Path $env:TEMP 'draxul-slice5-manual'
& $exe --shutdown-server --server-runtime-dir $runtime 2>$null
& $exe --experimental-remote-shell --server-runtime-dir $runtime `
  --server-shell powershell --server-scrollback-lines 25000
```

1. Launch the final command twice. Confirm one controller and one observer.
2. Produce more than a screen of output. Scroll/select/copy in A and confirm B remains
   live; type in A and confirm only A returns to live.
3. Run a full-screen program and a Unicode-producing command; resize both windows and
   transfer control repeatedly.
4. Close both windows, launch the command once more, and confirm the same shell/history
   returns.
5. Run `& $exe --shutdown-server --server-runtime-dir $runtime` when finished.

### Slice 6: server-authoritative shared topology

**Outcome:** both UIs reflect Space, tab, pane, and split mutations.

**Implementation status (2026-07-29):** first five vertical checkpoints implemented on
`codex/server-client-runtime`. `topology-v1` now supplies renderer-neutral snapshots,
revision-checked/idempotent commands, server authority, and a polling client
projection. Two headless clients converge in focused tests. Experimental remote UIs
project Space, tab, pane, name, and split-tree changes while retaining independent
active Space/tab/pane routes. Standard create/close/rename tab and pane actions now
submit server commands, and `client_local` descriptors instantiate a local host in
each UI. Live projection reconciles stable pane identities so a split, close, or ratio
update does not restart unchanged client-local hosts. `multi-terminal-v1` now gives
every `server_terminal` descriptor its own lazy server runtime and routes each
`RemoteTerminalHost` by stable `TerminalId`; removing the descriptor removes the
endpoint and process. Tab moves, pane swaps, keyboard/mouse ratio changes, and
equalization now route through the server and project without changing client-local
focus. Shared-pane restart now advances the server terminal generation and resyncs
every attached UI with the new process identity; client-local restart stays local.
The complete live two-window mutation demonstration remains.

**Work item:** [12 server-authoritative-topology -feature.md](../kanban/done/12%20server-authoritative-topology%20-feature.md)

Work:

- move neutral Session/Space/tab/pane/split values below `app/`;
- implement topology snapshot, revision, commands, events, and idempotent command IDs;
- make server-supported terminal panes reference `TerminalId`;
- add explicit `server_terminal` and `client_local` pane descriptors;
- change UI controllers into projections/adapters rather than authoritative owners for
  remote Sessions;
- instantiate one independent local host per client for a `client_local` descriptor,
  with an unavailable placeholder when that provider is not compiled;
- keep client route, window geometry, selection, and viewport local;
- implement split, close, rename, reorder, ratio, and terminal restart operations; and
- define conflicts and server rejection UI.

Checkpoint notes:

- Active Space/tab/focused-pane selection is deliberately absent from
  `TopologySnapshot`; those are per-window routes, not shared topology.
- Pane descriptors explicitly choose `server_terminal` with a stable TerminalId or
  `client_local` with a host kind. The initial topology points at the existing
  server-owned shell; dynamic terminal allocation follows with UI projection.
- Mutations require the caller's expected revision. A stale caller refreshes and
  retries; a repeated `(client_id, command_id)` is idempotent and returns the latest
  snapshot rather than rolling the caller backward.
- Current automated gate in `build-ninja-release`: `[server][topology]` passes
  86 assertions in 3 cases, `[host][remote-terminal][topology]` passes 10 assertions,
  the core suite passes 30,726 assertions, the app suite passes 4,115 assertions in
  478 cases, and smoke passes.
- The app polls topology at 100 ms. It maps opaque server Space IDs to its existing
  local controller IDs, routes structural Space mutations back to the server, and
  deliberately never serializes the active Space into shared state.
- Opaque tab and pane IDs map to stable local controller and leaf identities.
  `PaneManager::reconcile_projected_layout` validates a candidate split tree before
  applying it and preserves every existing host whose pane identity and launch kind
  are unchanged.
- Projected tabs are created without a placeholder host, so a new dynamic terminal
  never briefly attaches to the compatibility/default terminal. The descriptor's
  stable terminal identity is part of the saved launch value and host-reconcile key.
- Server tab order is projected as an exact stable-ID permutation while each window
  keeps its active tab. Pane swaps move stable pane/host identity through the split
  tree rather than restarting the host.
- Local divider IDs are reconstructed deterministically in pre-order and mapped to
  opaque server node IDs. Keyboard resize and equalization submit immediately;
  mouse movement is coalesced per SDL event batch and publishes the accepted ratio
  back through the same snapshot path.
- Shared terminal restart is an idempotent topology command. The server restarts the
  selected stable TerminalId, advances its runtime generation, and marks all
  subscribers for resync. Snapshot events include the current process ID so agent
  discovery and diagnostics do not retain the previous process identity.
- A late-client recovery test disconnects the original topology and terminal clients,
  reconstructs the full mutated snapshot in a new topology client, and reattaches a
  new terminal client to the same dynamic TerminalId, PID, and runtime generation.

Demonstration:

1. A and B begin on different Spaces.
2. A creates and names a Space; it appears in B without changing B's route.
3. A creates a tab and splits a shell; B receives the same tree and terminal IDs.
4. Add a client-local Neovim pane; both clients receive the same descriptor and each
   launches an independent Neovim runtime.
5. B renames and resizes the split; A reflects the accepted mutation.
6. B closes a pane while A observes one authoritative removal and each relevant local
   runtime shuts down independently.

Windows two-client checkpoint gate:

```powershell
$exe = Resolve-Path .\build-ninja-release\draxul.exe
$runtime = Join-Path $env:TEMP 'draxul-slice6-manual'
& $exe --shutdown-server --server-runtime-dir $runtime 2>$null
& $exe --experimental-remote-shell --server-runtime-dir $runtime
```

Run the final line in two PowerShell windows. In either Draxul window, create and
rename a Space, add and rename a tab, then create a vertical or horizontal split.
The other UI should reflect each accepted mutation within roughly 100 ms without
changing its active Space, active tab, or focused pane. Client-local split contents
are intentionally independent. Close both UIs and stop the shared server with:

```powershell
& $exe --shutdown-server --server-runtime-dir $runtime
```

Automated exit gate:

- two probe projections converge after every command permutation;
- duplicate command delivery applies once;
- stale destructive commands reject without partial mutation;
- one disconnected client can recover from a current topology snapshot;
- split-tree invariants and stable IDs survive two-client mutation tests.
- client-local runtime state is never mistaken for shared server state.

Rollback:

- only experimental remote Sessions use server topology; local `SpaceController` and
  `TabController` remain available for the recovery path.

### Slice 7: server-owned persistence and all-Session restore

**Outcome:** the server restores and checkpoints every Session/Space independently of
UI lifetime.

Work:

- move the snapshot codec and validation into `draxul-session-model`;
- make the server the only checkpoint writer;
- load current version-3 files without startup rewrite;
- restore all usable Spaces transactionally before launching shells;
- preserve agent restore policy and native session references;
- expose checkpoint results in protocol/status diagnostics;
- test UI-free periodic and shutdown checkpoints; and
- define behavior for snapshot content that references a client-local host.

Demonstration:

- create topology from two clients, close both, stop the server gracefully, restart it,
  and attach both clients to the restored ordered Sessions and Spaces.

Automated exit gate:

- existing snapshot fixtures still round-trip;
- server capture produces the same durable model as the current app path;
- interrupted or invalid writes preserve the last good file;
- restore failure in one pane does not destroy other usable Spaces;
- no client can write a Session file directly;
- cold restore is correctly reported as new shell processes, not live reconnection.

Rollback:

- original snapshots remain readable by the recovery path; schema version changes
  require explicit migration fixtures and are not automatic.

Implementation checkpoint (2026-07-29):

- The renderer-free v3 model/codec is shared by app and server.
- The experimental server owns every checkpoint under
  `<server-runtime-dir>/sessions/`, restores every usable Session and Space before
  processing queued requests, and checkpoints each dirty Session every 30 seconds
  plus graceful shutdown. `default` retains the readable `default.toml` path;
  named files use the existing collision-safe Session filename scheme.
- Stable pane/terminal descriptors, client-local host descriptors, and managed
  agent restore/session metadata survive cold start. Terminal text and live process
  identity do not; restored server terminals launch a new process lazily.
- Topology and terminal requests carry an explicit `session_id`. Each Session owns
  an independent topology service, terminal registry, ID allocator, checkpoint
  lifecycle, and status row. Terminal identity is therefore the explicit
  `(session_id, terminal_id)` pair; ordinary terminal IDs may safely repeat.
- `--server-status` and JSON status expose aggregate counts plus checkpoint
  path/state/last success/error and restore warnings for every Session. Invalid,
  partial, and interrupted writes preserve the affected previous file without
  blocking other usable Sessions.
- Selecting an unknown valid Session lazily creates it. Selecting `--session alpha`
  in two UIs shares `alpha`; selecting `--session beta` projects a separate Session.
- In-window switching remains intentionally out of scope for this slice: launch a
  UI with `--session <id>`. Moving the existing Session picker onto server Session
  discovery/routing can be added as a later UI slice without changing the protocol.

Repeatable Slice 7a manual test (use `build-ninja-release`):

```powershell
$exe = Resolve-Path .\build-ninja-release\draxul.exe
$runtime = Join-Path $env:TEMP 'draxul-slice7-manual'
& $exe --shutdown-server --server-runtime-dir $runtime 2>$null
& $exe --experimental-remote-shell --server-runtime-dir $runtime
```

Launch the final line in a second PowerShell window. Create and rename a Space,
add/rename a tab, and add a server-shell split. Close both UIs, then wait at least
30 seconds and inspect the UI-free checkpoint:

```powershell
& $exe --server-status --server-runtime-dir $runtime
Get-Content (Join-Path $runtime 'sessions\default.toml')
& $exe --shutdown-server --server-runtime-dir $runtime
& $exe --experimental-remote-shell --server-runtime-dir $runtime
```

The last launch should restore all ordered Spaces/tabs/panes. The split should keep
its pane and terminal descriptor but attach to a new process under the new server
epoch. A second UI should project the same restored topology. Stop the isolated
server when finished:

```powershell
& $exe --shutdown-server --server-runtime-dir $runtime
```

**Manual result (2026-07-29):** passed against `build-ninja-release`. Two live UIs
converged, detach/reconnect retained the topology, the UI-free periodic checkpoint
was present, and graceful shutdown plus cold restart restored the ordered
Spaces/tabs/panes with fresh shell processes.

Repeatable Slice 7b named-Session gate:

```powershell
$exe = Resolve-Path .\build-ninja-release\draxul.exe
$runtime = Join-Path $env:TEMP 'draxul-slice7-named'
& $exe --shutdown-server --server-runtime-dir $runtime 2>$null
& $exe --experimental-remote-shell --session alpha --server-runtime-dir $runtime
```

Launch the final line again in a second PowerShell window; both `alpha` windows
must reflect the same Space/tab/pane mutations. In a third window launch:

```powershell
& $exe --experimental-remote-shell --session beta --server-runtime-dir $runtime
```

Mutations in `beta` must remain isolated from `alpha`. Close all three UIs, inspect
the per-Session rows and paths, then cold-restart both named Sessions:

```powershell
& $exe --server-status --server-runtime-dir $runtime
& $exe --shutdown-server --server-runtime-dir $runtime
& $exe --experimental-remote-shell --session alpha --server-runtime-dir $runtime
& $exe --experimental-remote-shell --session beta --server-runtime-dir $runtime
```

Both named topologies must restore independently. The status command supplies the
exact hashed checkpoint path for each named Session; do not infer it from the ID.

Validation checkpoint (2026-07-29):

- focused named-Session cold restore: 41 assertions/1 case;
- all server-tagged Debug coverage: 565 assertions/29 cases;
- full Debug core and app executables pass (app: 4,115 assertions/478 cases);
- full Release core and app executables pass (app: 4,115 assertions/478 cases);
- `build-ninja-release\draxul.exe` builds, repository smoke passes, and an isolated
  Release server reports its per-Session status row before graceful shutdown.

### Slice 8: agent runtime and control migration

**Outcome:** agents continue to be detected and controlled with no UI attached.

**Implementation status (2026-07-29):** checkpoints 8a-8e implemented on
`codex/server-client-runtime`. The headless server now owns a Session-scoped
agent tracker, observes server terminal process trees and bounded screen state,
and publishes a revisioned `agent.snapshot`/`agent.poll` projection. The wire
contract contains only route, identity, lifecycle, status, and sanitized rule
metadata; terminal text and process command lines remain server-local. Remote
clients map that projection into the existing Agents rail, converge across
windows, and keep focus/attention acknowledgement client-local. The global
server also exposes `agent.list/get/explain/wait`, bounded `pane.read`, and
agent input/restart routes under the negotiated `agent-control-v1` capability.
Those operations continue after the final GPU client disconnects. Official
native-session reports now update server topology, projection, and persistence
only when their server epoch, Session, pane, managed identity, and runtime
generation all match; stale epochs, replaced runtimes, duplicate references,
and out-of-order reports are rejected. Managed agent launch now resolves the
server's built-in/custom profile registry, splits a server-owned terminal into
shared topology, eagerly starts it, and persists its identity, working
directory, restore policy, and official native reference. Every launch and
restart supplies server epoch, Session/Space/tab/pane/terminal identity, and
runtime generation to the child. Version-2 Codex and Claude hooks forward those
pins directly to the global server, including isolated runtime-directory
overrides. Stable arguments belong in the profile; one-off extra arguments are
rejected on remote launch until a server-private durable launch descriptor
exists.

Work:

- move `AgentController` authority and all terminal/process observations to the server;
- route manual discovery, manifests, hooks, attention, and native session references
  by server terminal/runtime generation;
- stream the Agents projection to all clients;
- move agent control API routing to the global server;
- update integration hooks to find the global server and target Session/pane identity;
- preserve bounded/sanitized explanations and event journals; and
- support client-local agent navigation without changing other clients' routes.

Demonstration:

1. Start Codex manually in client A's server-owned shell.
2. Observe the agent row and status in clients A and B.
3. Close A while Codex works.
4. Let status change to blocked/done.
5. Reopen A and observe the current state and attention projection.
6. Focus the agent in B without pulling A to the same Space.

Automated exit gate:

- discovery/status fixtures run against server-owned observations;
- hook events remain pinned to server epoch, pane, and runtime generation;
- waits and restart operations work with zero GPU clients;
- both clients converge on the same agent projection;
- terminal text is absent from persistence, control events, status, and logs.

Repeatable Slice 8 managed-agent gate:

```powershell
$exe = Resolve-Path .\build-ninja-release\draxul.exe
$runtime = Join-Path $env:TEMP 'draxul-slice8-agent'
& $exe --shutdown-server --server-runtime-dir $runtime 2>$null
& $exe integration install codex
& $exe --experimental-remote-shell --server-runtime-dir $runtime
```

Open a second UI with the final command, then run `launch_agent` from the
command palette and choose Codex. Both windows must show the same managed row
and terminal while retaining independent focus. Before detaching, focus the
managed pane in whichever window owns its controller and type `exit`. The pane
must disappear from both windows, neither window may exit or crash, and input
must continue in the surviving pane. Launch Codex again, close both windows,
and inspect the still-running agent without a GPU client:

```powershell
& $exe agent list --session default --server-runtime-dir $runtime
& $exe --experimental-remote-shell --server-runtime-dir $runtime
```

After reconnect, the same pane/agent identity must remain. A graceful server
shutdown followed by one more launch must cold-restore the managed pane under a
new server epoch; when `[agents].resume_on_restore = true` and an official
native reference was reported, Codex uses that reference for resume.

Automated validation checkpoint (2026-07-29):

- all Debug core/app CTest shards and repository smoke pass;
- all Release core/app CTest shards pass;
- the exact Release object graph links and passes smoke under an acceptance
  executable name while the user's existing `build-ninja-release\draxul.exe`
  server remains live;
- an executable-level isolated server reaches ready, serves global
  `agent list` with no UI, and shuts down cleanly;
- real managed-process coverage proves two projection clients converge,
  generation-aware restart environment, stale-report rejection, durable
  native reference/working directory, and cold restore under a new epoch; and
- Windows app-test CMake now stages `verovio.dll` beside the Release test
  executable, removing a previously stale-DLL-dependent pass; and
- projected host teardown releases the focused input route while the host is
  still alive, and dead remote panes wait for the server's topology update
  instead of attempting a rejected client-local close; and
- server-owned startup projects its first panes directly from authoritative
  topology rather than attaching a temporary legacy `terminal-1` host.
  A typed `terminal_not_found` initialization result triggers a bounded refresh
  while the server revision advances, so an agent exiting between snapshot and
  attach cannot veto the whole UI.

The manual two-window managed-agent sequence above remains the final Slice 8
acceptance item.

Rollback:

- remote agent behavior remains gated with remote terminal Sessions; the existing local
  harness still serves `--no-server`.

### Slice 9: make remote terminals the default and add server status surfaces

**Outcome:** ordinary Draxul uses the shared server confidently.

Work:

- flip normal terminal startup to server/client mode;
- retain `--no-server` for one documented confidence/recovery period;
- add Windows notification-area and macOS menu-bar status surfaces;
- expose Open Draxul, status, diagnostics/log, connected clients, running terminals,
  and one guarded stop flow with a conditional force-stop fallback;
- update user documentation, command palette, help, packaging, and logs;
- ensure app updates detect incompatible live servers without killing them;
- run extended Windows/macOS soak and sanitizer coverage; and
- decide, from field evidence, whether the local terminal backend should remain
  permanently as a diagnostic mode.

Demonstration:

- ordinary startup launches one visible server status icon and any number of UI
  windows; closing every UI leaves the server and terminals alive; reopening attaches
  immediately.

Automated exit gate:

- default-path smoke covers auto-launch, attach, shell, detach, and reconnect;
- two real GPU clients complete the shared-topology acceptance script;
- standalone product-host smoke proves no server dependency;
- full app/tests/smoke/render validation passes on Windows and macOS;
- server shutdown with live shells requires an explicit confirmed action.

Rollback:

- `--no-server` remains documented and tested; old snapshots are not rewritten into an
  incompatible format merely by launching the new client.

Implementation status (2026-07-29):

- ordinary shell startup now discovers or launches the shared server; explicit
  standalone product hosts and `--no-server` remain client-owned;
- the server process owns an SDL notification-area/menu-bar surface with live counts,
  Open Draxul, Open Log, refresh, and one guarded Stop Server action; its modal
  confirmation runs in a short-lived helper process, and Force Stop is offered only
  after graceful shutdown fails;
- server status includes active clients, Sessions, Spaces, terminals, live terminals,
  agents, persistence health, and the server log path;
- UIs unregister explicitly on clean detach and the server expires stale client
  presence after transport loss;
- an incompatible server is never killed or silently replaced;
- command-palette status/log/stop actions and CLI help/control routes are available;
- `--list-sessions` reads the running server's Session status rows instead of the
  legacy client-side saved Session files, with a Session-only JSON form;
- `--delete-session --session <id>` is server-owned: it refuses attached UIs,
  requires `--yes` before stopping live terminals, removes the in-memory Session,
  and deletes its checkpoint so it stays gone after restart;
- `--rename-session` is also server-owned, so list/rename/delete now address the
  same live Session registry and report the shared-server store in their output;
- ordinary first startup performs a one-time, read-only import from
  `<config>/sessions/` (and the older single default snapshot) into the server
  store. It validates each source, copies its original bytes without rewriting,
  leaves the legacy store untouched, and records a durable marker. Bad sources
  are warnings, not a reason to repeat or abandon the migration;
- checkpoint writes are durable and asynchronous. Corrupt files are archived,
  partial restores keep checkpointing, attaching UIs see one-time warnings, and
  graceful shutdown uses a bounded persistence budget while still capturing a
  revision that advanced behind an in-flight write;
- focused app/core tests and an isolated executable gate cover ordinary auto-launch,
  detach with a live terminal, status, refusal of unconfirmed shutdown, confirmed
  shutdown, and server exit.

Legacy-store retirement:

- The legacy store is intentionally a confidence-period fallback for
  `--no-server`, not a permanent read-through second home. The shared server only
  imports it once and never writes it.
- Retire the importer and legacy local store only after the shared-server default
  has shipped with migration telemetry/manual recovery confidence and
  `--no-server` no longer needs to preserve old layouts. Until then, schema
  compatibility remains one-way: validate and byte-copy old snapshots, then let
  normal server checkpoints evolve the imported copies.

Repeatable Windows acceptance (`build-ninja-release`):

1. Stop any current test server explicitly, then create an empty isolated runtime:
   `$runtime = Join-Path $env:TEMP ("draxul-slice9-" + [guid]::NewGuid())`.
2. Start `.\build-ninja-release\draxul.exe --server-runtime-dir $runtime`.
3. Start a second copy with the same arguments. Confirm one tray icon, two clients,
   matching Spaces/panes/output, and explicit controller takeover.
4. Close both UIs, run
   `.\build-ninja-release\draxul.exe --server-status --json --server-runtime-dir $runtime`,
   and confirm zero clients with the shell still live.
5. Reopen two copies and confirm the same Session/terminal identity, current cells,
   scrollback, and agent state.
6. Confirm plain `--shutdown-server` refuses while a terminal is live, then use
   `--shutdown-server --yes` and confirm the tray icon/server exit.
7. Launch `--host nvim` and one built standalone product host; confirm neither depends
   on nor starts the isolated server.

Windows validation (2026-07-30):

- `build-ninja-release` linked the production executable and both focused test
  binaries;
- all four Release core shards and both Release app shards passed;
- the Debug Vulkan smoke passed;
- `basic-view` render comparison passed (1.69638% changed pixels against a 2.1%
  threshold);
- an isolated ordinary UI detached and a new UI reconnected to the same server epoch,
  terminal count, and Space count with zero stale clients;
- unconfirmed live-terminal shutdown was refused and confirmed shutdown stopped the
  server; and
- an explicit Neovim product host opened without starting the isolated server.

### Post-Slice 9 robustness review (2026-07-30)

The completed local client/server path was reviewed across startup/discovery, IPC
framing and listener concurrency, server ownership, terminal processes, topology and
agent projection, UI teardown, persistence, status/tray control, and forced shutdown.

Hardening completed:

- connected clients and restored/runtime-created Sessions now have explicit limits;
- client goodbye and inactivity expiry remove fake and real terminal subscriptions,
  release controller claims, and bound queued terminal state;
- remote hosts reattach after lease expiry and retry only commands the server
  explicitly rejected as `not_attached`;
- topology and agent clients include their UI identity on polling and automatically
  refresh after a server revision rollback;
- both Windows named pipes and Unix-domain sockets use four concurrent listeners, so
  one stalled client cannot occupy the only IPC reader;
- Windows named pipes reject remote SMB clients, retain the first pipe instance for
  the server lifetime, and use identification-level security quality of service;
- runtime metadata is written through a secure same-directory temporary file and
  atomically replaced with owner-only permissions; Windows runtime directories use
  a protected owner-only DACL;
- server, topology, and agent parsers use shared range-checked integer extraction;
  status values and client identifiers are bounded and reject control characters;
- remote terminal dimensions match the PTY/ConPTY 320x200 bounds rather than allowing
  an inconsistent, potentially oversized million-cell grid;
- terminal allocation is capped server-wide, scrollback storage is lazy, subscriber
  queues are byte- as well as count-bounded, and oversized resyncs degrade visual
  metadata without losing text or wedging the frame;
- protocol-major-2 terminal cells use packed colours and shared attribute/hyperlink
  tables; validated dirty lists update client grids incrementally;
- fake and real endpoints now share one `RemoteTerminalService` state machine, including
  acknowledgement ordering and generation resync;
- agent wait filters, client identities, Sessions, event queues, protocol frames, and
  scrollback pages are bounded; and
- Windows force-stop holds the original process handle across the final PID/epoch
  check, while every platform rejects unrepresentable process IDs.

Focused regression coverage proves client-limit release, inactivity cleanup,
reattachment after a transport gap, repeated two-client controller transfer,
goodbye cleanup, topology/agent revision rollback recovery, stale-revision errors,
dimension rejection, and bounded agent waits.

Validation after hardening:

- `py do.py build release --ninja` rebuilt the production executable;
- both focused Release test executables rebuilt and all six core/app CTest shards
  passed;
- the updated Debug executable passed `--console --smoke-test`; and
- an isolated Release server accepted a normal shared-client smoke, retained its live
  terminal with zero clients after detach, reported a stable epoch, and exited cleanly
  through confirmed shutdown.

Follow-up recovery hardening completed on 2026-07-30:

- one App-owned recovery state now supplies a mutable server epoch plus isolated
  terminal, topology, and agent retry channels;
- terminal hosts survive long outages, refresh a replaced server epoch, and reattach
  in place; newly projected panes consume the same refreshed epoch;
- terminal, topology, agent, status, and attachment work runs off the render thread;
- jittered exponential backoff is attempt-based and capped at five seconds; and
- expired queued control work is cancelled, while topology, terminal, and agent
  mutations carry bounded request-ID deduplication contracts.

Windows validation includes a real 10.25-second outage followed by a new-epoch server
restart, attachment of both the existing pane and a newly projected pane, all six
Release/Ninja core and app shards, and Debug/Ninja smoke.

Topology projection failure handling completed on 2026-07-30:

- client-local host descriptors unavailable in one build remain visible as
  inert placeholder panes while every supported pane stays live;
- topology and agent delivery now separates received snapshots from UI-applied
  snapshots, retaining and coalescing pending state until an epoch/revision
  acknowledgement;
- command-result snapshots use the same acknowledged apply path, with activation
  deferred until the corresponding topology has applied;
- a transient pane creation failure retries automatically, performs stale cleanup,
  and restores the active input route on every exit; and
- apply-error notifications latch by exact error text rather than repeating on
  every poll.

Focused Release/Ninja coverage exercises unknown and known-but-unregistered host
placeholders, republish/ack/coalescing semantics including a same-revision new
epoch, and an App-level first-apply failure followed by successful retry and
keyboard routing restoration.

Final post-review hardening completed on 2026-07-30:

- negotiated UI identities receive server-issued connection tokens; retry-safe hello
  registration reuses a client nonce, server replacement rotates tokens, and concurrent
  recovery cannot install a stale identity;
- restored child topology IDs are parent-scoped, command acknowledgements return the exact
  created Space/tab/pane ID, and delayed divider commits retain durable node identities;
- GUI and control-plane agent restart use the same durable server mutation and retry contract;
- each agent mutation reserves one id per logical user action, retries only within that
  action, fences stale pre-restart projections below the acknowledged generation, and uses
  one absolute deadline outcome;
- terminal titles and shell marks are protocol-bounded, unconsumed worker publications are
  retained, empty restart titles restore the default window title, and fake-terminal restart
  resets the same state as the real runtime;
- terminal count, per-subscriber queues, and a shared 24,000,000-cell scrollback reservation
  bound the practical server footprint; scrollback resize accounts for its old-plus-new peak
  and safely degrades to no scrollback if a terminal-buffer allocation fails; and
- the final Windows Release/Ninja gate passed all 22 CTest groups, including smoke, five
  render comparisons, all core/app shards, and optional product modules. Standalone Debug
  smoke also passed.

Known boundaries retained for later work:

- same-user local compatibility still permits an unnegotiated legacy client identity. The
  Slice 10 bridge must disable that path and require its authenticated/negotiated identity;
- subscriber queues are byte-bounded individually but do not yet share an aggregate server
  queue budget. Global client and terminal caps keep the total finite; a future high-scale
  runtime should add aggregate admission accounting; and
- Unix/macOS code was reviewed for ownership and compilation consistency but was not
  executed by this Windows validation run.

### Slice 10: SSH bridge and remote hardening

**Outcome:** a local GPU client can attach to a Draxul server on another machine through
SSH without exposing a Draxul network port.

This slice is deliberately deferred until local multi-client behavior is stable, but
the preceding slices must not close its architectural route.

Work:

- implement the transport-neutral stdio bridge;
- negotiate compression and client capabilities;
- separate server paths from local file-opening actions;
- harden latency, disconnect, replay, takeover, and resync behavior;
- add SSH host/session selection UX;
- define remote clipboard and notification policy; and
- test mixed operating systems where practical.

Exit gate:

- no raw network listener is required;
- the remote UI passes the same snapshot/delta/topology convergence suite as local IPC;
- disconnecting SSH leaves remote terminals running;
- reconnecting through a new SSH process recovers current state;
- local paths are never accidentally executed or opened on the remote server.

## Two-client acceptance script

Run this manually at the end of Slices 3, 4, 6, 8, and 9, expanding assertions as
features become available:

1. Start an isolated server with an empty runtime directory.
2. Open UI A and UI B; confirm the same server epoch and different client IDs.
3. Place A and B on different Spaces.
4. Create a Space, tab, and split in A; confirm they appear in B without changing B's
   viewed route.
5. Type a command in A; confirm both clients render identical terminal output.
6. Attempt input from B as an observer; confirm it is rejected visibly and safely.
7. Take over in B; confirm A becomes observer and B controls input and resize.
8. Resize B; confirm both clients converge on the authoritative terminal grid.
9. Rename and resize topology in B; confirm A reflects the accepted mutation once.
10. Start a manually detected agent and confirm both agent projections match.
11. Close A; confirm B and the shell continue.
12. Close B; allow the shell/agent to produce output with no clients.
13. Reopen A; confirm one current snapshot, matching terminal/runtime IDs, scrollback,
    and current agent state.
14. Reopen B; confirm independent navigation and shared state.
15. Stop the server gracefully; confirm both clients receive shutdown and durable state
    restores on the next server start.

Where possible, run the same sequence with non-GPU probe clients in CI and reserve the
real two-window rendering check for smoke/manual validation.

## Test strategy

### Unit

- IDs, enums, validation, limits, framing, capability negotiation;
- terminal-core replay and semantic digest;
- snapshot/delta creation and application;
- topology commands, revisions, idempotency, and conflicts;
- controller lease transitions;
- persistence codec and migration;
- agent projection and status authority.

### Component

- named-pipe and Unix-socket partial reads/writes, cancellation, stale endpoints, and
  permission behavior;
- PTY/ConPTY runtime with deterministic child helpers;
- client cache resync after dropped/out-of-order messages;
- bounded slow-client queues;
- server status projection and graceful shutdown.

### Process integration

Use isolated runtime/config directories and deterministic helper processes. Cover:

- concurrent server launch;
- incompatible protocol versions;
- client crash and reconnect;
- server crash and cold restore;
- shell exit/restart;
- zero-client output;
- two-client topology convergence;
- control takeover and resize;
- persistence interruption; and
- standalone-host isolation.

Tests must verify behavior through endpoints and protocol, not by reaching into server
objects across process boundaries.

### Render and manual

- a deterministic `RemoteTerminalHost` render-snapshot scenario;
- two real UI windows against one server;
- interactive PowerShell and WSL on Windows;
- interactive Zsh/Bash and full-screen applications on macOS;
- Codex/Claude status transitions while detached;
- tray/menu-bar status and shutdown warning;
- font/DPI differences between two clients; and
- long-running detach/reconnect soak.

### Cross-platform and sanitizers

- Windows Debug and Release app/tests/smoke;
- macOS Debug and Release app/tests/smoke;
- macOS ASan for lifetime and decoder errors;
- macOS TSan for server/client/PTY queues and shutdown;
- malformed/oversized protocol corpus on both platforms; and
- CMake link-isolation checks for server/core/protocol headers.

## Failure semantics

| Failure | Required behavior |
|---|---|
| UI exits or crashes | Server and shells continue; lease is released |
| Client stream drops | Client reconnects and requests topology/terminal snapshots |
| Slow client | Its deltas coalesce/resync; PTY and other clients continue |
| Shell exits | Terminal remains inspectable with exit status; restart is explicit/policy-driven |
| Server exits gracefully | Checkpoint, notify clients, stop children, remove owned endpoints |
| Server crashes | Shells are lost; next server cold-restores durable topology honestly |
| Protocol mismatch | Reject with actionable status; never auto-kill live old server |
| Snapshot is corrupt | Preserve source, report error, use deterministic fallback |
| One pane restore fails | Prune/recover usable topology according to existing policy |
| Controller disappears | Retain last size, release lease, allow takeover |
| Tray/status surface fails | Log and continue serving without graphical status |

## Instrumentation and privacy

Server diagnostics should make distributed behavior debuggable without capturing user
terminal contents:

- server epoch, client/connection IDs, terminal IDs, runtime generations, and sequence
  ranges;
- topology revisions and command IDs;
- connect, attach, detach, lease, resync, checkpoint, launch, exit, and shutdown events;
- bounded frame sizes, queue depth, coalescing, dropped-delta, and snapshot counters;
- sanitized errors and capability mismatches; and
- optional raw PTY capture only through the existing explicit diagnostic mechanism.

Do not log input bytes, terminal cell contents, clipboard data, environment secrets,
agent prompts, or native conversation contents.

## Rollout and confidence policy

1. Slices 0-2 make no terminal ownership change.
2. Slices 3-5 require an explicit experimental client/server flag.
3. Slices 6-8 apply server authority only to experimental remote Sessions.
4. Slice 9 flips the default only after Windows and macOS gates, the two-client script,
   detach/reconnect soak, and snapshot migration tests pass.
5. Keep `--no-server` through at least one confidence period and while any unsupported
   shell-host gap exists.
6. Do not delete the local adapter in the same change that flips the default.
7. Do not rewrite or delete current Session files merely because server mode starts.
8. Keep commits cohesive by slice; each commit must build, pass its focused tests, and
   preserve an operable application.

Implementation work items belong in `kanban/pending/`, normally one cohesive item per
active slice. This document remains the architecture and acceptance source of truth;
phase completion should be ticked here and in the matching full-filename kanban item.

## First implementation boundary

The first implementation batch should stop after Slice 3:

```text
characterization
    -> extracted terminal core
    -> real singleton hello/status
    -> fake remote terminal
    -> two-client snapshot/input/resize/reconnect demonstration
```

That batch proves the dependency direction, executable modes, transport, protocol,
threading, client projection, renderer adapter, multi-client semantics, and resync
model before moving a real PowerShell, WSL, Bash, or Zsh process out of the UI.

If that boundary feels awkward in practice, change the architecture there. Do not
paper over it by giving the server access to `IHost` or by falling back to raw PTY
replay.
