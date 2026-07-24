# Local agent harness implementation plan

**Date:** 2026-07-24  
**Status:** Phases 0-7 core complete; interactive discovery correction remains optional
**Scope:** local agents owned by one running Draxul application

This plan consolidates the agent portions of
[herdr-agent-harness-research.md](herdr-agent-harness-research.md) and
[multi-space-session-persistence.md](multi-space-session-persistence.md). The Space,
tab, pane, session-persistence, and initial Agents-sidebar work described there is
already complete.

## Goal

Turn Draxul's existing Agents sidebar into a local agent harness that can:

- launch known agent products safely into terminal panes;
- distinguish agent identity, process lifecycle, semantic status, and native
  conversation identity;
- navigate, inspect, prompt, and wait on agents through a local API;
- restore every Space and optionally resume supported native agent conversations after
  an application restart; and
- explain how an agent status was derived without persisting or logging terminal
  contents.

Codex and Claude Code share one native-session interface despite their different hook
configuration formats.

## Current baseline

Draxul already has:

- `Session -> Space -> Tab -> Pane` ownership and all-Space transactional restore;
- stable Space, tab, pane, and agent-instance identities;
- optional pane-owned `AgentIdentity { kind, display_name, instance_id }`;
- `AgentController`, which derives a global Agents projection by walking every pane;
- Agents-sidebar focus navigation across inactive Spaces and tabs;
- an explicit `launch_agent` command-palette action;
- persisted agent identity and saved pane launch data; and
- ephemeral `running` and `focused` sidebar fields that are rebuilt after restore.

The current limitations are intentional:

- `launch_agent` writes an arbitrary command into a newly spawned shell;
- `running` describes the containing shell host, not necessarily the agent process;
- a restored agent pane replays its startup command and starts a fresh conversation;
- there is no semantic `working`, `blocked`, `done`, or `idle` state;
- there is no native Codex/Claude conversation reference;
- there is no local control endpoint; and
- manually started agents are not discovered.

## Herdr behavior to preserve

Herdr separates two restart cases:

| Case | Process | Agent conversation |
|---|---|---|
| Client reconnects to a live server | Same PTY and process | Same because it never stopped |
| Server cold-restores a snapshot | New PTY and process | Same only if the agent's native resume command succeeds |

Draxul currently has no background server, so application restart is always the second
case. For Codex, the equivalent cold-restore operation is a new process launched as:

```text
codex resume <native-session-id>
```

An official integration must report that native ID while Codex is running. Draxul must
store the reference against its pane occupant, validate it during restore, deduplicate
it across all Spaces, and build a product-owned resume plan. It must not claim that the
old process survived.

Eligible restored agents should start after application geometry is known, including
agents in inactive Spaces, inactive tabs, and non-focused panes. Focus is navigation
state, not a prerequisite for restore.

## Non-goals

This plan does not add:

- a hidden daemon or background owner after the Draxul window closes;
- detach/reattach, live process handoff, or suspend/resume;
- SSH, remote Spaces, or remote process control;
- terminal-screen persistence or transcript recovery;
- automatic prompt approval or autonomous permission decisions;
- cloud coordination; or
- heuristic discovery as an authority for native conversation identity.

Those features can build on the local control protocol later, but none is required for
the local harness.

## Model and ownership

An agent remains an optional occupant of a terminal pane. It is not a pane type and it
does not own a second copy of the Space/tab/pane hierarchy.

### Durable values

| Value | Meaning | Owner |
|---|---|---|
| `AgentIdentity` | Kind, display name, and stable per-Session instance ID | Pane |
| `AgentLaunchDescriptor` | Executable, argv, working directory, profile, and restore policy | Existing pane launch snapshot |
| `AgentSessionRef` | Optional product-native conversation ID or path plus reporting source and integration version | Pane occupant |
| `AgentRestorePolicy` | `fresh`, `resume_if_available`, or `shell_only` | Pane occupant/profile |

### Ephemeral values

