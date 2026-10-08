# Personal Assistant initial implementation plan

Date: 2026-10-01  
Status: Course correction (2026-10-06): personal agents are persistent Codex/Claude terminal conversations with optional sidebar names and automatically provisioned Dropbox backing data. The earlier form/editor and bounded one-shot runner are superseded. See `docs/personal-assistant.md` for current behavior.
Tracking: [67 personal-assistant-host -feature.md](../kanban/pending/67%20personal-assistant-host%20-feature.md)

## Accepted interaction model

Start an agent from a sidebar pill and chat using its existing provider terminal.
Prefix its startup with the location of `Vault/PA/agents/<id>` so it can read or
initialize standing instructions and durable context. The user can optionally
rename the pill; internal IDs and metadata are not a configuration task. Keep
provider model and permission behavior in the normal chat/profile workflow.

The remaining design below is historical planning. Scheduling, messaging,
ownership and handoff require review against this simpler model before another
slice starts; they must not reintroduce a job-definition form or manual-run ritual.

## Original intent (historical)

Make Draxul a visible home for persistent personal agents without building a new
agent framework. A user chooses a supported provider/model, describes a job in
plain language, and can inspect activity or change standing instructions later.
Agents live in a user-selected folder, potentially synced through Dropbox. The
Draxul server owns their execution; closing a host, tab, or every GUI window does
not delete definitions or stop the scheduler.

The Personal Assistant is a built-in core product host, following the Markdown
and Kanban module pattern, not an optional external plugin. Its presentation must
remain separate from the headless server. "Core" does not mean putting product
logic into `app/` or linking a GUI host into `draxul-server`.

## Agreed scope

- Discover and maintain one configured personal-assistant collection initially.
- Persist names, provider/model selections, instructions, schedules, enabled
  state, durable messages/checkpoints, and inspectable run records.
- Create agents conversationally; distinguish a one-off message from an edit to
  standing instructions. Show the interpreted schedule before enabling it.
- Keep every definition visible in a Personal Agents rail section, even asleep.
  Show active executions in the existing running Agents section as well.
- Open a Personal Assistant host for details, instructions, activity, pause,
  manual run, and bounded communication with an agent.
- Schedule bounded runs without calling a model simply to check for pending work.
- Keep exactly one authorized executor for the collection; support graceful
  handover before calling cross-machine hosting complete.
- Preserve Windows and macOS behavior and existing interactive managed agents.

Out of scope: Outlook/news connectors, mail permissions, autonomous tool design,
a new reasoning or memory framework, arbitrary provider support, seamless transfer
of live provider processes/conversations, and guaranteed offline forced takeover.
The user-selected runtime performs the job. Provider availability and credentials
are machine prerequisites, not things copied into the shared folder.

## Existing foundations and required seams

`modules/kanban/draxul-kanban` demonstrates a built-in product and host boundary.
`libs/draxul-agent` owns neutral profile and identity values. The server already
launches managed agents, owns terminal processes, and publishes agent status to
clients without a GUI attached.

Two existing assumptions need deliberate handling:

1. `libs/draxul-server/src/server_agent_service.h` derives Session-scoped agent
   snapshots from pane-owned runtimes. A personal definition must instead survive
   without a pane or active execution and be shared across local Sessions.
2. `TopologyService::launch_agent` in
   `libs/draxul-server/src/topology_service.cpp` requires Space/tab/pane topology.
   Reuse its process/profile facilities through a narrow runtime seam; do not
   implement personal agents as hidden permanent terminal panes. Keep existing
   topology-backed launch behavior intact.

Proposed ownership, with final target names settled during the first slice:

| Boundary | Responsibility |
|---|---|
| `libs/draxul-personal-agent/` | UI-free definition/storage, schedule and run values, ownership contract, bounded inbox policy |
| `libs/draxul-server/` | Collection registry, execution/scheduling, private runner adapter, ownership enforcement, authoritative live projection |
| `libs/draxul-protocol/` and `libs/draxul-client/` | Versioned collection-scoped requests, revisions, sanitized status, explicit bounded history/output reads |
| `modules/personal-assistant/` | Built-in host, definition/detail views and instruction editing; consumes client APIs, never launches processes |
| `libs/draxul-app-shell/` and `app/` | Rail layout/selection, host registration, palette actions and thin orchestration |

The server remains independent of product modules, SDL, windows and renderers.
Reuse current authenticated local transport; a remote machine is not implicitly
granted a connection to the PC's server. Remote coordination is a separate seam.

## Durable collection

