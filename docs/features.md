# Draxul Features

Quick reference of all user-facing features, configuration, CLI flags, build options, and CI infrastructure.

---

## Host Types

| Host | Flag | Description |
|------|------|-------------|
| Neovim | `--host nvim` | Embeds `nvim --embed` via msgpack-RPC over stdin/stdout pipes |
| Markdown | `--host markdown --source <file.md>` | Native Draxul markdown viewer host using the FreeType/HarfBuzz font pipeline, MD4C parsing, variable-height document rows, configurable body text size/margins, restrained styled headings, section indentation, front matter/code/list/table decorations, mouse wheel/PageUp/PageDown/Home/End plus Vim-style `j/k`, `Ctrl+F/B`, `gg`, `G` scrolling, and a draggable proportional scrollbar |
| Kanban | `--host kanban [--source <folder>]` | Native grid-backed kanban viewer for a `kanban/` folder. Subfolders become columns, Markdown files become cards, `.draxul-kanban.toml` stores ordering, Vim-style `h/j/k/l`, `Ctrl+F/B`, `gg`, and `G` move selection within the current column, shifted up/down arrows reorder cards, `<`/`>` move files between column folders, `z` zooms to the selected column full-width (`z` again restores all columns), `p` pins a bottom-third Markdown preview of the selected card that follows the selection (`p` again removes it), and Enter opens the selected card's Markdown file in a background Neovim host (reusing an existing Neovim pane or spawning a split) without moving focus off the board |
| Bash | `--host bash` | Server-owned PTY terminal (Unix) |
| Zsh | `--host zsh` | Server-owned PTY terminal (Unix) |
| PowerShell | `--host powershell` | Server-owned ConPTY terminal on Windows |
| WSL | `--host wsl` | Server-owned Windows Subsystem for Linux terminal |
| MegaCity | `--plugin dev.draxul.megacity` with `{"mode":"city"}` | Dynamic semantic code-city plugin with textured materials, shadows, SSAO, mouse-drag pan, Alt+drag orbit, and a configurable local Tree-sitter scan root |
| BioView | `--plugin dev.draxul.megacity` with `{"mode":"biology"}` | Biology mode of the same dynamic plugin: modules become tissues, classes become cells, and dependencies become blood vessels; semantic model, procedural geometry, UI, assets, and Vulkan/Metal renderer are plugin-owned |
| ScoreView | `--plugin dev.draxul.scoreview` at launch or on pane/tab commands | Dynamically loaded music score viewer + adaptive learning runner ([docs/features/scoreview.md](features/scoreview.md)); launch JSON accepts `source`, `mode`, and `background_playback` |
| SatView | `--plugin dev.draxul.satview` at launch or on pane/tab commands | Dynamically loaded satellite overview with an interactive scene, map and ground-observer views, background catalog/simulation work, and plugin-owned ImGui controls. Full narrative: [docs/features/satview.md](features/satview.md) |
| Rezonality | `--plugin dev.draxul.rezonality` at launch or on pane/tab commands | Fault-tolerant Vulkan/Metal live shader viewer ported from VkLive. It loads the bundled `simple` project or a configured project directory, watches external edits, compiles GLSL off the UI thread, and retains the last valid GPU generation when an edit fails |

Shell Session splits use the server's platform default shell (Zsh on macOS,
PowerShell on Windows). Explicit self-contained product windows advertise only
the client-owned hosts they can create.
Host names, aliases, platform support, test-only status, and split/new-tab visibility come from the registered provider metadata. Optional hosts that are not built are therefore absent from the command palette and rejected explicitly by `--host`; the hidden `nanovg-demo` provider remains directly launchable by the render harness.

### Native GPU pane plugins

Draxul can host trusted, client-local native plugins in a pane or an entire tab.
Plugins are discovered at startup from `%APPDATA%/draxul/plugins` and
`<exe>/plugins` on Windows, or `~/Library/Application Support/draxul/plugins`
and the app bundle's `Contents/PlugIns` on macOS. Each immediate child directory
contains either a legacy `plugin.toml` plus platform DLL/dylib or atomically
published immutable generations selected by `current.json`. User plugins override
bundled plugins with the same stable ID. Draxul shadow-copies the complete selected
package to a host-private per-process runtime directory, so the producer can rebuild
without overwriting or locking the active DLL/dylib.

`reload_plugin` in the GUI and `draxul plugin reload <id>` on the local control
endpoint prepare and validate a new generation, quiesce every matching pane in that
UI, release their render passes, wait for the renderer once, and replace them as one
cohort. A lifecycle failure rolls the cohort back to the resident prior generation.
Late callbacks carry generation-scoped tokens and cannot target the replacement.
Candidate storage-service writes are journaled until activation. The optional
`draxul.hot-reload` extension transfers bounded transient JSON state; missing or
incompatible state starts fresh and warns rather than rejecting a healthy build.
Retired native images remain resident until process exit because general C++ and
Objective-C module unloading is not a safe runtime contract.

The server stores only the stable plugin ID and bounded JSON configuration. Each
attached UI loads its own installed module, so an unavailable or incompatible
plugin leaves the shared pane intact and shows an actionable placeholder. The
current-only C ABI v2 in `sdk/include/draxul/plugin_api.h` supplies
Vulkan or Metal command-buffer access without transferring swapchain, submission,
or presentation ownership. Draxul is pre-release, so the loader intentionally has
no v1 compatibility path: bundled modules, fixtures, manifests, and the SDK move
together when the ABI changes.

The public header is also exported as the installable CMake package
`DraxulPluginSDK`; external plugins link `Draxul::PluginSDK` and do not need a
Draxul source checkout. The `draxul-plugin-sdk` install component additionally
ships the `draxul-types` headers and static library (plus the
`draxul-performance` archive its `PERF_MEASURE` spans reference and the
`<draxul/plugin_runtime.h>` runtime-context vocabulary) so standalone product
builds can link the shared foundation types. The spinning-triangle example supports a standalone
CMake build, and the `draxul-sdk-external-smoke` target installs the SDK, builds
that example from a clean copied tree, loads the resulting module, and renders a
non-blank raw-GPU frame. When mounted in the Draxul tree, the same example owns
its manifest, shader payload, and staging declaration through the generic
`draxul_register_bundled_plugin` contract. Shared same-build C++ helpers must use
an explicitly allowed `Draxul::PluginSupport::*` target; only the SDK C ABI is a
runtime contract. The allowlist exposes narrow leaves for raw-ABI host services,
render-pass/context types, configuration documents, text and tooltip rasterizing,
HTTP, logging/performance types, ImGui, ImGui core (the single
SDL-scancode→ImGuiKey table plus the `IImGuiHost` backend interface in
`libs/draxul-imgui-core`, exported as `Draxul::PluginSupport::ImGuiCore`),
NanoVG (the NanoVG core plus Draxul's custom Vulkan/Metal backends with a
settable shader root, exported as `Draxul::PluginSupport::NanoVG` from
`libs/draxul-nanovg`; a standalone extraction builds the same directory with
`DRAXUL_NANOVG_BACKEND_ONLY=ON`), and Vulkan resource ownership. The Vulkan leaf
owns the whole HDR/MSAA scene
scaffolding both 3D products render with: attachment creation, a per-format MSAA
sample-count probe that walks 4x/2x/1x against the real colour and depth formats
(never device limits alone), shader-module loading, load-time image upload with
sampler and mip generation, and an `HdrScenePipeline` that owns the MSAA scene
pass, the resolve, the tone-map pass, their per-frame targets, and the single set
of subpass dependency masks. A `Draxul::PluginSupport::CameraInput` leaf owns the
shared camera key-latch table (arrows/WASD, Q/E orbit, R/F zoom with a
configurable modifier guard so a host accelerator such as Ctrl+R is not consumed,
T/G pitch) and the drag-inertia plus click/double-click state machine; each
product binds the key groups to its own camera axes, and the camera math stays
per-product. The plugin ImGui leaf also carries the shared
`PluginImGuiContext` lifecycle (context flags, font, backend attach, frame
begin, ordered shutdown, optional ini persistence) and the header-only
`ImGuiInputBridge` (modifier/key, mouse remap, position/wheel/text routing)
that all three product runtimes use instead of hand-rolled copies. A
configure-time graph check rejects any support leaf that reaches Draxul's host,
window, renderer implementation, topology, terminal, or app orchestration.
SatView, MegaCity/BioView, and ScoreView are all registered in strict dependency
mode: their product targets may link only other product-owned targets,
third-party libraries, the public SDK, or these named plugin-support leaves.
On macOS, bundled plugin dylibs must not register Objective-C classes that the
host executable also defines (the runtime warns "Class X is implemented in
both" and casts can misbehave). Two build rules enforce this: plugin product
targets link `SDL3::Headers` instead of the SDL archive and the module links
with `-undefined dynamic_lookup`, so SDL calls resolve against the host
executable's statically linked SDL at load; and each plugin compiles its own
ImGui Metal backend with plugin-unique class names via
`draxul_plugin_imgui_attach_metal_backend` instead of receiving it from the
shared plugin ImGui static library. Windows and standalone plugin builds keep
linking the SDL archive directly.
The same isolation applies to C++ weak symbols: bundled macOS plugin dylibs
export only the `draxul_plugin_query_v2` C entry point
(`draxul_register_bundled_plugin` passes `-exported_symbol`). Without this,
inline functions the plugin shares with the host — Dear ImGui's header inlines
especially — are emitted as coalescible weak externals, and dyld unifies them
across images at load, so the plugin's ImGui would call host-image inlines that
read the host's `GImGui` and mix two ImGui context universes (a crash first
reproduced by the headless `do.py score-shot-check` guard).
SatView and MegaCity use the generic plugin lifecycle/viewport contract rather
than `IHost`, and resolve packaged assets and source roots explicitly instead of
assuming a Draxul checkout path.
ScoreView has the same repository-extraction proof through the opt-in
`draxul-scoreview-extraction-smoke` target. It copies only the product, the
shared plugin ImGui support, and the `draxul-imgui-core` leaf (staged as
`support/imgui-core`) beside an installed SDK, performs a cold build, and loads
the resulting module in a clean Draxul package. The target is intentionally not
part of ordinary CTest because compiling Verovio from a cold tree is expensive.

ABI v2 separates render deadlines from main-thread logic deadlines. Thread-safe
callbacks can request either kind of work, and plugins quiesce background/device
callbacks before Draxul waits for renderer idle and destroys the instance. An
optional presentation extension supplies per-instance display/status text,
background, cursor, actions, readiness, and print hints. Hidden tabs and Spaces
stop render animation deadlines; plugins explicitly decide whether any non-render
logic continues while hidden.

Versioned path, storage, and UI-style services expose plugin resource,
configuration, data, cache, and temporary directories, bounded atomic JSON
documents, and Draxul's recommended font/scale with a change generation. Storage
is client-local and main-thread-only; background workers request a logic tick
before reading or writing it.