| Value | Meaning |
|---|---|
| `AgentTarget` | Agent identity plus resolved Space, tab, pane, and current host |
| `AgentLifecycle` | `starting`, `running`, `exited`, or `failed` |
| `AgentStatus` | `unknown`, `idle`, `working`, `blocked`, or `done` |
| `AgentObservation` | Bounded process, title, activity, and bottom-buffer evidence |
| `AgentStateAuthority` | The source currently allowed to author lifecycle or semantic state |
| `AgentAttention` | Unseen completion or needs-input latch |
| `AgentRuntimeGeneration` | Changes whenever a new process occupies the pane |

Lifecycle and semantic status must remain separate. A process can be running while its
agent is idle or blocked. A native conversation ID says nothing about either state.

### Component boundaries

The reusable model and policy should live below `app/`:

```text
libs/draxul-agent/
  Agent model values
  Agent definitions/profiles
  Status-manifest evaluator
  Resume-plan validation
  Explain-state result

libs/draxul-host/
  Direct local-process launch through PTY/ConPTY
  Bounded terminal observations
  Serialized automation input

libs/draxul-control/
  Versioned JSON protocol
  Windows named-pipe transport
  Unix-domain-socket transport

app/
  AgentController routing across Spaces
  Main-thread mutation and event publication
  Session snapshot integration
  Command palette and sidebar presentation
```

`AgentController` should remain in `app/` because it resolves application-owned Spaces,
tabs, panes, and focus. It should depend on neutral agent values rather than owning the
status engine or transport.

Worker threads may capture transport messages or evaluate immutable observations, but
they must publish results back to the main thread. They must never mutate a grid,
`PaneManager`, `SpaceController`, or renderer directly.

## Phase 0: pin the model and current behavior

**Status:** complete (2026-07-24).

**Purpose:** create a safe refactoring boundary before launch behavior changes.

### Work

1. Add characterization tests for:
   - ordered agent projection across multiple Spaces and tabs;
   - focus routing by `instance_id`;
   - restored pane-owned identity;
   - globally unique agent instance IDs;
   - current fresh-command restore behavior; and
   - exited containing hosts.
2. Move neutral agent values from app-private headers into a small
   `draxul-agent` static library.
3. Keep `AgentController` and all visible behavior unchanged.
4. Add exhaustive string conversion for lifecycle, semantic status, authority, and
   restore-policy enums.
5. Document one invariant: sidebar rows and lookup indexes are always derived and are
   never serialized.

### Likely files

- `app/agent_identity.h`
- `app/agent_controller.h/.cpp`
- `app/pane_manager.h/.cpp`
- `tests/space_controller_tests.cpp`
- `tests/pane_manager_tests.cpp`
- `tests/app_smoke_tests.cpp`
- new `libs/draxul-agent/`
- `CMakeLists.txt`
- `docs/module-map.md`

### Exit

All existing tests and smoke validation pass with no user-visible change.

## Phase 1: structured profiles and direct managed launches

**Status:** complete (2026-07-24).

**Purpose:** make explicitly launched agents reliable before attempting detection.

### Work

1. Introduce `AgentDefinition` and `AgentDefinitionRegistry`.
2. Supply built-in definitions for Codex and Claude Code:
   - canonical kind;
   - display name;
   - executable;
   - default argv;
   - supported native session-reference kind;
   - restore adapter; and
   - status-manifest identifier.
3. Support user profiles in config using an executable and an argv array. Do not accept
   one interpolated shell string as a managed profile.
4. Replace `App::launch_agent(string_view)` internally with a structured
   `AgentLaunchRequest`.
5. Launch the executable directly as the PTY/ConPTY child by using `command` and `args`
   in `HostLaunchOptions`, rather than typing it into a shell through
   `startup_commands`.
6. Ensure shell-host defaults are applied only when the default shell executable is
   selected. In particular, a custom Windows process with no arguments must not inherit
   PowerShell's `-NoLogo` argument.
7. Inject routing environment variables into managed processes:

   ```text
   DRAXUL_ENV=1
   DRAXUL_SESSION_ID
   DRAXUL_SPACE_ID
   DRAXUL_TAB_ID
   DRAXUL_PANE_ID
   DRAXUL_AGENT_INSTANCE_ID
   ```