Illustrative initial layout; the first slice fixes one strict current schema:

```text
Personal Assistant/
  collection.toml
  agents/
    <stable-agent-id>/
      agent.toml
      instructions.md
      memory/
      inbox/
      runs/
        <run-id>/
          run.json
          summary.md
          artifacts/
  handoffs/
```

The collection manifest supplies a stable identity and schema version, not a
machine-specific absolute root. Each agent manifest records its identity, display
name, runtime profile/model, enabled state, schedule and definition revision.
The instruction filename is provider-neutral; adapters supply provider-specific
context without assuming every runtime reads `AGENTS.md` automatically. Agent-owned
memory is opaque to Draxul. Draxul does not define a reasoning/memory system.

Store live process handles, authentication secrets, coordinator credentials,
local locks and fast-changing runtime state outside the synced collection.
Persist a timestamped last-known activity summary, but label it stale when the
owning server cannot be reached; a synced status file is never proof of ownership.
Raw transcripts are local by default; exporting private content into a synced
folder is explicit. Run records capture instruction revision, trigger, model,
timestamps, outcome and references to artifacts/checkpoints.

Use bounded reads, strict version checks, root-contained paths and transactional
replacement. Reject path traversal and unsafe link escapes. Validate a complete
definition revision before scheduling it; temporarily incomplete sync states are
not deletions. Retain last-known data for display, but pause scheduling an invalid
or conflicted definition until resolved. A missing collection pauses scheduling
instead of creating a replacement collection with an unrelated identity.

Edits go through the owning server where reachable. Disconnected edits are
revision-based proposals, not competing authoritative writes; direct file edits
are supported but validated before activation. Never silently overwrite a
concurrent instruction change. Removing a definition stops future runs; deleting
history or terminating an active run is a separate explicit action.

## User experience and execution contract

The rail separates persistent personal agents from currently running agents.
Clicking a personal agent opens/focuses its detail host; clicking an active run
opens its bounded output or a supported attached terminal view. Closing that view
does not cancel the run. Personal definitions are collection-scoped rather than
owned by whichever Session happened to create them.

Creation collects a name, supported runtime/model and natural-language job. A
bounded call through the selected runtime may propose structured schedule and
instructions; validate its response, show a preview, and keep the agent disabled
until accepted. Missing runtime support or authentication is a visible blocked
state, not a silent model substitution. Only configured adapters that implement
the required execution contract are selectable for unattended execution.

Start with interval schedules and manual/message triggers, not a full cron or
calendar language. An inexpensive server timer reconciles due work at roughly
one-minute resolution; it does not invoke every model each minute. Wake sooner
for explicit requests when possible. Coalesce missed intervals, avoid overlapping
runs for one agent, and enforce global concurrency, timeout, output and retry
limits. Persist trigger IDs and claim state before launch. After a crash, mark an
ambiguous in-flight run interrupted/unknown rather than blindly replaying actions.

Each run uses an immutable snapshot of the definition and its revision. Instruction
or model edits affect the next run. "Restart with changes" explicitly cancels the
current run before launching another. Pausing disables future scheduling; stopping
an active run is separately visible. Use process completion and structured runner
results for lifecycle decisions, not terminal text heuristics alone.

For initial communication, persist addressed messages with unique IDs, sender,
recipient, correlation, acknowledgement and bounded payloads. A recipient can be
woken by a message; acknowledge only durable processing progress, and deduplicate
redelivery. Cap queue size and delegation depth. Messages do not rewrite standing
instructions or grant capabilities. General agent collaboration remains the
selected runtime's responsibility, not a new Draxul multi-agent framework.

## Collection ownership and handover

A locally synced file is not the distributed lock. Use one collection-wide owner,
not a separate distributed election per agent. The coordinator contract needs an
atomic claim, renewal, release, owner inspection, and a monotonically increasing
ownership generation. Its authoritative clock determines lease expiry; the client
uses conservative monotonic deadlines and fails closed when renewal is uncertain.

The coordinator backend, hosting/account requirements and authentication are an
explicit design gate before enabling cross-machine automatic acquisition. A real
service with atomic coordination is required; do not select or provision paid
infrastructure as part of this plan. Tests use an injected coordinator and clock,
but a fake coordinator is not production acceptance evidence.

Normal behavior:

1. On startup, load definitions and display their state without executing them.
2. Claim through the coordinator if unowned. If another machine owns the collection,
   display its name and last contact with a "Move here" action.
3. A move request makes the old owner quiesce: stop launches, finish or cancel and
   confirm exit of runs, publish a checkpoint handoff manifest, then release.