The installed SDK exposes only C-owned lifecycle, input, service, presentation,
and raw Vulkan/Metal frame structures. No Draxul C++ renderer, ImGui type, or
build-matched object crosses that public boundary. Vulkan callbacks begin with no
active render pass and a color-attachment-optimal target; Metal callbacks begin
with no encoder and a load/store continuation descriptor. Plugins end every pass
or encoder they create, restore the documented continuation state, and never
submit, present, retain, release, or destroy borrowed host objects.

Bundled IDs currently include `dev.draxul.satview`, `dev.draxul.scoreview`,
`dev.draxul.rezonality`, and the ABI example `dev.draxul.spinning-triangle`.
Product preferences are pane-local
and durable; shared launch JSON remains limited to values every attached UI should
see. SatView now owns its complete product stack under `plugins/satview`: model,
services, simulation, UI, Vulkan/Metal HDR renderer, shaders, catalogs, textures,
and tests. Its dynamic module renders the actual satellite application rather
than the earlier procedural stand-in. ScoreView likewise owns its notation,
learning, transport, worker, device, UI, and raw Vulkan/Metal rendering stack
under `plugins/scoreview`; no Draxul C++ canvas or ImGui object crosses the ABI.

```text
draxul plugin list --json
draxul plugin get <plugin-id> --json
draxul plugin reload <plugin-id> --session <id> --json
draxul pane split <pane-id> --direction right --plugin <plugin-id> \
  [--plugin-config <json>] --json
draxul tab create --space <space-id> --name <name> --plugin <plugin-id> \
  [--plugin-config <json>] --json
```

For example, open the bundled score reader without a terminal:

```text
draxul tab create --space <space-id> --name ScoreView \
  --plugin dev.draxul.scoreview \
  --plugin-config '{"source":"C:/scores/piece.musicxml","mode":"paged"}' --json
```

ScoreView pauses transport and releases device leases while hidden by default;
`"background_playback":true` opts into hidden logic/audio without hidden renders.

The bundled `dev.draxul.spinning-triangle` module is a real dynamically loaded
Vulkan/Metal sample. Its configuration accepts `speed_radians_per_second`,
`initial_angle`, `paused`, and optional `remember_state`; Space toggles pause and
left-click reverses it. Its rotation is driven by ABI v2 logic ticks rather than
render callbacks. With state retention enabled, pause and direction are stored
per pane for this UI; another attached UI resolves the shared pane independently.
CTest loads this plugin from the staged application bundle so manifest/library
filename drift and dynamic-loader or ABI failures are caught on both platforms.

---

## Shared Terminal Server

- `draxul --server` runs the renderer-free per-user server, while
  `--server-status` and `--shutdown-server --yes` inspect or stop it.
  `--server-runtime-dir <path>` isolates an endpoint for testing.
- `--server-status --json` includes bounded control-transport diagnostics:
  accepted/current/peak listener occupancy, request and failure counts, per-method
  queue/dispatch/response timing, and failures grouped by operation, transport
  stage, native error domain/code, and compatibility classification. These are
  physical connection/request measurements and are separate from the logical
  client leases reported by `connected_clients`.
- An ordinary `draxul` launch discovers or starts the singleton, opens the default
  shared shell Session, and reconnects to the same server-owned Spaces, panes,
  terminals, and agents after the UI closes. Shells have no client-owned fallback.
  Explicit hosts such as `--host nvim`, Markdown, and Kanban remain client-owned
  and do not start the server. MegaCity/BioView, SatView, and ScoreView are
  client-local plugin panes created in shared server topology.
- New clients prefer the negotiated `session-stream-v1` path: one authenticated,
  epoch-bound local event connection per attached UI carries bounded topology, agent,
  and terminal batches plus idle heartbeats. The server state thread only enqueues
  work to a bounded writer and never waits for platform I/O; a stalled UI is isolated
  and disconnected without blocking healthy clients or CLI status requests.
  Registration/cursor updates travel on the stream. When
  `session-stream-commands-v1` is also negotiated, attached-UI terminal input,
  resize, controller and scrollback operations, topology mutations, and GUI agent
  start/restart requests use correlated stream commands. Existing mutation IDs make
  retries idempotent, command responses have reserved priority capacity ahead of bulk
  presentation, and lost responses can be replayed after reconnect without applying
  the mutation twice. Bootstrap, status, diagnostics, CLI access, and compatibility
  continue to use short control requests. Stream negotiation or transport failure
  falls back to one recurring `session.poll` per UI, and older servers without either
  capability retain the compatible per-channel polling path. Terminal channels remain
  independently ordered and recover from overflow or cursor gaps with a channel-local
  snapshot; the shared scheduler rotates fairly within the stream's negotiated payload
  budget. The persistent path feeds that scheduler with typed requests and responses,
  so it serializes only the final stream frame; the JSON `session.poll` boundary is
  retained only for fallback and compatibility clients. A transient transport failure
  leaves the last coherent topology, agent, and
  terminal projections visible and does not produce an immediate toast. If the whole
  Session remains unavailable for two seconds, the UI emits one background-reconnect
  warning; recovery clears that outage state without a success toast. The diagnostics
  panel reports the selected Session transport, connection phase, outage duration,
  reconnect/fallback/resync counters and bounded reason buckets, alongside the short
  control transport's request and native-stage failure metrics.
- The server owns a Windows notification-area or macOS menu-bar status item. Its menu
  reports connected clients, Sessions, Spaces, terminals, live terminals, and agents,
  and provides Open Draxul, refresh, open-log, and one guarded Stop Server action.
  The stop dialog runs in a short-lived helper process on Windows. On macOS the server
  itself runs as a nested `LSUIElement` app with a distinct bundle identifier, and its
  dialog runs in the menu-bar process while the RPC loop continues on the server thread.
  The normal Draxul app therefore remains purely a UI client and always attaches to the
  existing server when reopened. The dialog tries graceful shutdown first and offers
  Force Stop only if that attempt fails. `server_status`, `open_server_log`, and
  `stop_server` expose the matching UI operations through the command palette.
- On Windows the server runs from a sibling `draxul-server.exe` copy that is refreshed
  only when a server is started. The UI executable therefore remains replaceable while
  the server is running, so normal development builds can relink `draxul.exe` without
  first shutting down persistent terminal Sessions. macOS uses the corresponding nested
  helper executable inside the application bundle.
- Graceful shutdown refuses to stop a server with live terminals unless the action is
  explicitly confirmed. CLI shutdown therefore uses `--shutdown-server --yes`, while
  `--force-stop-server --yes` is reserved for an unresponsive server. Incompatible
  live servers are reported and left running; stop the existing server explicitly
  before retrying.
- `--experimental-server-client` and `--experimental-remote-shell` remain
  compatibility aliases for earlier slice scripts; they do not select another
  runtime. `--experimental-remote-terminal` retains the deterministic fake
  terminal as a protocol/renderer diagnostic.
- `--experimental-remote-terminal` is the Slice 3 test path. It discovers or starts
  the singleton, disables file-backed Session restore for that UI, and renders one
  deterministic server-owned fake terminal through `RemoteTerminalHost`. Multiple
  windows using the flag see the same terminal cells, title, cursor, dimensions, and
  controller lease. The first attached window controls input and resize; observers
  can run `take_terminal_control` from the command palette to take over. Brief local
  transport interruptions and full server restarts use per-channel, jittered
  exponential backoff capped at five seconds. A pane remains alive while reconnecting,
  refreshes the shared server epoch, and reattaches in place unless the server
  authoritatively reports that its terminal was removed.
- `--experimental-remote-shell` follows the same production renderer, protocol,
  and controller lease, lazily starting a real server-owned PowerShell on Windows
  or the configured login shell on macOS/Linux. Closing every attached window leaves
  the process and terminal state alive in the server; reconnecting recovers the same
  terminal ID, process ID, generation, and current cells. A clean process exit removes
  its shared pane, or its now-empty tab/Space, when another pane remains in the Session.
  A clean exit from the final shell closes each attached UI while leaving the server
  running; abnormal exits remain visible for explicit restart. The
  diagnostic fake path remains available. On first server launch,
  `--server-shell <powershell|bash|zsh|wsl>`,
  `--server-working-dir <path>`, and `--server-scrollback-lines <count>` define
  server-owned process/history settings. Stop an already-running isolated server
  before changing them; client fonts, palette, selection, and rendering remain local.
- The server owns durable checkpoints for every Session under
  `<server-runtime-dir>/sessions/`. It restores every usable Space before processing
  client requests, checkpoints changed topology every 30 seconds without a UI, and
  checkpoints again on graceful shutdown. Writes flush a temporary file before an
  atomic replace and run off the kernel request loop. A corrupt checkpoint is archived
  as `.corrupt-<timestamp>` before saving resumes; partial restores remain writable.
  Restore/checkpoint warnings are shown once in an attaching UI as well as by
  `--server-status`.
- Remote terminal clients receive a complete versioned snapshot followed by ordered
  dirty-cell and controller events. Each client has a bounded server queue; a slow
  client receives a fresh snapshot rather than delaying the terminal or another
  client. Protocol major 2 cell frames use packed RGBA8 colours plus shared attribute
  and hyperlink tables. Subscriber queues are capped at 32 events and 2 MiB, and poll
  responses budget the first event and resync snapshot as well as later events. An
  otherwise valid oversized snapshot deterministically sheds hyperlinks, then visual
  attributes, while preserving terminal text and geometry instead of wedging the
  client. Dirty-cell lists update the client grid incrementally; full rebuilds are
  reserved for full frames, resizes, and scrollback presentation transitions. Client
  input is batched and command work is bounded
  between projection polls so sustained typing cannot starve observers. Windows
  named pipes and Unix-domain sockets both serve four clients concurrently. Windows
  pipes reject remote SMB clients, retain the first pipe instance for the server
  lifetime, and use identification-level client impersonation. Runtime metadata is
  replaced atomically with current-user-only permissions, and the Windows runtime
  directory and named pipes have protected DACLs tied to the user's SID rather than
  the process owner, so elevated and non-elevated Draxul processes share one server.
  Client presence, Sessions, terminal dimensions, agent wait filters, and stale
  delivery queues are bounded. Server, topology, and agent parsing range-checks
  narrowing integers; status values and client identifiers are also bounded and
  reject control characters. Clean goodbye or lease expiry releases every terminal
  subscription and controller claim; a paused UI reattaches and retries safely.
  Topology and agent projections refresh automatically if a restarted server reports
  an earlier revision. Reconnect restores the current server state. Queued terminal
  input is retained in order across transient failures, expired control requests are
  cancelled before dispatch, and bounded request-ID caches make topology, terminal,
  and agent mutations safe to replay. Topology, agent,
  status, and terminal attachment work runs away from the render thread; projected
  panes remain responsive placeholders until their first snapshot arrives. Divider
  drags preview locally and send one trailing authoritative update rather than
  blocking the UI on every mouse move.