8. Change the command-palette flow from a raw command prompt to an agent-profile picker.
   A custom profile is configured as structured argv. Keep ordinary shell panes
   available for arbitrary commands.
9. Preserve compatibility for existing version-2 snapshots containing agent identity
   plus shell `startup_commands`; they remain legacy fresh-launch panes until explicitly
   relaunched through a managed profile.

### Restore policy

New built-in profiles use `resume_if_available`. Until native integrations land, this
falls back to `shell_only`, not a guessed resume. User profiles may opt into `fresh`.
Legacy version-2 panes keep their existing fresh-command behavior.

### Tests

- built-in and configured profile resolution;
- argv round-trip without shell expansion;
- executable-not-found failure;
- paths and arguments containing spaces and shell metacharacters;
- Windows custom executable with empty argv;
- macOS PTY direct process launch;
- routing environment values;
- legacy snapshot compatibility; and
- restart-in-place preserving profile and identity.

### Exit

Draxul can launch Codex and Claude as directly owned terminal processes, and host
lifecycle now describes the managed agent process rather than an enclosing shell.

## Phase 2: lifecycle truth and sidebar actions

**Status:** complete (2026-07-24).

**Purpose:** make the existing Agents area operational even before semantic detection.

### Work

1. Add per-pane `AgentRuntimeGeneration` and lifecycle timestamps.
2. Derive `starting`, `running`, `exited`, and `failed` from the directly owned host.
3. Capture exit code without deleting pane identity or its final rendered contents.
4. Extend `AgentProjection` with lifecycle, exit code, and generation.
5. Add sidebar status text while preserving the existing shared pill geometry:
   - no suffix while running and status is unknown;
   - `[starting]`, `[exited N]`, or `[failed]` for lifecycle;
   - semantic labels after Phase 3.
6. Add command-palette actions:
   - focus agent;
   - restart agent as a new runtime generation;
   - close agent pane; and
   - clear stale agent identity from a pane.
7. Make waits and future API calls resolve both `instance_id` and runtime generation so
   a replacement process cannot accidentally satisfy an old operation.

### Exit

The Agents sidebar accurately represents explicitly managed process lifecycle across
all Spaces, including completed and failed agents.

## Phase 3: terminal observation and semantic status

**Status:** complete (2026-07-24).

**Purpose:** derive explainable `working`, `blocked`, `done`, and `idle` status without
requiring every agent product to expose complete lifecycle hooks.

### Host capability

Add a narrow terminal-agent capability implemented only by local terminal hosts:

```text
capture_agent_observation(max_rows, max_bytes)
send_agent_input(bytes)
```

An observation contains:

- a monotonically increasing output generation;
- capture time and last-output time;
- terminal title;
- a bounded set of bottom visible rows;
- cursor visibility/position when useful; and
- available direct-process metadata.

It must not expose mutable grid objects or renderer state.

### Status engine

1. Add bundled, versioned screen manifests for Codex first.
2. Evaluate on terminal-output generations with a short debounce and a maximum rate,
   never on every render frame.
3. Use conservative precedence:
   - direct host owns lifecycle;
   - a complete official hook may own semantic status;
   - otherwise the screen manifest owns semantic status;
   - ambiguous evidence produces `unknown`.
4. Record only the matched rule ID, evidence category, manifest version, and timestamp.
   Do not retain matched screen text in durable state or ordinary logs.
5. Add an Explain Agent State view containing:
   - active authority;
   - manifest and rule version;
   - sanitized evidence category;
   - last transition time; and
   - fallback reason.
6. Add an unseen `done`/`blocked` attention latch. Focusing the target acknowledges the
   latch; a later transition can raise it again.
7. Add Claude fixtures and rules only after the Codex evaluator and diagnostics are
   shared and stable.
8. Do not download remote manifest updates in this phase. Bundled rules and explicit
   local overrides are sufficient for the first release.

### Tests

- recorded, hand-redacted Codex fixtures for idle, working, approval/input blocked,
  done, and ambiguous states;