4. The destination claims ownership and verifies that the referenced revisions and
   checkpoint content have arrived before launching. Lease acquisition does not
   imply Dropbox has finished syncing.
5. If renewal fails, stop launches and enforce child-process shutdown before the
   local safe lease deadline. Revalidate on resume from sleep before doing work.

A lease authorizes execution; it cannot prove that an unreachable machine stopped.
Ownership generations only fence operations whose destination validates them.
Arbitrary provider processes and direct external tool calls are not automatically
fenced, and an accepted external action cannot be undone by killing its process.
Therefore v1 does not automatically replay unfinished work after an unacknowledged
owner loss. It enters "Previous executor unconfirmed" and requires positive stop
confirmation or an explicitly reviewed recovery path. Never market expiry alone
as exactly-once execution or offer a silently unsafe "Force" button.

Local development can use an explicitly pinned single-machine executor with a
machine-local singleton lock. Other machines stay display-only, and copied config
does not authorize execution. This is an intermediate slice, not delivery of the
requested cross-machine ownership behavior. Automatically starting after reboot
requires explicit background-service/start-at-login provisioning; merely closing
the UI is already a different lifecycle. Sleeping machines do not execute work.

## Implementation sequence and review gates

Deliver small vertical slices; stop for human review after each completed slice.
Checkboxes and evidence live in the linked Kanban card, not in a second task list.

### Slice 1 Discover and inspect a collection

Introduce the strict folder schema and UI-free store, a server collection registry,
versioned list/get projection, and the built-in host plus Personal Agents rail.
Open a sample collection from two local clients in different Sessions. Definitions
appear without processes; closing/reopening views changes no agent lifecycle.
Exercise missing roots, malformed definitions, version rejection, sync-incomplete
files and conflicts. Pin execution off until the ownership mode is configured.

### Slice 2 Create edit and manually run one agent

Add create/update operations with expected revisions, natural-language creation
preview, supported profile/model selection, and explicit pinned-executor mode.
Implement the narrow server runner adapter, immutable run input, run history and
manual run/cancel. Project active personal executions into the existing Agents
section without inventing a permanent pane or duplicating ordinary agent rows.
Prove edits survive restart and both clients converge; run one supported provider
against a harmless local task and retain inspectable results with the UI closed.

### Slice 3 Schedule and exchange durable messages

Add interval scheduling, run-now/message triggers, pause, bounded inbox delivery,
concurrency/timeouts and retry policy. Include a durable checkpoint and ambiguous
run recovery policy. Demonstrate two simple agents exchanging one message, no
idle model calls, no overlapping runs, no message loop, and no catch-up storm after
sleep. Verify runtime/model errors become actionable states without fallback.

### Slice 4 Coordinate two machines and hand over

Resolve the coordinator backend and authentication gate, then implement its real
adapter and owner/status projection. Add automatic acquisition of a genuinely
unowned collection and graceful "Move here" with checkpoint-availability checks.
Test simultaneous startup, rejected stale generations, delayed sync, coordinator
outage, lease loss, interrupted handoff and machine sleep/resume. Unacknowledged
loss remains blocked instead of silently forcing failover. Demonstrate a real
Windows/macOS handoff with the chosen backend before claiming portability works.

### Slice 5 Finish the built-in experience

Complete host navigation, instruction/message distinction, owner/blocked/paused
states, bounded output/history, recovery explanations and palette actions. Add
focused render coverage, configuration documentation, CLI inspection/control parity,
and the feature/module-map entries only as capabilities actually ship. Document
credentials, background-server startup and what is not guaranteed during outages.

## Validation and completion

Favor integration coverage across folder, server, client and host boundaries.
Use deterministic fake runners/clocks for failure cases, plus opt-in live-provider
evidence without storing secrets or private prompts in fixtures. Register target
dependencies and core scope labels in CMake rather than duplicating test inventories.

Each completed implementation slice runs `py do.py test debug` and
`py do.py smoke --skip-build` from the same Debug cache, plus a relevant render
scenario for visible UI changes. Use the equivalent Python command on macOS and
normal cross-platform CI. Finish the feature with the repository's Release startup
confirmation. The real two-machine ownership exercise is a specific manual gate
because local fake-clock tests cannot establish sync/handoff behavior on two hosts.

Record commands, timing, outcomes and unresolved gates in the tracker. Do not move
the card to done while provider, render, cross-machine or other required checks
remain open. Planning-only edits require document/link/diff checks, not a product
build. No implementation or execution guarantee is established by this plan.