- A server process admits at most 256 terminal runtimes across all Sessions (tests can
  inject a lower bound). Registering a lazy terminal allocates only its small live
  grid; the configured scrollback ring is allocated after the first successful child
  process start. Topology command replay caches retain only bounded command outcomes,
  not thousands of copied topology snapshots.
- Each server terminal admits input to a bounded per-terminal queue and performs the
  potentially blocking PTY/ConPTY write on that terminal's writer thread. Saturation
  returns backpressure without delaying another terminal or the server request loop.
  PTY output readers pause at their bounded queue limit without dropping bytes, and
  live-process teardown is reaped away from the server state thread.
- The real endpoint retains bounded semantic scrollback and serves versioned pages.
  Each window owns its scroll offset, selection, clipboard copy, and cursor
  presentation, so scrolling one client does not disturb another. Shift+PageUp,
  Shift+PageDown, Shift+Home, and Shift+End navigate that local view, and keyboard
  copy mode works over both live and historical cells without sending navigation to
  the shell. Observers can still scroll and copy; attempted text or paste shows one
  Take Terminal Control hint instead of disappearing. Keyboard, focus, terminal
  mouse reporting, bracketed paste, OSC 8 links, OSC 52 clipboard writes,
  alternate-screen state, synchronized output, shell marks, title, and cwd travel
  through the remote path. Input while scrolled returns only that client to live.
  Oversized paste is sent as ordered bounded frames; invalid input, input
  backpressure, process-write rejection, and other request failures are surfaced
  without stopping a live pane. Unexpected poll failures reattach with bounded
  backoff, while a scrollback failure simply returns that client to live.
- Hello negotiation explicitly advertises scrollback, sanitized metrics, and the
  current uncompressed frame fallback. Negotiated presentation suspension stops
  terminal polling, delta encoding, hidden-grid publication, frame requests, and
  periodic UI wakeups for panes outside the active tab and Space while their
  server-owned processes, terminal cores, scrollback, topology, and controller claims
  remain live. Reactivating a tab resumes from one authoritative full snapshot. OSC 52
  clipboard writes produced while the controlling presentation is suspended are
  intentionally discarded rather than replayed into the foreground later.
  `terminal.metrics` reports active and suspended subscribers, suspension/resume and
  avoided-encoding totals, suppressed clipboard events, counts, encoded bytes, delta
  density, queue count/byte limits, pressure/resyncs, oversized events, degraded
  frames, and scrollback service volume without terminal text; the client records its
  attach/reconnect latency. Unknown additive terminal event kinds are counted and
  skipped, while malformed known events still trigger bounded recovery.
- `topology-v1` is the first Slice 6 checkpoint. The headless server now owns a
  renderer-neutral Session/Space/tab/pane/split snapshot with monotonic revisions.
  Mutations are optimistic and idempotent, and multiple clients can poll to the same
  accepted snapshot. Active Space/tab/focus, viewport, selection, and window geometry
  remain client-local. Shared-shell UIs now project server Spaces, tabs,
  panes, names, and split trees; create/close/rename actions use server commands.
  Each `client_local` descriptor creates an independent host in each UI, and live
  split reconciliation preserves unchanged hosts instead of restarting them. The
  descriptor includes the client host kind, working directory, source path, and
  optional companion owner, so file-backed hosts restore consistently across UI
  reconnects and durable Session checkpoints.
  `multi-terminal-v1` gives every `server_terminal` pane a distinct lazy server
  runtime and stable TerminalId; the real host adapter targets that identity, and
  closing the shared pane removes its endpoint and process. Tab moves, pane swaps,
  keyboard resize, cell-snapped divider drag, and split equalization now submit
  authoritative server commands and project back into every UI without moving
  client-local focus. Restarting a shared terminal pane restarts its server runtime
  exactly once, advances its runtime generation, and resynchronizes every attached
  client with the new process identity. Client-local pane restart remains local.
  A host kind missing from a particular build now projects as an inert
  `<kind> not available in this build` grid instead of preventing that client
  from attaching. Topology and agent snapshots remain pending until the UI
  acknowledges successful application by server epoch and revision; failed
  projections retry, coalesce to the newest snapshot, preserve input routing,
  and report a persistent apply error only once.
- **Headless topology and terminal control**: `draxul.exe` talks directly to the
  shared server for Space, tab, pane, split, terminal, and declarative-layout
  operations; no Draxul window is required. Mutations update the same
  server-authoritative topology projected by every attached UI. Commands accept
  `--session <id>`, `--server-runtime-dir <path>`, and `--json`. Server shells
  inherit `DRAXUL_SESSION_ID`, `DRAXUL_SPACE_ID`, `DRAXUL_TAB_ID`,
  `DRAXUL_PANE_ID`, `DRAXUL_TERMINAL_ID`, and `DRAXUL_SERVER_RUNTIME_DIR`, so an
  agent inside a pane can use `--current` and can omit its inherited Session and
  runtime route.

  | Area | Commands |
  |------|----------|
  | Spaces | `space list`, `space get/create/rename/close` |
  | Tabs | `tab list/get/create/rename/close`, `tab move --delta -1|1` |
  | Panes | `pane list/get/split/rename/close/restart/swap`, `pane move --target <pane> --direction <left|right|up|down>` |
  | Splits | `split list`, `split set --ratio <0.1..0.9>`, `split equalize` |
  | Terminal processes | `pane run --command <text>`, `pane send --text <text>`, `pane keys <keys...>`, `pane read`, `pane wait-output --text <text> --timeout <duration>` |
  | Managed agents | `agent start <profile> --space/--tab/--pane [--replace]`, `agent prompt`, `agent keys`, `agent get/list/explain/wait/restart`; `--replace` converts the selected server-terminal pane in place and preserves its pane ID |
  | Declarative layouts | `layout validate <file|->`, `layout apply <file|-> [--dry-run]` |

  Layout JSON creates one Space atomically. It contains `name`, optional `alias`
  and `root_directory`, plus non-empty `tabs`; each tab contains a name, optional
  alias, and panes. Every pane has a unique `alias` and may set `name` and `cwd`.
  Panes after the first may set `split_from` to an earlier pane alias,
  `direction` (`left`, `right`, `up`, or `down`), and `ratio`. Validation performs
  no mutation. Apply returns an `aliases` object mapping caller-chosen names to
  durable server IDs; allocation failure destroys terminals created by the
  request and restores the pre-request topology before returning an error.

---

## Rendering