- output split across arbitrary PTY chunks;
- resized and narrow terminals;
- Unicode and styled cells;
- rule precedence and unknown fallback;
- stale observation rejection;
- attention-latch transitions;
- no terminal text in snapshot or diagnostic logs; and
- main-thread ownership under worker evaluation.

### Exit

Codex status is useful, conservative, fixture-tested, and explainable. Claude runs
through the same evaluator once its fixtures pass the same contract.

## Phase 4: local read-only control plane

**Implemented:** 2026-07-24

**Purpose:** make Draxul inspectable as a harness before enabling external mutation.

### Transport

- Windows: same-user named pipe.
- macOS/Linux: Unix-domain socket with owner-only permissions.
- One endpoint per running Draxul Session.
- Length-prefixed, versioned JSON messages with strict size and nesting limits.
- A random runtime authentication token stored in an owner-only runtime file.
- The CLI discovers the endpoint from the Session ID; child agents receive routing IDs,
  not the authentication token.

Transport threads parse bounded messages and enqueue typed requests. The main thread
resolves application state and returns immutable responses.

### Initial methods

```text
system.hello
space.list
space.get
agent.list
agent.get
agent.explain
pane.read
```

`pane.read` returns only a caller-requested, bounded recent region. It is never written
to the Session snapshot or application log.

### CLI

```text
draxul space list --session <id>
draxul agent list --session <id>
draxul agent get <instance-id> --session <id>
draxul agent explain <instance-id> --session <id>
draxul pane read <pane-id> --lines <n> --session <id>
```

Human output should be concise; `--json` should return the protocol result without
presentation fields.

### Exit

Local scripts can reliably enumerate and inspect all Spaces and agents without UI
automation or access to Draxul's in-memory objects.

Implementation notes:

- `draxul-control` owns the cross-platform framed transport, runtime metadata,
  authentication token, bounds checking, and client.
- The transport queues typed requests; `ControlRequestRouter` resolves them on the
  application thread.
- Normal restorable desktop launches expose one endpoint per Session. Smoke tests,
  render tests, and embedded `App` users remain endpoint-free unless they opt in.
- `pane.read` currently returns the bounded bottom region exposed by a live terminal
  host. It does not expose mutable grid state or persisted scrollback.
- Transport, router, and CLI parsing have focused tests.

## Phase 5: harness mutations, waits, and events

**Implemented:** 2026-07-24

**Purpose:** allow local agents and scripts to coordinate work safely.

### Methods

```text
space.focus
agent.start
agent.focus
agent.restart
agent.send_text
agent.send_keys
agent.wait
event.subscribe
```

### Rules

1. `agent.start` accepts a profile plus structured argv overrides, never shell text.
2. Input is serialized per pane on the main thread.
3. `send_text` and `send_keys` are distinct operations.
4. No API automatically confirms an approval prompt.
5. `agent.wait` pins `instance_id` plus runtime generation and supports lifecycle,
   semantic status, timeout, and cancellation.
6. If the occupant is replaced, a pinned wait ends with `agent_replaced`.
7. Subscriptions are bounded; slow consumers are disconnected with an explicit reason.
8. All mutating requests return the resolved Space/tab/pane target and resulting
   generation.

### CLI

```text
draxul agent start codex --cwd <path> -- <argv...>
draxul agent focus <instance-id>
draxul agent send <instance-id> --text <text>
draxul agent keys <instance-id> <key...>
draxul agent wait <instance-id> --until blocked,done --timeout 10m
```

### Exit

A local script can start, locate, inspect, prompt, and wait on several agents across
Spaces without relying on window focus or mouse/keyboard automation.

Implementation notes:

- Mutations resolve on the application thread and return the resulting agent
  projection, including its route and runtime generation.
- `agent.start` accepts only a registered profile, a bounded argv array, an optional
  working directory, and an optional Space ID. No shell command string is accepted.
- `agent.send_text` and `agent.send_keys` remain separate protocol methods. Key input
  accepts a bounded, named-key vocabulary; neither path interprets or confirms prompts.
- `agent.wait` is a generation-pinned state probe. The CLI polls it locally, owns its
  timeout/cancellation lifetime, and reports `agent_replaced` if the pane occupant's
  runtime generation changes.
- `event.subscribe` is a bounded cursor subscription rather than an indefinitely held
  transport request. It returns sanitized Space/agent transition events; a client that
  falls behind the 256-event journal receives `cursor_expired`.

## Phase 6: official hooks and native conversation restore

**Implemented for Codex and Claude:** 2026-07-24.

**Purpose:** resume the same Codex or Claude conversation after Draxul reconstructs a
Session.

### Integration management

Add explicit commands:

```text
draxul integration status
draxul integration install codex
draxul integration uninstall codex
draxul integration install claude
draxul integration uninstall claude
```

Installation must be opt-in, versioned, idempotent, and preserve unrelated user hooks.
`status` reports installed, current, outdated, or invalid without modifying files.

The official hooks read their `SessionStart` payloads and report:

```text
pane.report_agent_session {
  pane_id,
  agent_instance_id,
  source = "draxul:<agent>",
  agent = "<agent>",
  integration_version,
  sequence,
  session_ref = { kind = "id", value = <codex-session-id> }
}
```

The pane and agent instance must match the routing environment inherited by the agent.
Only official source/agent pairs may populate a resumable native reference.

### Persistence

Introduce Session snapshot version 3 when `AgentSessionRef` and restore policy are
persisted:

- keep version-1 and version-2 readers;
- write version 3 only;
- do not rewrite a file merely because it was inspected;
- bound reference values and reject control characters;
- require absolute normalized paths for path-based references;
- avoid including reference values in errors or logs; and
- enforce globally unique native-resume keys across all Spaces during restore.

Version 3 is preferable to silently extending version 2 because an older binary would
otherwise load and rewrite the file while dropping native conversation identity.

### Restore transaction

Before starting any restored pane:

1. Decode and validate all Spaces and panes as today.
2. Resolve each native reference through its owning `AgentDefinition`.
3. Check the official source/agent pair and minimum integration version.
4. Construct argv as data, for example:

   ```text
   ["codex", "resume", "<id>"]
   ```

5. Reserve a deduplication key consisting of source, agent kind, reference kind, and
   reference value.
6. Build one `AgentRestorePlan` per eligible pane.
7. Commit the reconstructed Space collection only after it is usable.
8. Once application geometry is established, directly spawn every eligible resume plan,
   including plans in inactive Spaces and tabs.

Direct executable launch avoids converting a native reference into shell syntax.

### Fallbacks

- No reference: follow the pane/profile's saved restore policy.
- Unsupported, outdated, invalid, or duplicated reference: open a normal shell in the
  saved working directory and retain a diagnostic reason.
- Resume executable missing or spawn fails: keep the pane inspectable as failed; do not
  silently start a new conversation.
- Agent rejects a valid-looking stale reference: show the process exit and offer
  `Start fresh`; do not loop.
- Duplicate reference: at most one deterministic pane may resume it.

Native resume replaces any fresh startup command for that pane; both must never run.
Pane screen history remains absent, because the native agent owns conversation replay.

### Configuration

```toml
[agents]
resume_on_restore = true

[agents.profiles.codex]
restore_policy = "resume_if_available"
```

The global setting defaults to `false` for the first release containing integrations.
After one release of diagnostics and field use, it may default to `true` for current
official integrations. Per-profile `shell_only` always wins.

### Tests

- Codex and Claude hook payload fixtures;
- install/status/reinstall/uninstall preserving unrelated hook configuration;
- source/agent allowlist;
- integration-version checks;
- ID and path validation;
- snapshot v1/v2 to v3 migration;
- no downgrade data loss;
- deduplication across different Spaces;
- inactive Space/tab restore;
- native resume overriding fresh startup;
- missing executable, failed spawn, and stale-reference behavior;
- reference values absent from logs; and
- `resume_on_restore = false`.

### Exit

After a normal Draxul restart, all Spaces are rebuilt and supported agent panes can
resume their prior native conversations as new local processes. Draxul accurately
describes this as conversation restore, not process survival.