- **Backends**: Vulkan (Windows), Metal (macOS)
- **Renderer target layout**: Public `draxul-renderer` API stays stable while the build internally splits shared renderer core and platform backend implementation targets
- **Architecture**: Two-pass instanced draw -- background quads then alpha-blended foreground glyphs
- **Glyph atlas**: Configurable size (default 2048x2048 RGBA8), shelf-packed, incremental upload
- **Buffer**: Host-visible/shared memory, direct writes, no staging. 112 bytes per cell
- **Frames in flight**: 2 with synchronization primitives
- **Pixel format**: BGRA8 Unorm (Neovim sends pre-sRGB colors)
- **MegaCity materials**: Textured asphalt road surfaces, paving-stone sidewalks, flat-color procedural n-gon building shell meshes with configurable roughness/metallic, bark-textured central-park trees, plus forward-lit material debug controls including metallic, tangent, bitangent, packed-TBN, directional-shadow, point-shadow, point-shadow-face, point-shadow-stored-depth, and point-shadow-depth-delta views
- **MegaCity surface pipeline**: Opaque MegaCity rendering now uses cascaded directional shadow maps, point-light cubemap shadow maps, a depth/normal AO prepass, an offscreen MSAA depth buffer, an MSAA `RGBA16F` scene color target, a resolved HDR scene texture, and a final `BGRA8 sRGB` scene texture before the main swapchain present; the debug panel can inspect the resolved HDR/final scene targets, directional shadow cascades, and point-shadow faces alongside the AO/GBuffer surfaces
- **MegaCity tone mapping controls**: The HDR post pass now applies tone mapping before the final sRGB target, with configurable `Exposure` and `White Point` controls in the Megacity lighting UI
- **Shared shader includes with a parity contract**: `shaders/include/` holds GLSL and MSL includes any mounted product may consume (currently the ACES tone-map curve, previously four hand-synced copies across MegaCity and SatView in both languages). Products reach the directory through `draxul_shared_shader_includes()` in `cmake/DraxulPlugins.cmake`, which supplies both the `-I` path for `glslc`/`xcrun metal` and the file list for shader rebuild dependencies. Each shared include has a declarative manifest under `shaders/contracts/`, and `tests/shader_abi_parity_tests.cpp` re-derives the constants, the function signature and the curve expression from both language copies and asserts they match it, so GLSL and MSL cannot drift apart
- **SatView HDR surface pipeline**: SatView scene layers render into a linear `RGBA16F` target with MSAA fallback, ACES tone mapping, and persisted exposure/white-point controls; details in [docs/features/satview.md](features/satview.md#rendering-and-data-pipeline)
- **MegaCity module surfaces**: Each non-central module now draws a thin module-colored outline above the shared road layer so module footprints are readable beneath sidewalks and buildings
- **MegaCity park dressing**: Central park now includes a procedurally generated `DraxulTree` mesh with atlas-based PBR leaf cards
- **MegaCity dependency routing**: The City Map panel now overlays routed building-to-building dependency lines driven by Tree-sitter field references and road-only semantic routing, and the same routed polylines are emitted into the 3D scene as thin raised connection strips with a directional green-to-red gradient from source to target, plus a configurable per-route layer step for stacked overlap readability
- **MegaCity semantic filters**: The City Build UI can now hide test entities and struct-backed entities before layout/build
- **MegaCity stacked struct plates**: Same-footprint structs within a module are stacked vertically into compact square-section plate buildings with configurable gap, max-per-stack, and sign colors; each plate remains independently clickable with full dependency routing and per-plate tooltips
- **MegaCity building shading controls**: The City Build UI includes `Middle Strip Push`, `Alternate Darken`, `Flat Roughness`, and `Flat Metallic` controls for non-textured procedural buildings, so flat-color shells can get configurable per-level mid-band ripples, alternating-band darkening, roughness, and metallic without affecting roads, routes, signs, or other flat overlays
- **MegaCity projection toggle**: The renderer panel can switch the MegaCity camera between `Orthographic` and `Perspective`; the choice persists in config, keeps the existing orbit/pan/zoom interactions, and also drives perspective-aware cascade splits and screen-space zoom scaling
- **MegaCity semantic snapshot**: The City Build UI builds the semantic city from the same neutral `CodeSemanticSnapshot` used by BioView. Tree-sitter scanner output is first projected into repository/module/file/type/function/method/field/reference nodes, then the city builder applies city-specific roles, building metrics, function layers, and dependency routing before layout. The old SQLite city snapshot module and Tree-sitter city adapter have been removed. Repository module boundaries are derived from paths, so `app/...`, `libs/<name>/...`, and `modules/<name>/...` appear as distinct city modules
- **BioView procedural cell**: `dev.draxul.megacity` with `{"mode":"biology"}` grows a single, anatomically-suggestive eukaryotic cell entirely from procedural geometry, replacing the earlier flat ellipsoid-cell-and-fibre projection. The cell is wider and longer than it is tall and floats above the grid so it casts a soft shadow. A double-sided translucent membrane (a noise-displaced "blob" sphere) wraps a fainter cytosol shell; inside sits a nucleus with its own translucent violet envelope, a dense nucleolus, and a four-color DNA double helix (two swept-tube backbones plus alternating base-pair rungs). Warm bean-shaped mitochondria carry cristae ridges, a curved Golgi stack of bowed cisternae sits near the membrane, a folded rough endoplasmic reticulum of swept tubes is studded with bright ribosomes, and the cytoplasm is scattered with free ribosomes, golden mRNA strands, translucent vesicles, purple lysosomes, and a perpendicular centriole pair. All parts use per-vertex-colored flat-color PBR shading through the shared cross-platform MegaCity/BioView render pass (directional + point lights, cascaded shadows, SSAO, HDR tone mapping), so Vulkan and Metal stay aligned. Geometry is generated by the plugin-owned `draxul-geometry` cell toolkit (`build_blob_mesh`, `build_dna_double_helix`, `build_mitochondrion`, `build_golgi`, `build_endoplasmic_reticulum`, `build_tube`, plus 3D value-noise and mesh transform/append helpers). Its analysis UI still exposes BioView-specific build controls and shared renderer controls rather than city/building, park, tree, sign, or road-layout sliders.
- **BioView semantic mapping**: the cell represents one **Type** (class/struct) from the Tree-sitter `CodeSemanticSnapshot` — deterministically the most significant one, `argmax(4·method_count + min(field_count,24) + 2·referenced_type_count)` with `line_count` then `qualified_name` tie-breaks (methods weighted high, field count capped so a giant plain-data config struct doesn't out-rank a real class). Its real members drive the organelles: each **method → a mitochondrion** (length from the method's line count, cristae ridges from how many distinct types it touches, warm→hot color from complexity, capped at 40 by line count); each **field → a ribosome** studded on the nuclear envelope (green-tinted if the field references another type); **every declared member → one DNA base-pair rung** in source-declaration order, four-color-coded by category (field, self-contained method, collaborator method, constructor/virtual, capped at 60); the **inheritance chain → a Golgi stack** (one cisterna per ancestor); distinct **outgoing type dependencies → vesicles**; **oversized methods (>60 lines) → purple lysosomes**; and the **constructor or busiest method → the centrosome**. Overall class health — average method length, coupling, and god-class size — tints the membrane (and DNA backbone) green→amber→red and drives membrane spikiness, so a bloated, highly-coupled class reads as an inflamed, crowded cell at a glance. Every organelle carries a `CodeVizSemanticRef` back to its semantic node (file, qualified name, node id) for future hover/pick identification. The build is fully deterministic (all placement seeded from stable hashes of member names); when the snapshot has no types it falls back to a generic decorative cell.
- **BioView tissue / organism**: biology mode grows the *whole codebase* as one organism, not just a single cell. Every **module** becomes a soft, translucent, module-colored **tissue territory** (a flattened blob patch on the floor); every **class/struct** becomes a **cell** packed into its module's tissue via phyllotaxis (sunflower) placement, with the most significant classes clustered toward each tissue's center and sized by significance; and **strong cross-module dependency coupling** (aggregated `ReferencesType`/`Inherits` edges between two modules, threshold ≥3) becomes a crimson **blood vessel** tube arcing between the two tissues, its thickness scaling with the edge count. The top classes (default 10) render as full detailed organelle cells (the mapping above); all other classes render as cheaper module-tinted "simple" cells (membrane + small nucleus, health-shifted toward red) that share meshes so hundreds stay affordable. Total cells are capped (default 640, dropping the least significant with a logged count), vessels capped (default 20). Module tissues are shelf-packed on the floor and the organism is recentered at the origin; the camera frames the whole span and a key light is positioned for the full organism. Health for simple cells is derived cheaply from the type's own line count, coupling, and member count. Everything remains deterministic. Planned follow-ups: file-level sub-clustering boundaries, honest "fat cell" / "nerve" mappings for other code shapes, level-of-detail as you zoom, and per-organelle hover tooltips in bio mode.
- **MegaCity performance preview and coverage modes**: The Codebase Analysis panel now exposes saved top-level `Perf`, `Coverage`, `LCOV Coverage`, and `Perf Log Scale` controls. `Perf` blends flat-color buildings toward a green-to-red heat palette per semantic building layer using smoothed live timing heat, while `Coverage` forces any touched/matched function layer to full heat so executed code lights up clearly. `LCOV Coverage` imports a static LLVM `lcov` tracefile from `db/coverage.lcov` or `build/coverage.lcov` and lights semantic function layers based on function-level test coverage from the LLVM coverage report — covered functions render as hot, uncovered stay at base color. The local `do.py coverage` flow exports `build/coverage.lcov` and refreshes `db/coverage.lcov` for app use. The debug panel shows LCOV-specific diagnostics (report functions, covered functions, matched/heated layers/buildings), and the building tooltip reports per-function coverage status. `Perf Log Scale` applies a visual logarithmic boost to low heat values so more active layers move toward the warm end without changing the underlying timing data. All modes are driven by a live or imported metrics snapshot for every building and function, indexed in the shader by stable building/layer ids, and accompanied by an in-panel matched/unmatched perf debug readout plus tooltip timing details for hovered functions
- **MegaCity sign sizing controls**: Building roof-sign rings can now enforce a configurable `Min Width / Char`, so long class/module labels can expand the repeated sign band instead of being squeezed into the default building footprint
- **MegaCity building shape thresholds**: The City Build UI now exposes both `Hex Threshold` and `Oct Threshold`, letting connected buildings step from 4-sided to 6-sided to 8-sided procedural shells based on total incident dependency count
- **MegaCity selection tuning**: Selection fade now has configurable dependency, hidden, hover-hidden, and road hidden alpha controls, with configurable spacebar-held raise/fall timing for hidden buildings so the shared road layer can remain fully visible while selected-context buildings read clearly
- **SatView rendering and data pipeline**: the Earth/Moon/Sun/planet passes, surface-object and sky-orientation overlays, catalog/propagation services, sun-synchronous filter, and dock panels are documented in [docs/features/satview.md](features/satview.md#rendering-and-data-pipeline)
- **Native network transport**: Weather, SatView catalog, and live-cloud downloads use a shared bounded HTTP client backed by WinHTTP on Windows and `NSURLSession` on macOS. Requests have explicit connection and overall deadlines, per-service response-size limits, RFC 3986 query encoding, and cancellation before worker joins, so runtime networking no longer requires `curl` or passes URLs through a command shell. Weather responses are parsed as typed JSON and reject missing, non-finite, wrong-type, or out-of-range values.
- **Markdown viewer pipeline**: Markdown panes are rendered by Draxul itself rather than through the terminal grid or ImGui. The host parses Markdown into document blocks, lays them out as variable-height rows, builds a GPU draw list of styled rectangles and glyph runs, uploads rich-text atlas regions incrementally, and renders directly through the platform hardware renderer. Inline `**bold**` and `*italic*` emphasis render with real bold/italic faces (including inside table cells and headings), each authored newline inside a block starts exactly one new visual row (Obsidian-style line handling, rather than CommonMark reflow) with no blank row between, and task-list markers draw as scalable □/✓ glyphs tinted with the theme accent. GitHub/Obsidian pipe tables render with header/body styling, cell borders, wrapped cell text, left/center/right column alignment, and content-aware column widths that balance required and preferred cell sizes. Markdown body size is controlled independently through `[markdown].font_size` (defaulting to one point below the global `font_size`), headings scale relative to it, focused Markdown panes consume `font_increase`, `font_decrease`, and `font_reset`, and `[markdown].margin_columns` controls the document margin in body character widths. Navigation supports PageUp/PageDown/Home/End, wheel scrolling, Vim-style `j/k`, `Ctrl+F/B`, `gg`, `G`, and mouse dragging on the wider scrollbar thumb.

## GUI (draxul-gui)

A standalone GUI library for rendering UI items that do not depend on ImGui. It leverages the project's font engine and GPU renderer for high-performance, pixel-precise overlays.

- **Tooltips**: Multi-line tooltips with a semi-transparent dark background and a 2-column table layout for labels and values. Rasterized on-demand via `TextService` and rendered as a screen-space alpha-blended quad.
- **Toast notifications**: Auto-dismissing notifications stacked at the bottom-right corner via `ToastHost` (info/warn/error levels with distinct colors and fade-out animation). Thread-safe `push()` and `IHostCallbacks::push_toast()` lets any host or app subsystem report recoverable failures (clipboard errors, font fallback warnings, unknown config keys, secondary host spawn failures, invalid pane targets) without blocking the user. Toasts pushed before the host exists during init are queued and replayed.
- **Shaders**: Generic `gui_tooltip.vert/frag` (Vulkan) and `gui.metal` (Metal) for rendering GUI elements.

---

## Font Pipeline

- **FreeType** loads faces, **HarfBuzz** shapes text, glyph cache rasterizes on demand
- **Ligatures**: Programming ligatures via HarfBuzz (configurable, default on); supports multi-cell ligatures up to 6 cells (e.g. `===`, `!==`, `>>=`, `<<=`), with correct highlight-boundary breaking. Ligature spans cover only the cells whose shaping actually changed, cluster glyphs are pinned to grid-cell pitch, and edits regroup the whole shaping run — so incremental typing produces pixel-identical output to a full repaint
- **Multi-weight**: Bold, italic, bold+italic via separate font files
- **Fallback chain**: Primary font + configurable fallback paths for missing glyphs. macOS defaults include STIX Two Math for technical symbols (e.g. `⏵` U+23F5) absent from Apple Symbols
- **Synthesized box drawing**: Box Drawing (U+2500–257F) and Block Elements (U+2580–259F) are drawn procedurally at exact cell size instead of rasterized from the font, so adjacent cells tile seamlessly at any size/DPI (no anti-aliased gaps in TUI borders, progress bars, or logos)
- **Emoji**: Color glyph rendering, variation selectors (VS-16), ZWJ sequences
- **Wide characters**: CJK double-width, combining characters
- **Bundled fonts**: JetBrains Mono Nerd Font (regular/bold/italic/bold-italic), Cascadia Code
- **Rich text service**: Markdown viewing can resolve separate point sizes and bold/italic style keys through pooled `TextService` instances, enabling larger heading rows without forcing the terminal grid to adopt variable-sized cells.
- **Per-display DPI**: moving the window between displays with different scale factors re-initialises font metrics (SDL display-scale-changed events), so text stays sharp on mixed-DPI setups

---

## Terminal Emulation (shell hosts)

- **VT100+** escape sequence support (ANSI/256/24-bit SGR colors, cursor control, DECSTBM scroll regions, DECAWM auto-wrap `DECSET 7`, DECOM origin mode `DECSET 6`)
- **Scrollback**: Configurable row ring buffer with viewport offset (default 10000)
- **Alt screen**: Main/alt switching (`DECSET 1049`) with snapshot restore; if the window is resized while in alt-screen, the saved content is re-dimensioned before restore
- **Mouse modes**: None, button-click (`DECSET 1000`), drag (`DECSET 1002`), all-motion (`DECSET 1003`), SGR encoding (`DECSET 1006`)
- **xterm focus reporting**: DECSET `?1004` emits `CSI I` / `CSI O` on pane focus gain/loss
- **DEC special graphics / ACS**: `ESC ( 0`, `ESC ) 0`, `SO`, and `SI` map VT line-drawing characters to Unicode box-drawing glyphs
- **Bracketed paste**: VT-wrapped clipboard paste (`DECSET 2004`)
- **Paste confirmation**: Pastes ≥ `paste_confirm_lines` newlines stash the payload and surface a toast; `confirm_paste` (default `Ctrl+Shift+Enter`) sends it, `cancel_paste` (default `Ctrl+Shift+Escape`) discards it. Set `paste_confirm_lines = 0` to disable
- **OSC 7**: Current working directory tracking from shell
- **OSC 8**: Terminal hyperlink regions are tracked per grid cell, underlined, and open on click
- **OSC 52**: Clipboard read (`?` query) and write (base64 payload) for tmux/SSH/Neovim remote clipboard integration
- **URL detection**: HTTP/HTTPS text is underlined and can be opened with Ctrl/Cmd-click; explicit OSC 8 hyperlinks take priority
- **Shell TERM identity**: Unix PTY shell hosts advertise `TERM=xterm-256color`, `COLORTERM=truecolor`, and `TERM_PROGRAM=draxul`
- **Selection**: Click-and-drag with system clipboard integration; configurable cell cap (`selection_max_cells`, default 65536)
- **Word/line selection**: Double-click selects the word at the cursor (contiguous non-whitespace), triple-click selects the entire row
- **Selection copy gestures**: Clicking inside an existing mouse selection copies it to the system clipboard; `Ctrl+C` also copies when a shell-pane mouse selection is active, without sending SIGINT to the process
- **Copy on select**: `copy_on_select` automatically copies completed mouse selections (drag, double-click, or triple-click) to the system clipboard; enabled by default
- **Keyboard copy mode**: `toggle_copy_mode` (default `Ctrl+S, Return`) enters a vim/tmux-style cursor: `h/j/k/l` and arrows move, `0/Home/End` jump to line bounds, `g/Shift+G` jump to top/bottom, `v`/`V` start char/line selection, `y` yanks to clipboard and exits, `Esc`/`q` exits without copy. Available on both client-owned and server-owned shell hosts (including an observer's local scrollback view); Neovim panes already provide their own visual mode
- **Terminal colors**: Configurable foreground/background via `[terminal]` config section
- **Renderer-free terminal state**: Local PowerShell, Bash, and Zsh hosts all compose
  the same platform-neutral terminal core for VT parsing, grid/mode state,
  alternate-screen handling, reusable scrollback storage, and complete or dirty
  semantic snapshots. The current process, selection, clipboard, and rendering path
  remains local; this is the compatibility boundary for the planned server runtime.

---

## Input

- **Keyboard**: Full SDL3 key events with modifier tracking (shift, ctrl, alt, super)
- **IME**: Text input + text editing event forwarding
- **Mouse**: Button, motion, wheel with per-host protocol routing
- **MegaCity camera**: Left-drag in the render view pans the scene, `Alt` + left-drag scrubs orbit
- **SatView camera/map/ground**: globe orbit/dolly, map panning, ground-view rotation, and the keyboard equivalents are documented in [docs/features/satview.md](features/satview.md#input-camera-map-and-ground-view)
- **Smooth scroll**: Trackpad momentum accumulation (configurable speed multiplier)
- **File drop**: Native drag-and-drop dispatched to host as `open_file:` action
- **Kanban navigation**: Kanban panes support Vim-style card selection with `h/j/k/l`, `Ctrl+F/B` page jumps, `gg`/`G` beginning/end jumps within the current column, shifted up/down arrows for reordering cards, `<`/`>` for moving files between columns, `r` reload, and Enter to open the selected card's Markdown file for editing in a Neovim host.
- **Kanban column zoom**: `z` collapses the board to just the selected column at full width (moving left/right pages between columns while zoomed); `z` again restores the multi-column view.
- **Kanban card preview**: `p` pins a live Markdown preview pane across the bottom third of the board that always renders the currently selected card; it follows the selection as you move and Enter keeps input focus on the board so the preview and the board stay in view together. In shared topology, the server owns the preview split and source descriptor so every connected UI projects it and reconnect restores it. `p` again closes the preview.
- **GUI keybindings**: Chord-style prefix bindings (e.g. `ctrl+s, |`)
- **Command palette**: `Ctrl+Shift+P` opens a centered fuzzy-search overlay for all GUI actions with fzf-style scoring, `Ctrl+J/K` navigation, keybinding hints, and palette-rendered text prompts for actions needing short values
- **Print pane** (`print_pane` action, palette or `[keybindings]`): captures the focused pane's pixels, composes a single-page A4 PDF (aspect-fit inside margins, auto landscape for wide panes, CoreGraphics), and presents the native macOS print dialog for it (PDFKit print operation: preview, printer/paper choice, and auto-rotation so landscape pages land correctly on portrait paper); toasts report printed/canceled/failed. Hosts advise the printer via `IHost::print_hint()` — a pane-relative content rect plus a paper-white flag — so ScoreView prints just the page/band (no backdrop border) with its warm screen sheet tint snapped to pure white instead of printed stipple. macOS-only for now. `DRAXUL_PRINT_DRY_RUN=1` composes the PDF but skips the dialog and toasts the temp path (test hook)
- **`--gui-action <name>` CLI test hook**: with `--screenshot`, pumps until content is ready, dispatches any canonical GUI action by name, then captures — lets headless runs exercise palette actions and verify their toasts/effects
- **Config reload**: `reload_config` rereads `config.toml` on demand so palette alpha, keybindings, scroll settings, ligatures, terminal font changes, and Markdown font/margin changes can be applied without a restart

---

## Split Panes

- Binary split tree with vertical and horizontal splits
- Invisible four-pixel split gutters with ratio-based sizing — hovering a gutter switches the mouse cursor to the platform EW/NS resize cursor; click-and-drag updates the ratio in real time without drawing a divider line
- Per-pane host instance with independent lifecycle
- Focus tracking and pane-aware input routing
- Each pane leaves a four-pixel margin before its full rectangular focus frame. Window-facing edges add another two pixels while pane-to-pane edges stay unchanged, balancing the doubled margins at split joins without widening those joins. The host viewport follows the same edge-aware insets, keeping the configured red active frame (or subtle grey inactive frame) clear of both the pane edge and its content.
- Pane status uses one cell-high pill band. Any fractional terminal-row tail is painted with the host background, so it remains visually part of the content instead of making the status band look oversized.
- Keyboard-driven pane focus navigation (`Ctrl+H/J/K/L` vim-style) via `focus_left`, `focus_right`, `focus_up`, `focus_down` actions
- Keyboard-driven pane resizing via `resize_pane_left`, `resize_pane_right`, `resize_pane_up`, `resize_pane_down` actions (each nudges the nearest enclosing divider by 5%)
- **Pane zoom**: `toggle_zoom` action (default `Ctrl+S, z`) expands the focused pane to fill the full window; toggling again restores the previous split layout exactly (like tmux `Ctrl+B z`)
- **Close pane**: Closes the focused pane and its host; if last pane, exits the app
- **Server-owned Session persistence**: the headless server periodically checkpoints every Session, Space, tab, pane, split, name, working directory, restore policy, and agent reference. Closing every UI leaves that topology and its terminal processes running; graceful server shutdown writes a final checkpoint, and the next server cold-restores every usable Session.
- **One Session, many clients**: multiple Draxul windows can attach to the same Session without duplicating processes or competing file writers. Topology and terminal state are authoritative in the server, while each client retains independent navigation and presentation state.
- **Session-scoped CLI**: `--session <id>` selects a shared server Session, `--new-session` creates a fresh one (generating a unique id when omitted), and `--session-name <name>` sets its display name. `--list-sessions`, `--rename-session --session-name <name>`, `--delete-session --session <id>`, and `--delete-all-sessions --yes` all address the running server registry. Deletion refuses while a UI is attached; stopping live terminals requires explicit confirmation.
- **Abnormally exited shell panes stay inspectable**: If a shell pane dies unexpectedly, Draxul keeps the pane and its last rendered output visible instead of immediately tearing it down. The pane status pill shows `[exited]`, a toast points you at `restart_host`, and the existing restart action respawns the host in place. Clean shell exits still close the pane normally.
- **Session startup messaging**: Shell sessions surface a toast when Draxul starts a brand-new session or restores saved topology, so the user can tell which path was taken.
- **Restart host**: Kills the current host in the focused pane and relaunches with the same arguments
- **Swap pane**: Swaps the focused pane with the next pane in spatial order

---

## Spaces

- The live hierarchy is **Session -> Space -> Tab -> Pane**. A Space is a local project/task container with its own tabs, split layouts, hosts, and default root directory.
- A server-owned Session can contain multiple live Spaces. Each UI chooses its
  active Space independently while the server continues to own every inactive
  Space's terminals and agents.
- The left rail appears once a second Space or a tracked agent exists. Its upper Spaces section uses the shared segmented pill component (`1: Name`): every Space pill has a one-third-brightness palette-blue body, while the selected Space's number segment uses the bright blue. The first Space row follows its header with the same compact spacing used by Agents. Click a pill to activate it. A horizontal application-shell divider separates the lower Agents section. Agent rows are derived from pane-owned identities across all Spaces, use their own mauve family body and bright focused/attention number accent, show `[exited]` when their host is unavailable, and navigate to the owning Space, tab, and pane when clicked. The rail background uses the same dark-grey chrome colour as the surrounding UI and default console background. Drag the rail's right-hand divider to resize it; the width snaps to terminal columns and is retained across launches.
- `new_space`, `switch_space`, `rename_space`, and `close_space` are available in the command palette. They are unbound by default.
- `launch_agent` is available in the command palette and unbound by default. It
  opens a profile picker with built-in Codex and Claude entries plus structured
  `[agents.profiles.<id>]` configuration. The server resolves the profile,
  creates the server-owned terminal, injects Session/Space/tab/pane/runtime
  routing, persists the launch descriptor and identity, and projects the new
  pane/agent into every client under `managed-agent-v1`. The launching UI gets
  the initial controller lease; another client may explicitly take control.
  Codex and Claude can install opt-in, versioned `SessionStart` hooks with
  `draxul integration install codex|claude`; each hook reports the official
  native conversation ID to the owning server pane. Bare
  `draxul integration status` inspects both integrations without modifying configuration.
- `focus_agent`, `restart_agent`, and `clear_agent_identity` are also available in the command palette. Runtime generations and process exit codes are kept in memory, so restarting an agent cannot make an earlier process look current and failed/exited agents remain visible and inspectable in the rail.
- Server terminal runtimes expose bounded bottom-of-screen and process evidence
  to the server agent tracker. Bundled Codex and Claude manifests conservatively
  project `idle`, `working`, `blocked`, and `done`; ambiguous output remains
  `unknown`. The Agents rail shows semantic state and client-local attention.
  `explain_agent_state` reports only sanitized evidence, never captured text.
- Codex and Claude started manually inside ordinary shell panes are discovered
  best-effort by the server. Process inspection runs outside the server's
  terminal/control loop and publishes a cached observation at one-second
  projection cadence. Unix uses the PTY foreground process group, re-probing on
  group/output changes with slow reconciliation. Windows uses each ConPTY's Job
  Object process notifications, coalesces changes for one second, and queries
  that job's current PIDs. A background descendant-tree reconciliation covers
  nested-job and breakaway children that Windows does not report through the
  ConPTY job. Reconciliation runs every five seconds while an inferred agent is
  present or every thirty seconds otherwise. Managed panes skip process
  discovery because their identity is authoritative. Detection sees through
  structured launchers and accepts `DRAXUL_AGENT=codex|claude` as an explicit
  hint. Inferred rows are not given durable native-session references.
- Terminal output and queued control work wake the server loop immediately;
  otherwise it sleeps for up to one second between housekeeping passes. This
  removes the old 25 ms control delay and polling cadence without delaying
  interactive terminal publication.
- The shared server exposes its authenticated same-user local control endpoint.
  The headless topology/terminal commands above provide bounded discovery and
  manipulation by stable route ID and Session; structured agent operations
  start (including in-place pane replacement), restart, prompt, send bounded
  keys, and wait on a pinned runtime generation. Sanitized agent events never
  include terminal text; terminal text is available only through explicit pane
  reads and output waits.
- Shared server Sessions expose the sanitized Agents projection to every
  attached UI. Agent focus and attention acknowledgement remain local to each
  window. `agent list/get/explain/wait/restart/send-text/send-keys` and bounded
  `pane read` commands address the global server for the selected Session, so
  inspection and control continue with no GPU client attached. This route is
  negotiated as `agent-control-v1`; terminal text is returned only by the
  explicit bounded pane-read operation and is never included in the Agents
  projection.
- Official native-session reports for managed agents are owned by the
  global server. A report must match the current server epoch, Session, pane,
  declared agent instance, kind, and runtime generation; stale, duplicate, or
  out-of-order reports are rejected. Accepted references update shared topology,
  the Agents projection, and the durable Session checkpoint even when no UI is
  attached.
- A new Space inherits the focused host's current working directory when possible. Its root directory becomes the fallback working directory for new hosts in that Space.
- Closing a Space terminates the hosts it owns. The final Space cannot be closed.
- Spaces are authoritative server topology. Server checkpoints use a version-3
  Space envelope, migrate older schema versions in memory, atomically persist
  the ordered Space collection, and transactionally restore every usable Space.
  Suspend/resume and remote-machine transport remain future work.
- Server recovery is bounded before processes launch: checkpoints are limited
  to 4 MiB, 64 Spaces, 128 tabs per Space, 256 panes per tab, 64 layout levels,
  and bounded text/list fields. Diagnostics identify invalid fields without
  echoing commands or paths.
- A successful server checkpoint atomically replaces the previous snapshot.
  Draxul does not currently maintain a second `.bak` copy; corrupt checkpoints
  are archived before checkpointing resumes.
- Agent runtime state, semantic observations, explanations, attention latches, and sidebar rows are projections, not persisted state. The pane-owned agent identity, restore policy, and optional official native session reference are durable; live status and terminal evidence never enter a Session snapshot. Native references are bounded, source-allowlisted, and globally unique across restored Spaces.

---

## Tabs

- Multiple tabs, each with its own independent split tree and host set
- Space, Agent, tab, and pane-status labels share one pill layout and palette model for capsule size, number accent width, text columns, foreground contrast, and active/inactive/editing colours. Each collection keeps a 30%-brightness version of its unchanged role colour across every pill; the selected/focused number segment uses the brighter role colour (Space blue, Agent mauve, tab red, pane green).
- The top tab bar remains visible even with a single tab and shows right-aligned pills for live system usage and active chord prefixes
- `new_tab` (`Ctrl+S, C`): Create a new tab
- `close_tab` (`Ctrl+S, &`): Close the active tab (disabled when only one tab remains)
- `next_tab` (`Ctrl+S, N`): Cycle to the next tab
- `prev_tab` (`Ctrl+S, P`): Cycle to the previous tab
- Tab switching preserves focus state per tab (focus lost/gained notifications)
- **Inline Space and tab rename**: double-click a Space or tab pill (or use the corresponding command-palette action; tabs also support `Ctrl+S, ,`) to edit its name in place. Enter commits, Escape cancels, Backspace/Delete/Home/End/Left/Right work as expected. Empty commits leave the existing name untouched.
- **OSC 7 default naming**: shell hosts (e.g. zsh) drive the tab name from the OSC 7 working-directory escape until the user explicitly renames the tab; once the user sets a name, OSC 7 updates no longer overwrite it.
- **Stable pane labels and inline rename**: pane pills show a custom pane name when set, otherwise the stable host or shell name (`PowerShell`, `Zsh`, `Neovim`, and so on). Live remote-controller role, terminal size, and connection timing remain diagnostics and no longer cause pane labels to change. Double-click a pane pill (or press `Ctrl+S, .`) to set an override; an empty commit clears it. Pane name overrides follow the stable pane identity and are included in Session snapshots.
- **Luminance-based pill text colour**: tab and pane pill text colour is chosen automatically from the underlying NanoVG fill via BT.709 relative luminance, so any future background tweak gets a readable foreground without re-tuning a constant.

---

## Diagnostics Panel (ImGui)

Toggle with F12. Shows:

- Display DPI, cell size, grid dimensions, dirty cell count
- Frame timing (current + average)
- Atlas usage ratio and glyph count
- Startup profiling step timings
- MegaCity renderer controls, including module filtering (`All Modules` or a selected module), a `Point Shadow Debug Scene` toggle, debug views (`Final Scene`, `Ambient Occlusion`, `Normals`, `World Position`, `Roughness`, `Metallic`, `Albedo`, `Tangents`, `UV`, `Depth`, `Bitangents`, `TBN Packed`, `Directional Shadow`, `Point Shadow`, `Point Shadow Face`, `Point Shadow Stored Depth`, `Point Shadow Depth Delta`), tone-mapping controls, AO tuning, shadow-map inspection, and configurable connected-building hex/oct thresholds
- MegaCity sign styling controls, including separate module-sign and building-sign board/text colors
- MegaCity central-park tree controls, including age, seed, branch depth/count, curvature, trunk/branch wander, bend frequency/deviation, leaf density/orientation randomness, leaf size range, leaf start depth, bark colors, and atlas-based leaf cards with PBR normal/roughness/opacity/scattering textures

---

## Default Keybindings

| Action | Default Binding |
|--------|-----------------|
| `toggle_diagnostics` | `F12` |
| `toggle_host_ui` | `F1` |
| `copy` | `Ctrl + Shift + C` |
| `paste` | `Ctrl + Shift + V` |
| `font_increase` | `Ctrl + =` |
| `font_decrease` | `Ctrl + -` |
| `font_reset` | `Ctrl + 0` |
| `split_vertical` | `Ctrl + S, Shift + \` |
| `split_horizontal` | `Ctrl + S, -` |
| `command_palette` | `Ctrl + Shift + P` |
| `quit` | `Ctrl + S, Q` |
| `new_space` | (unbound) |
| `switch_space` | (unbound) |
| `rename_space` | (unbound) |
| `close_space` | (unbound) |
| `edit_config` | (unbound) |
| `reload_config` | (unbound) |
| `toggle_zoom` | `Ctrl + S, Z` |
| `close_pane` | `Ctrl + S, X` |
| `restart_host` | `Ctrl + S, R` |
| `swap_pane` | `Ctrl + S, O` |
| `focus_left` | `Ctrl + H` |
| `focus_down` | `Ctrl + J` |
| `focus_up` | `Ctrl + K` |
| `focus_right` | `Ctrl + L` |
| `resize_pane_left` | `Ctrl + S, Left` |
| `resize_pane_right` | `Ctrl + S, Right` |
| `resize_pane_up` | `Ctrl + S, Up` |
| `resize_pane_down` | `Ctrl + S, Down` |
| `open_file_dialog` | (unbound) |
| `new_tab` | `Ctrl + S, C` |
| `close_tab` | `Ctrl + S, &` |
| `next_tab` | `Ctrl + S, N` |
| `prev_tab` | `Ctrl + S, P` |
| `rename_tab` | `Ctrl + S, ,` |
| `rename_pane` | `Ctrl + S, .` |
| `confirm_paste` | `Ctrl + Shift + Enter` |
| `cancel_paste` | `Ctrl + Shift + Escape` |
| `toggle_copy_mode` | `Ctrl + S, Return` |
| `test_toast` | (unbound) |

Customizable in `config.toml` under `[keybindings]`. Chord syntax: `"prefix, key"`. Set to empty string to unbind. The font actions adjust the focused Markdown pane when it accepts them; otherwise they adjust the shared terminal/grid font.

Key syntax: modifiers `Ctrl`/`Control`, `Shift`, `Alt`, `Super`/`Meta`/`Gui` (case-insensitive), combined with `+` (e.g. `"Ctrl+Shift+V"`). Symbol aliases: `=`/`equals`, `-`/`minus`, `+`/`plus`, `|`/`pipe`. Any other key uses its SDL key name (`F1`--`F12`, `Tab`, `Return`, `Escape`, `Space`, `Home`, `End`, `PageUp`, `PageDown`, arrow keys, ...).

---

## Configuration (config.toml)

Draxul reads `config.toml` on startup and creates it with defaults on first save if it does not exist.

| Platform | Path |
|----------|------|
| Windows  | `%APPDATA%\draxul\config.toml` |
| macOS    | `~/Library/Application Support/draxul/config.toml` |
| Linux    | `$XDG_CONFIG_HOME/draxul/config.toml` (falls back to `~/.config/draxul/config.toml`) |

### Display

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `window_width` | 1280 | 800--8000 | |
| `window_height` | 800 | 600--8000 | |

### Font

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `font_size` | 11.0 | 6.0--72.0 | Points; 0.5pt step on increase/decrease |
| `space_sidebar_columns` | 20 | 12--48 | Preferred width of the multi-Space navigation rail in terminal columns |
| `font_path` | (bundled) | | Primary font file path |
| `bold_font_path` | (none) | | Bold variant |
| `italic_font_path` | (none) | | Italic variant |
| `bold_italic_font_path` | (none) | | Bold + italic variant |
| `fallback_paths` | [] | | Array of fallback font paths |
| `enable_ligatures` | true | | Programming ligature combining |

### GUI

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `palette_bg_alpha` | 0.9 | 0.0--1.0 | Command palette background opacity; clamped |
| `focus_border_width` | 3.0 | 1.0--10.0 | Focused-pane border thickness in pixels; clamped |
| `weather_location` | (empty) | | Weather pill: a city name (`"York, UK"`) or `lat,lon` (`"53.96,-1.08"`) shows the current temperature in the top-right chrome bar; empty disables |

### Markdown (`[markdown]` section)

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `font_size` | `font_size` - 1.0 | 6.0--72.0 | Markdown body text size in points. If `[markdown]` is omitted, it follows the global `font_size` one point smaller (prose reads better than terminal text at a slightly reduced size); headings and other markdown styles scale relative to this value. |
| `margin_columns` | 2.0 | 0.0--24.0 | Left/right document margin measured in Markdown body character widths |

### Rendering

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `atlas_size` | 2048 | 512--4096 | Must be power of 2 |

### Scrolling

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `smooth_scroll` | true | | Trackpad momentum accumulation |
| `scroll_speed` | 1.0 | 0.1--10.0 | Multiplier; out-of-range logs WARN and resets to 1.0 |
| `scrollback_lines` | 10000 | 1--1000000 | Shell-host scrollback capacity; out-of-range logs WARN and resets to default |

### Notifications

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `enable_toast_notifications` | true | | Master switch for toast overlay |
| `toast_duration_s` | 4.0 | 0.5--60.0 | Seconds each toast remains on screen before fading |
| `chord_timeout_ms` | 1500 | `>= 100` | How long a chord prefix stays armed while waiting for the next key |
| `chord_indicator_fade_ms` | 2500 | `>= 100` | How long the top-bar chord indicator takes to fade after a chord completes or times out |

### Agents (`[agents]` section)

| Key | Default | Notes |
|-----|---------|-------|
| `resume_on_restore` | false | Allow current official integrations to resume a saved native conversation as a new local process |
| `profiles.<id>.restore_policy` | `resume_if_available` | `fresh`, `resume_if_available`, or `shell_only`; `shell_only` always prevents native resume |

### Pane Status Bar

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `show_pane_status` | true | | One-cell-tall pane label strip showing the custom name or stable host/shell kind |

### MegaCity (`[mega_city_code]` section)

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `code_source` | `treesitter_db` | `treesitter_db` | Legacy source selector; stale values such as `graphify` load as the direct Tree-sitter source and are rewritten as `treesitter_db` when MegaCity saves config |

### Terminal (`[terminal]` section)

| Key | Default | Range | Notes |
|-----|---------|-------|-------|
| `fg` | `#eaeaea` | | Hex color (3 or 6 digit) |
| `bg` | `#141617` | | Hex color (3 or 6 digit) |
| `selection_max_cells` | 65536 | 256--1048576 | Maximum cells in a single selection before truncation |
| `copy_on_select` | true | | Auto-copy completed selections to the system clipboard |
| `paste_confirm_lines` | 5 | 0--100000 | Pastes with this many lines or more require `confirm_paste`. `0` disables |
| `url_detection` | true | | Detect HTTP/HTTPS URLs in grid text and make them clickable with Ctrl/Cmd-click |
| `enable_osc8_hyperlinks` | true | | Enable OSC 8 terminal hyperlink regions |
| `enable_shell_integration_marks` | true | | Track OSC 133 shell-integration marks (prompt/command/output boundaries with exit codes) emitted by supporting shells |

### Chrome (`[chrome]` section)

All values are hex colors in `#RRGGBB` or `#RGB` form. Omitted keys keep the built-in Catppuccin Mocha-inspired defaults.

| Key | Default | Notes |
|-----|---------|-------|
| `tab_bar_bg` | `#161616` | Application chrome background: tab bar, Spaces rail, and pane gutters |
| `tab_active_fg` | `#f5e0dc` | Active tab label text |
| `tab_inactive_fg` | `#cdd6f4` | Inactive tab label text |
| `space_active_bg` | `#89b4fa` | Active Space number/accent fill |
| `agent_active_bg` | `#cba6f7` | Focused or attention Agent number/accent fill |
| `tab_active_bg` | `#b93c3c` | Active tab number/accent fill |
| `tab_inactive_bg` | `#45475a` | Secondary neutral chrome fill for headers and outlines |
| `tab_editing_bg` | `#8c90af` | Tab rename field fill |
| `divider` | `#78788c` | Spaces/Agents section divider |
| `focus_border` | `#7b2828` | Focused border when a tab has multiple visible panes |
| `status_bar_bg` | `#45475a` | Legacy pane status body setting retained for config compatibility |
| `status_bar_fg` | `#cdd6f4` | Pane status text |
| `status_focused_accent_bg` | `#3ca55f` | Focused pane status number/accent fill |
| `status_inactive_accent_bg` | `#6e738c` | Legacy inactive-pane accent retained for config compatibility |
| `status_editing_bg` | `#8c90af` | Pane rename field fill |
| `resource_pill_bg` | `#f9e2af` | Normal CPU/RAM pill fill |
| `resource_pill_fg` | `#1a1a1f` | CPU/RAM pill text |
| `resource_pill_warn_bg` | `#f5c282` | CPU/RAM warning fill |
| `resource_pill_hot_bg` | `#f45656` | CPU/RAM hot fill |
| `chord_pill_bg` | `#45475a` | Active chord indicator fill |
| `weather_pill_bg` | `#474d61` | Weather pill fill |
| `editing_outline` | `#ffffff` | Rename caret and outline |

---

## CLI Flags

`draxul --help` enumerates the public launch, server/Session, Space, agent,
pane-inspection, and Codex/Claude integration commands. Internal test hooks and
server helper invocations are deliberately omitted. Subcommands are dispatched
through their own option grammars, so invocations such as `draxul agent list`
and `draxul integration status` do not pass through the launch-option parser.

| Flag | Description |
|------|-------------|
| `--host <type>` | Core host type: nvim, markdown, kanban, powershell, bash, zsh, wsl |
| `--plugin <id>` | Launch the primary pane as a product plugin (e.g. `dev.draxul.scoreview`); cannot be combined with `--host`, and a plugin that fails to load fails startup instead of degrading to a placeholder pane |
| `--plugin-config <json>` | Configuration JSON passed to the `--plugin` instance (ScoreView accepts `source`, `mode`, `background_playback`) |
| `--command <cmd>` | Override host command path |
| `--source <path>` | Markdown file for `--host markdown`; product plugins carry sources in `--plugin-config` JSON |
| `--session <id>` | Select which saved shell session to restore |
| `--new-session` | Start a fresh saved shell session; if `--session` is omitted Draxul generates a unique session id. If the requested session cannot be prepared (for example an explicit `--session` id that already exists) Draxul reports the error and exits rather than silently falling back to `default` |
| `--session-name <name>` | Set the saved display name for the launched or restored shell session |
| `--rename-session` | Rename the selected Session in the running shared-server store using `--session-name <name>` |
| `--list-sessions` | Query the running shared-server store and print its Session names and status rows |
| `--delete-session --session <id>` | Delete a detached server Session and its checkpoint; add `--yes` if it owns live terminals |
| `--delete-all-sessions --yes` | Stop every detached Session's live terminals and delete every shared-server Session checkpoint; refuses while any UI is attached |
| `--continuous-refresh` | Let animation/3D hosts request frames continuously; use `--no-vblank` separately when unsynced presentation is desired |
| `--log-file <path>` | Write logs to file |
| `--log-level <level>` | Minimum level: error, warn, info, debug, trace |
| `--pty-capture-file <path>` | Capture raw terminal drain chunks to a replayable PTY log for terminal debugging |
| `--console` | (Windows) Allocate debug console window |
| `--smoke-test` | Non-interactive startup test, exits after 3s |
| `--render-test <file>` | Run render test scenario (requires DRAXUL_ENABLE_RENDER_TESTS) |
| `--bless-render-test` | Update reference image from test output |
| `--show-render-test-window` | Show window during render test |
| `--export-render-test <file>` | Export captured frame to BMP |

---

## Build

### Prerequisites
- CMake 3.25+
- Windows: Visual Studio 2022, Vulkan SDK (with glslc)
- macOS: Xcode Command Line Tools (Metal compiler)

### CMake Presets

| Preset | Platform | Description |
|--------|----------|-------------|
| `default` | Windows | Debug, VS 2022 x64 |
| `release` | Windows | Release |
| `win-ninja-debug` | Windows | Debug, Ninja single-config local-iteration build in `build-ninja-debug/` |
| `win-ninja-release` | Windows | Release, Ninja single-config local-iteration build in `build-ninja-release/` |
| `win-ninja-relwithdebinfo` | Windows | RelWithDebInfo, Ninja single-config local-iteration build in `build-ninja-relwithdebinfo/` |
| `mac-debug` | macOS | Debug |
| `mac-release` | macOS | Release |
| `mac-asan` | macOS | Debug + AddressSanitizer + UBSan |
| `mac-tsan` | macOS | Debug + ThreadSanitizer (mutually exclusive with ASan) |
| `mac-coverage` | macOS | Debug + LLVM coverage |
| `clang-tools` | macOS | Ninja, compile_commands.json only |

### Convenience Scripts

- `do build`, `do run`, and `do test` use one shared build-selection path. They default to Debug and Ninja on Windows, reuse `build-ninja-debug/`, and accept `debug` / `release` plus `--vs` / `--ninja` without silently switching generators between commands
- `do run relwithdebinfo` / `do build relwithdebinfo` use `RelWithDebInfo` on Windows for optimized builds with PDB symbols
- `do run --vs` falls back to the Visual Studio generator if you want the existing `build/` workflow
- `do run --ninja` forces the Ninja local-iteration path explicitly
- `do test` builds `draxul-tests-core` and its helper/dependency targets in the selected `do.py` cache, then runs the core, app, Markdown/Kanban, and Python workflow unit entries through CTest with bounded parallelism. It does not launch the app or run smoke/render snapshots
- Product unit suites are opt-in and additive: `do test --megacity`, `--satview`, or `--scoreview` adds only that product's aggregate and CTest entries; `--products` adds all three for shared plugin SDK/support/renderer changes; `--all` builds the historical `draxul-tests` aggregate and runs the complete unit inventory
- `do clean` recursively removes repository-root build directories named `build/` or `build-*`, covering Visual Studio, Ninja, tooling, and custom build trees. It succeeds when none exist and preserves deploy packages, render outputs and references, databases, source files, and similarly named regular files
- `do hygiene` fails (exit 1) if a forbidden artifact is tracked — OS/coverage temps (`.DS_Store`, partial-transfer `.!*`, `*.profraw`, `*.profdata`) anywhere, or `key.txt` / `NUL.obj` / `megacity-linux-drivers-mesh.bmp` / stray `*.log`, `*.obj`, `*.bmp` at the repo root — or if the feature docs have duplicated (`docs/features.md` must exist and root `FEATURES.md` must stay a short pointer, not a second inventory). Legitimate nested assets (mesh `*.obj`, render-reference `*.bmp`) are allowed
- `do kanban-report` reads `kanban/` as the authoritative tracker and prints lane counts, flags `kanban/done` cards that still carry unchecked task boxes, and lists fully-ticked `kanban/pending` cards as move candidates. It is strictly read-only — it never edits, ticks, or moves a card
- Normal Debug/Release presets explicitly disable coverage and sanitizers, and the test scripts reject an instrumented shared cache before running. This prevents a prior coverage/ASan/TSan configure from silently slowing or changing the ordinary unit workflow
- `do smoke --skip-build` runs the explicit startup check from an already-built selected cache, avoiding a second compile/plugin-staging pass after `do test`; omitting `--skip-build` still configures/builds when needed. The normal completion path is Debug iteration, one parallel `do test debug`, smoke from that cache, relevant render checks only, then `do run release` for the final Release startup confirmation
- `t.sh`, `t.bat`, and `scripts/run_tests.*` retain the broad unit + smoke + available render-snapshot workflow for explicitly requested full/multi-configuration or CI validation; they are not stacked onto the normal `do.py` completion path
- `do deploy` creates a Release build, stages the runtime payload into `deploy/YYYY_MM_DD/mac` or `deploy/YYYY_MM_DD/win`, and writes a matching `draxul-YYYY_MM_DD-mac|win.zip` archive under the date folder. Windows packages contain only `draxul.exe`, its Microsoft C++ and adjacent runtime DLLs, compiled shaders, bundled fonts, and runtime assets; CMake metadata, object files, static libraries, tests, and source/build directories are excluded
- The repo-scoped `$draxul-review` skill runs isolated, read-only multi-AI reviews through installed Codex, Claude, Agy/Gemini, and Grok CLIs. Its default panel selects one healthy OpenAI, Anthropic, and Google transport; explicit panels reject duplicate companies, and `--all` adds every healthy configured company. When a synthesis prompt requests Kanban work items, the trusted parent runner validates the returned card paths/content and atomically creates them under `kanban/pending/`; providers never receive repository write access
- Reviews and synthesis are separate operations. Immutable run archives, manifests, diagnostics, and optional summaries live under `plans/reviews/runs/`, while atomically refreshed `*-latest.*.md`, `*-consensus.md`, `*-latest.manifest.json`, and `*-latest.summary.manifest.json` files preserve stable pointers without synthesis hiding the latest review manifest
- `$draxul-preflight` checks installation, authentication, requested-model access, and a nonce-based live response before review work. Agy is preferred for Google and falls back to Gemini when unavailable, with the fallback recorded in the run manifest. Reviewers have a 30-minute default timeout and emit flushed lifecycle events. Real Codex, Claude, and Grok review/synthesis sessions persist in their normal provider stores for TokenFu tool-call and token accounting, while nonce-only preflight sessions remain ephemeral and cross-session memory stays disabled. A Codex review that hits native Windows sandbox error 1312 retries once without the broken OS sandbox inside its disposable repository snapshot, retains the fixed review-only contract and original total timeout, and records the fallback in the manifest

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `DRAXUL_ENABLE_RENDER_TESTS` | ON | Render test/snapshot infrastructure |
| `DRAXUL_ENABLE_SANITIZERS` | OFF | ASan + UBSan |
| `DRAXUL_ENABLE_TSAN` | OFF | ThreadSanitizer (Clang/GCC only, mutually exclusive with `DRAXUL_ENABLE_SANITIZERS`) |
| `DRAXUL_ENABLE_COVERAGE` | OFF | LLVM source-based coverage |
| `DRAXUL_ENABLE_MEGACITY` | ON | Builds and stages `dev.draxul.megacity` with its private City/Biology implementation, tests, shaders, and assets; the production executable has no static registration |
| `DRAXUL_ENABLE_SATVIEW` | ON | Builds and stages the `dev.draxul.satview` DLL/dylib plus its private product libraries and assets; the executable has no static SatView host fallback |
| `DRAXUL_ENABLE_SCOREVIEW` | ON on Windows/macOS | Builds and stages `dev.draxul.scoreview`, its private runtime libraries, Verovio, fonts, and soundfonts; the executable has no static ScoreView fallback |
| `DRAXUL_ENABLE_REZONALITY` | ON | Builds and stages the `dev.draxul.rezonality` DLL/dylib, preserved platform shader compiler, bundled simple project, live-edit runtime, and Vulkan/Metal single-pass renderer |
| `DRAXUL_MEGACITY_PLUGIN_DIR` | `plugins/megacity` | MegaCity/BioView submodule mount path; an enabled but absent mount is skipped |
| `DRAXUL_SATVIEW_PLUGIN_DIR` | `plugins/satview` | SatView submodule mount path; an enabled but absent mount is skipped |
| `DRAXUL_SCOREVIEW_PLUGIN_DIR` | `plugins/scoreview` | ScoreView submodule mount path; an enabled but absent mount is skipped |
| `DRAXUL_REZONALITY_PLUGIN_DIR` | `plugins/rezonality` | Rezonality submodule mount path; an enabled but absent mount is skipped |
| `DRAXUL_REQUIRE_ENABLED_PLUGINS` | OFF (ON when `CI` env var set) | Turns the enabled-but-unmounted plugin skip into a configure failure so CI cannot silently drop product coverage |
| `BUILD_TESTING` | ON | Test targets |

The product mounts are git submodules of their own repositories:
[draxul-megacity](https://github.com/cmaughan/draxul-megacity),
[draxul-satview](https://github.com/cmaughan/draxul-satview), and
[draxul-scoreview](https://github.com/cmaughan/draxul-scoreview), and
[draxul-rezonality](https://github.com/cmaughan/draxul-rezonality). Clone with
`--recurse-submodules` (or run `git submodule update --init`); an
uninitialized submodule leaves a core-only build.

Markdown and Kanban are product modules under `modules/markdown/` and `modules/kanban/`. They are built by default and keep their existing host flags and CMake target names.

### Build Targets
- `draxul` -- Main executable (.app bundle on macOS)
- `draxul-tests` -- Unit test suite (Catch2), compiled with a test-only precompiled header and registered as four disjoint CTest shards labeled `unit`
- `draxul-tests-core` -- Core/app/Markdown/Kanban test executables and public-header link-isolation checks used by default by `do test`
- `draxul-tests-megacity`, `draxul-tests-satview`, `draxul-tests-scoreview` -- Product-specific aggregates selected explicitly by the corresponding `do test` flags
- `draxul-rpc-fake` -- Fake RPC server for integration tests

ScoreView builds as private product libraries beneath `plugins/scoreview` inside
the `DRAXUL_ENABLE_SCOREVIEW` gate, plus the dynamic module; the per-library
layering and dependency-isolation rationale is documented in
[docs/features/scoreview.md](features/scoreview.md#build-structure).

CTest also registers `tests/do_py_tests.py` under the `unit` label. App smoke and render-snapshot tests use a shared CTest resource lock so full parallel test runs never overlap GPU/application processes.
On Windows, every test executable that links ScoreView stages `verovio.dll`
beside itself, so Debug and Release CTest runs do not depend on a stale DLL or
the developer's `PATH`.

Each optional product owns its FetchContent declarations, focused test wiring,
shader compilation, assets, tools, and runtime payload declaration beneath its
mounted directory. Root CMake only enables the mounted directory;
generic registration stages the declared payload and the generic test harness
includes the product-owned test file. Removing a product therefore removes its
downloads and focused tests from the build graph without editing core wiring.

### Dependencies (FetchContent, automatic)
SDL3, FreeType, HarfBuzz, MPack, ImGui, GLM, Catch2, vk-bootstrap (Windows), VMA (Windows)

### Compiler Cache
If `ccache` (or `sccache`) is found on `PATH`, the build automatically routes every C/C++ compile through it via `CMAKE_<LANG>_COMPILER_LAUNCHER`. The launcher is configured before `project()` so language-enablement compile probes also benefit. No effect when neither tool is installed.

### Shaders
- Windows: GLSL 4.50 -> SPIR-V via glslc
- Windows shader discovery uses CMake `CONFIGURE_DEPENDS`, so added `.vert`/`.frag` files trigger regeneration of the shader build rules during the next build
- macOS: Metal Shading Language -> metallib via xcrun

---

## CI (GitHub Actions)

| Workflow | Description |
|----------|-------------|
| `build.yml` | Windows + macOS build/test pipeline, run automatically for pushes and pull requests to `main` or manually through `workflow_dispatch`; uploads the Windows app artifact and both platforms' render-test outputs |

Both CI platforms install Neovim and run with `DRAXUL_RUN_SLOW_TESTS=1`.
Sanitizer and coverage presets remain available for local diagnostics but are not separate GitHub Actions workflows.

---

## Render Test Infrastructure

- **Scenario inventory**: `tests/render/manifest.json` is the single source for CTest registration, `do.py` commands, required platform references, and regression/developer/documentation status
- **Scenario files**: TOML in `tests/render/` with per-scenario font, size, DPI, commands; undeclared or missing files fail validation
- **Reference images**: BMP files in `tests/render/reference/` (platform-suffixed)
- **Regression scenarios**: basic-view, cmdline-view, unicode-view, panel-view, nanovg-demo
- **Developer-only scenario**: wide-char-scroll (not in CTest until both platform references exist); README and Claude-logo scenarios are documentation-only
- **Comparison**: Pixel-diff with configurable tolerance and changed-pixel threshold
- **Blessing**: scenario commands and `py do.py blessall` are derived from the manifest

---

## Logging

| Level | Macro | Notes |
|-------|-------|-------|
| Error | `DRAXUL_LOG_ERROR` | Always compiled |
| Warn | `DRAXUL_LOG_WARN` | Always compiled |
| Info | `DRAXUL_LOG_INFO` | Always compiled |
| Debug | `DRAXUL_LOG_DEBUG` | Stripped in release |
| Trace | `DRAXUL_LOG_TRACE` | Stripped in release |

Categories: App, Rpc, Nvim, Window, Font, Renderer, Input, Test.
Output: stderr (always) + optional file via `--log-file`.