Implementation notes:

- Session snapshot version 3 persists a validated, globally unique
  `AgentSessionRef`, profile ID, and restore policy while retaining v1/v2 readers.
- `pane.report_agent_session` routes on the stable pane ID plus agent-instance ID,
  rejects stale sequences and duplicate native conversations, and never returns the
  native reference value through the public projection.
- `draxul integration install|status|uninstall codex|claude` manages versioned
  `SessionStart` hooks under `CODEX_HOME` or `CLAUDE_CONFIG_DIR`, preserves unrelated
  configuration, and installs idempotently. Bare `integration status` reports both.
- With `[agents].resume_on_restore = true`, eligible panes are spawned as
  `codex resume <id>` or `claude --resume <id>`. Resume argv and routing environment
  are runtime-only and do not replace the pane's durable launch profile in the next
  checkpoint.
- The initial release remains opt-in and defaults resume to false. Missing or invalid
  native identity does not become inferred identity.

## Phase 7: best-effort discovery of manually launched agents

**Core implemented:** 2026-07-24.

**Purpose:** recognize agents started in ordinary shell panes without weakening the
reliable managed path.

This phase follows explicit launch, hooks, status evaluation, and the control API.

### Work

1. Add platform process observations:
   - Unix PTY session/process group and descendant commands;
   - Windows ConPTY descendant process tree as fallible evidence.
2. Normalize direct executable names, known aliases, and wrapped commands by inspecting
   executable name, argv zero, and structured argv. Cover common Node/Bun, Python,
   shell, `cmd`, and PowerShell wrappers without treating arbitrary argument text as an
   executable.
3. Accept an explicit per-process `DRAXUL_AGENT=<kind>` hint for VM/sandbox wrappers
   that hide the real executable. The hint is identity evidence only and is never a
   native conversation reference.
4. Create a discovered occupant immediately when one foreground candidate is
   convincing. On Windows, accept multiple matching descendants only when they are the
   same agent in one ancestor chain; ambiguous competing candidates remain unknown.
5. Stabilize what is actually noisy:
   - apply a short startup grace period before semantic screen evaluation;
   - retain identity through a bounded number of failed process probes;
   - publish process exit before returning the pane to an ordinary shell; and
   - debounce working-to-idle transitions.
6. Combine the identified process with terminal title and screen-manifest evidence for
   semantic status. Screen text alone must not create an agent identity.
7. Mark discovered identity and lifecycle authority explicitly.
8. Never assign a native `AgentSessionRef` from process or screen inference.
9. Let the user attach, correct, or dismiss a discovered identity.
10. Explain process identity evidence, wrapper normalization, status evidence, and
    confidence without logging argv or screen contents.

This follows Herdr's current behavior more closely than the earlier proposal. Herdr
recognizes a convincing positive foreground-process match immediately; its stability
logic primarily protects startup, semantic transitions, and removal rather than
delaying first recognition.

### Exit

Manually launched Codex/Claude processes can appear in the sidebar without being
mistaken for officially managed or natively resumable agents.

Implementation notes:

- `IHost` exposes a bounded immutable process observation. Unix terminal hosts sample
  the PTY foreground process group; Windows samples the ConPTY root's bounded
  descendant tree and marks the result as fallible foreground evidence.
- The neutral `draxul-agent` evaluator recognizes direct Codex/Claude executables,
  structured Node/Bun/npm wrappers, and an explicit `DRAXUL_AGENT` environment hint.
  Competing agent kinds are rejected; on fallible Windows observations, multiple
  matches must form one ancestor chain.
- Convincing positive evidence creates a discovered pane occupant immediately.
  Screen manifests do not create identity and semantic evaluation has a short startup
  grace period.
- Discovery probes are rate-limited, identity is retained through six failed probes,
  and a missing discovered process is projected as exited before removal.
- Discovered identities carry origin, evidence category, and confidence in local API
  results and `explain_agent_state`, but are excluded from Session snapshots and can
  never own a native session reference.
- `clear_agent_identity` suppresses rediscovery of the same live occupant until it has
  been absent for the full removal window. Explicit `DRAXUL_AGENT` hints provide the
  initial correction path; a dedicated interactive attach/correct picker remains a UI
  follow-up.

## UI progression

The sidebar remains a global projection ordered by Space, tab, and pane. Keep the
current compact pill layout and focused-pane green accent. Add information gradually:

1. lifecycle suffixes;
2. semantic status/attention suffixes;
3. optional Space name when two rows would otherwise be ambiguous; and
4. context actions for focus, restart, explain, start fresh, and close.

Do not use semantic status color as the selected-row color; selection/focus and status
are different dimensions. A small marker or text suffix can carry attention state.

## Security and privacy invariants

- Session topology, native session references, and runtime endpoint credentials are
  owner-readable only.
- Terminal observations and `pane.read` responses are bounded and never logged by
  default.
- Native reference values, prompts, screen text, and authentication tokens are excluded
  from errors, telemetry, and explain output.
- Agent argv is stored as an array and launched directly.
- Custom shell commands remain ordinary shell behavior and are not treated as managed
  structured agents.
- The control endpoint accepts only same-user local connections plus the runtime token.
- Protocol parsing is size-bounded before allocation and rejects unknown mutating
  methods.
- No status rule can send input or approve a prompt.

## Validation per implementation phase

Every phase should be a cohesive commit and must run:

```text
cmake --build build --target draxul draxul-tests
py do.py smoke
```

Also run:

- full `ctest` when terminal input, process launch, or control transport changes;
- render smoke/snapshots when sidebar or status presentation changes;
- Windows Release validation for ConPTY, named-pipe, ACL, and hook installation;
- macOS build/test validation for PTY, Unix socket, permissions, and hook installation;
- fixture-only status tests without requiring installed third-party agents; and
- opt-in live Codex/Claude integration tests, never as mandatory CI dependencies.

Update `docs/features.md` at each user-visible phase and `docs/module-map.md` when adding
or changing library boundaries.

## Recommended delivery sequence

| Milestone | Phases | User-visible result |
|---|---|---|
| Reliable targets | 0-2 | Structured launches and truthful lifecycle |
| Useful dashboard | 3 | Explainable Codex/Claude status and attention |
| Local harness | 4-5 | Inspect, prompt, focus, and wait through CLI/API |
| Conversation restore | 6 | Resume the same native conversation after restart |
| Broader discovery | 7 | Best-effort recognition of manually launched agents |

The first practical implementation slice is Phases 0 and 1 together: model extraction,
profile registry, and direct Codex launch. It removes the current ambiguity where
Draxul knows an agent was requested but can only observe the lifetime of its enclosing
shell.

## Definition of done

The local agent-harness plan is complete when:

- explicitly launched agents have structured, pane-owned identities and direct process
  lifecycle;
- the Agents sidebar reflects all Spaces and routes to the exact pane;
- Codex and Claude semantic status is conservative and explainable;
- the local API can list, inspect, focus, prompt, and wait on pinned runtime
  generations;
- official hooks can report native conversation references;
- all eligible native sessions restore once, including in inactive Spaces and tabs;
- invalid or stale references fail safely without starting an unintended conversation;
- terminal contents and runtime credentials are not persisted or logged; and
- documentation never implies that processes survive application exit.

## Related material

- [Herdr agents documentation](https://herdr.dev/docs/agents/)
- [Herdr integrations documentation](https://herdr.dev/docs/integrations/)
- [Herdr session-state documentation](https://herdr.dev/docs/session-state/)
- [Herdr Codex session hook](https://github.com/ogulcancelik/herdr/blob/master/src/integration/assets/codex/herdr-agent-state.ps1)
- [Herdr native resume planner](https://github.com/ogulcancelik/herdr/blob/master/src/agent_resume.rs)
- [Herdr deferred restore launcher](https://github.com/ogulcancelik/herdr/blob/master/src/app/agent_resume.rs)
- [Draxul Herdr research](herdr-agent-harness-research.md)
- [Draxul multi-Space persistence plan](multi-space-session-persistence.md)
