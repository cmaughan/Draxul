# Multi-Space session persistence and agent identity

**Status:** complete (2026-07-24)
**Date:** 2026-07-23
**Research baseline:** Herdr `e7fc85bfdb51f89488430adbfe5bbced3be79c2f`
**Scope:** save and restore every locally loaded Space, plus durable agent identity
**Out of scope:** PTY handoff, detach/reattach, suspend/resume, SSH, remote Spaces,
background ownership, and native agent conversation resume

## Decision

Keep Draxul's explicit C++ snapshot types and TOML persistence, but adopt Herdr's
structural boundaries:

1. One saved Session is one versioned aggregate containing the complete ordered
   collection of Spaces.
2. `active_space_id` identifies focus after restore; it does not select the only Space
   to restore.
3. Snapshot values contain durable data only. Controllers, hosts, renderer state, live
   process state, agent status, and detection evidence are rebuilt at runtime.
4. Agent identity is durable metadata attached to a pane occupant. The Agents sidebar
   is a derived projection across Spaces, not a second authoritative collection that
   can drift.
5. Capture the immutable snapshot on the app thread, then perform a replace-safe file
   write separately from live controller mutation.

Changing from TOML to JSON would add migration and dependency work without supplying
any of those properties. The clean part of Herdr is its state boundary and restore
transaction, not its encoding.

## What Herdr actually stores

Herdr stores one `SessionSnapshot` per selected/named session. That snapshot has:

```text
SessionSnapshot
|- version
|- workspaces[]                 # every loaded Space/workspace, in order
|- active                      # focused workspace index
|- selected                    # sidebar selection
`- sidebar presentation state

WorkspaceSnapshot
|- stable id and display name
|- identity cwd/worktree metadata
|- stable tab/pane number maps
|- tabs[]
`- active_tab

TabSnapshot
|- split layout
|- panes[]
|- focused/root/zoomed pane
`- display name

PaneSnapshot
|- cwd and display label
|- agent name and managed agent kind
|- optional native agent session reference
`- launch argv
```

The snapshot schema is explicit and versioned. Its layout is a recursive binary split
tree, similar in purpose to Draxul's existing `SplitTree::Snapshot`.
See Herdr's
[`SessionSnapshot`, `WorkspaceSnapshot`, `TabSnapshot`, and `PaneSnapshot`](https://github.com/ogulcancelik/herdr/blob/e7fc85bfdb51f89488430adbfe5bbced3be79c2f/src/persist/snapshot.rs#L14-L142).

Startup restores the complete `workspaces` vector and only then applies the saved
active and selected indices. It is therefore an **all-Spaces restore**, not
"restore the last active Space." See
[`AppState` startup restoration](https://github.com/ogulcancelik/herdr/blob/e7fc85bfdb51f89488430adbfe5bbced3be79c2f/src/app/mod.rs#L385-L470).

Restore is deliberately tolerant below the Session boundary:

- each workspace is attempted independently;
- a pane that cannot be recreated is omitted and the split tree is pruned;
- a tab is omitted only if none of its panes survive;
- a workspace is omitted only if none of its tabs survive;
- active/focused IDs fall back to surviving objects;
- stable public identities are kept separate from newly allocated runtime IDs.

This lets useful Spaces survive one stale directory, missing host, or bad pane. Draxul
does not need Herdr's full runtime-ID remapping, but it should adopt the partial-restore
policy.

### How Herdr tracks agents

Herdr does not persist an independent authoritative `AgentRegistry`.

Durable agent facts live with terminal/pane state: optional agent name, managed kind,
native session reference, and launch command. Live facts such as working/blocked/done,
token totals, hook evidence, and timestamps remain runtime state. The sidebar rebuilds
its agent entries by flattening pane details across all workspaces and tabs, then
filters and sorts that view. See the
[`AgentPanelEntry` projection](https://github.com/ogulcancelik/herdr/blob/e7fc85bfdb51f89488430adbfe5bbced3be79c2f/src/ui/sidebar.rs#L23-L40)
and
[`agent_entries` aggregation](https://github.com/ogulcancelik/herdr/blob/e7fc85bfdb51f89488430adbfe5bbced3be79c2f/src/ui/sidebar.rs#L112-L184).

That is the right ownership rule for Draxul:

```text
Space -> Tab -> Pane -> optional durable AgentIdentity
                         + ephemeral AgentRuntimeState

Agents sidebar = query/projection over all Pane occupants
AgentController = operations over those occupants, not duplicate ownership
```

An optional index keyed by `pane_id` or future `agent_instance_id` is useful for fast
lookup, but it must be rebuilt from pane-owned state and never serialized as a second
source of truth.

## Draxul's current position

Draxul v1 already has good snapshot primitives:

- `SessionSnapshot` is a pure value;
- each `TabSnapshot` owns a `PaneLayoutSnapshot`;
- pane snapshots contain stable pane strings and complete host launch descriptors;
- the recursive split tree, focus, zoom, tab IDs, and next-ID counters round-trip;
- named Sessions already use separate hashed TOML files;
- old `workspace` and `host_manager` wire keys remain readable despite the vocabulary
  rename.

The missing envelope is visible in
[`SessionSnapshot`](../app/session_state.h): v1 stores one tab collection directly.
[`App::can_snapshot_session_state()`](../app/app.cpp) consequently refuses to save when
more than one Space exists, correctly preventing silent data loss.

The main weaknesses to address are:

- direct truncating writes can leave a corrupt/empty state file after interruption;
- periodic checkpointing runs serialization and I/O synchronously;
- v1 validation is all-or-nothing;
- restoration only targets the active Space's `TabController`;
- one non-restorable host currently prevents the entire Session snapshot.

## Proposed version-2 model

Use stable IDs rather than Herdr's active indices, because Draxul already has them.

```cpp
struct AgentIdentitySnapshot
{
    std::string kind;          // "codex", "claude", or configured kind
    std::string display_name;  // optional user label
    std::string instance_id;   // optional stable routing identity
};

struct SpaceSnapshot
{
    SpaceId id = kInvalidSpaceId;
    std::string name;
    std::filesystem::path root_directory;
    int active_tab_id = -1;
    int next_tab_id = 0;
    std::vector<TabSnapshot> tabs;
};

struct SessionSnapshot
{
    int version = 2;
    std::string session_id;
    std::string session_name;
    SpaceId active_space_id = kInvalidSpaceId;
    SpaceId next_space_id = kDefaultSpaceId;
    std::vector<SpaceSnapshot> spaces;
};
```

`AgentIdentitySnapshot` should be optional on `PaneSnapshot`. The existing
`HostLaunchOptions` remains the launch source of truth; do not copy its argv, cwd, or
startup commands into agent metadata.

Do not persist:

- `AgentStatus` (`working`, `blocked`, `done`, `idle`, `unknown`);
- process IDs, handles, PTYs, terminal grids, activity timestamps, token observations,
  or hook authority;
- renderer/layout rectangles derived from the current window;
- native agent session references until explicit opt-in resume is implemented.

### TOML shape

Use new, honest v2 keys rather than retaining v1's vocabulary inside the new envelope:

```toml
version = 2
session_id = "default"
session_name = "default"
active_space_id = 1
next_space_id = 3

[[spaces]]
id = 0
name = "Draxul"
root_directory = "D:/dev/Draxul"
active_tab_id = 2
next_tab_id = 4

[[spaces.tabs]]
id = 2
name = "build"
name_user_set = true

[spaces.tabs.pane_layout]
# existing tree and pane encoding
```

The exact nested TOML table syntax should be locked by round-trip fixtures before
implementation. The reader selects a decoder from the top-level version; it should not
mix v1 and v2 field aliases throughout the core restore path.

## Format comparison

| Choice | Advantages | Disadvantages | Decision |
|---|---|---|---|
| Keep Draxul TOML and add v2 snapshots | No new dependency, preserves existing tests and files, readable, straightforward v1 migration | Explicit encode/decode boilerplate; nested arrays require careful fixtures | **Adopt** |
| Switch to Herdr-style JSON | Natural aggregate representation; Serde makes this concise in Rust; broad tooling support | Draxul does not gain Rust derives, must migrate files and tests, adds format churn without improving ownership | Reject for v2 |
| Split one file per Space | Localized writes and failures; possible future lazy loading | Cross-file transaction problems, ordering/active state coordination, orphan cleanup, harder Save As | Reject for local all-Space restore |
| Persist a separate agent list | Direct sidebar load | Duplicates pane identity and launch data; creates stale entries and reconciliation rules | Reject |

Herdr's single aggregate JSON file is clean because a saved Session is the transaction
boundary. The equivalent Draxul design is a single aggregate TOML file.

## Save transaction

1. Mark the Session dirty after durable mutations: Space/tab/pane creation, close,
   rename, reorder, focus changes worth restoring, split resize, and cwd/launch changes.
2. Debounce saves (Herdr uses five seconds). Keep shutdown and explicit Save As as
   immediate flushes.
3. On the app thread, walk **every** `SpaceController::spaces()` entry in display order:
   - reject or omit unsupported panes according to an explicit policy;
   - capture stable Space, tab, pane, focus, split, and launch data;
   - record `active_space_id` independently.
4. Pass the immutable `SessionSnapshot` to the persistence layer. No live controllers
   or hosts cross into the writer.
5. Serialize to a sibling temporary file, flush/close it, then replace the destination.
   Preserve the prior file if serialization or replacement fails. Use a Windows-safe
   replace helper rather than assuming POSIX rename-over-existing semantics.
6. If another change arrives during a write, retain the dirty flag and schedule a
   follow-up capture; never run two writes for the same Session concurrently.

Herdr follows the same capture/write separation and writes a temporary file before
renaming it into place. See its
[`save_snapshot_to_dir`](https://github.com/ogulcancelik/herdr/blob/e7fc85bfdb51f89488430adbfe5bbced3be79c2f/src/persist/io.rs#L44-L75).

### Restorable-host policy

For the first v2 implementation, preserve the existing safety rule: only write a
checkpoint when every pane in every Space is restorable. This is conservative and
simple.

Then make partial persistence an explicit follow-up rather than silently dropping
product panes. A future `PanePersistencePolicy` can distinguish:

- `Restorable`: save and respawn;
- `MetadataOnly`: save a placeholder/reopen descriptor;
- `Transient`: omit with a visible warning;
- `BlocksCheckpoint`: preserve the last known-good snapshot.

## Restore transaction

Restore must build all Spaces before choosing the active one:

1. Parse and validate a pure `SessionSnapshot`.
2. If v1:
   - decode with the existing reader;
   - wrap its tab collection in one default `SpaceSnapshot`;
   - set `active_space_id` to that Space;
   - do not rewrite the file merely because it was loaded or listed.
3. Create a temporary `SpaceController`/collection with no active host focus.
4. For each `SpaceSnapshot`, in saved order:
   - validate a unique non-negative Space ID;
   - restore its tabs using the existing `TabController::restore_tabs`;
   - use the Space root when resolving relative/default pane working directories;
   - keep each restored Space unfocused.
5. Apply partial failure rules:
   - skip a failed pane and prune invalid split branches where possible;
   - skip a tab only if no pane survives;
   - skip a Space only if no tab survives;
   - record one compact diagnostic summary for everything skipped.
6. If nothing survives, abandon the candidate and start the normal fresh default
   Session; do not overwrite the saved file during that startup.
7. Compute `next_space_id` as at least both the saved counter and one greater than the
   maximum surviving ID. Apply the same invariant to tabs and pane leaf IDs.
8. Choose the active Space by stable `active_space_id`; fall back to the first survivor.
9. Atomically swap the candidate collection into `App`, enable focus only for the
   chosen Space, rebuild shell/render/input routing once, and only then allow
   checkpointing.

Constructing a candidate before replacing the live collection also makes named Session
switch rollback simpler: retain the old controller until the target is fully usable.

## Phased implementation

### Phase 0: characterize v1 and restore invariants

**Status:** complete (2026-07-24)

- Add committed v1 TOML fixtures, including historical `workspace` and `host_manager`
  keys.
- Add corrupt, unsupported-version, duplicate-ID, missing-directory, and
  non-restorable-host cases.
- Extract version dispatch and value validation from file I/O.
- Specify whether current `TabController::restore_tabs` is transactional or mutates
  before failure; fix that boundary before composing multiple Spaces.

**Exit:** v1 compatibility and current failure behavior are pinned by tests.

Implementation notes:

- `decode_session_state()` now provides pure version dispatch and TOML decoding,
  independent of filesystem reads.
- `validate_session_snapshot()` rejects duplicate tab, layout-leaf, and stable pane
  identities, plus pane/layout mismatches.
- Missing working directories and non-restorable product hosts remain valid durable
  values at the codec boundary; runtime capture/restore policy owns those decisions.
- `TabController::restore_tabs()` builds a complete candidate collection before
  shutting down the live tabs. A failed candidate is cleaned up while the current
  collection and active tab remain intact.

### Phase 1: v2 values and codec

**Status:** complete (2026-07-24)

- Add `SpaceSnapshot` and the v2 `SessionSnapshot` envelope.
- Implement pure `encode_v2`, `decode_v2`, and `migrate_v1_to_v2`.
- Update `SessionSummary` to count Spaces, tabs, and panes.
- Add two-Space round trips with different active tabs, split trees, roots, names, and
  next-ID counters.
- Keep the v1 writer only in tests if needed; production writes v2 after the first
  successful capture.

**Exit:** a v1 file reads into the v2 value model and v2 round-trips without launching
hosts.

Implementation notes:

- `SpaceId` now has a dependency-light header shared by live and snapshot types.
- Production saves write v2 `spaces` / `spaces.tabs` / `pane_layout` tables; v1 wire
  keys remain isolated in the v1 decoder.
- Every v1 decode migrates into one default `SpaceSnapshot` in memory without
  rewriting the source file.
- Session summaries and listing output now report Space, tab, and pane totals.
- The codec round-trip covers two ordered Spaces with distinct roots, active tabs,
  split trees, names, and next-ID counters.

### Phase 2: capture and safely save every Space

**Status:** complete (2026-07-24)

- Add `SpaceController::snapshot_spaces()` or an equivalent pure traversal.
- Replace `space_controller_.count() == 1` with all-Space restorable validation.
- Capture the full ordered collection and active Space ID.
- Add dirty/debounced checkpoint scheduling.
- Add temporary-file replacement and failure tests.
- Prevent startup restore from immediately overwriting a source snapshot.

**Exit:** creating two or more Spaces produces one valid v2 Session file containing
all of them, and an interrupted write preserves the last good file.

Implementation notes:

- `SpaceController::snapshot_spaces()` captures every Space in stable display order,
  including inactive Spaces and their independent tab counters.
- Checkpointing is mutation-driven and debounced; a failed or concurrent save leaves
  the dirty generation pending for a later retry.
- Saves are written to a sibling temporary file, flushed and closed, then atomically
  replace the destination. A failed temporary write leaves the previous snapshot
  intact.
- Startup no longer immediately rewrites the snapshot it just decoded.
- Multi-Space named saves and normal checkpoints now write the complete live
  collection. Loading those snapshots becomes available in Phase 3.

### Phase 3: restore every Space

**Status:** complete (2026-07-24)

- Add a restore-oriented `SpaceController` factory or candidate-builder API that can
  install stable IDs and counters without replaying user actions.
- Restore all Spaces unfocused, then activate the saved stable ID.
- Add partial recovery and one summarized startup warning.
- Make named Session switching swap candidates transactionally.

**Exit:** restart restores every previously loaded Space, every surviving tab/pane
topology, and the previously active Space. Switching named Sessions cannot destroy the
current one when the target fails.

Implementation notes:

- `SpaceController::restore_spaces()` builds the complete candidate collection with
  saved IDs, roots, ordering, tab counters, and focus disabled before touching the
  live collection.
- The saved active Space is focused only after all candidates exist; a missing active
  Space falls back to the first usable restored Space.
- Runtime host failures recover at tab granularity. Failed tabs and wholly unusable
  Spaces are omitted, stable counters still advance past their saved IDs, and one
  summarized warning describes the recovery.
- If no candidate Space is usable, the current Space collection and its live hosts
  remain untouched.
- Named Session switching saves the current snapshot, constructs the target
  transactionally, and changes the active Session identity only after restore
  succeeds; it no longer needs destructive restore-and-rollback.

### Phase 4: durable agent identity and sidebar projection

**Status:** complete (2026-07-24)

- Add optional `AgentIdentity` to live pane metadata and `AgentIdentitySnapshot` to the
  pane snapshot.
- Populate it first for agents launched explicitly by Draxul.
- Add an `AgentQuery`/projection that walks all Spaces and emits sidebar rows with
  stable routing: Space ID, tab ID, pane ID, display name, kind, and current ephemeral
  status.
- Let `AgentController` focus and operate on the resolved pane occupant.
- Rebuild any lookup index after restore; do not serialize the index or sidebar rows.

**Exit:** explicitly launched agents reappear as identities after restart and the
Agents section derives its rows from restored pane occupants without duplicated state.

Implementation notes:

- `AgentIdentity` is optional pane-owned metadata containing kind, display name, and
  a stable per-Session instance ID. It round-trips inside the pane snapshot.
- `AgentController` walks the authoritative Space -> tab -> pane hierarchy to derive
  ordered rows with stable routing fields and live `running` / `focused` state.
  Runtime state is not serialized.
- Clicking an Agent row resolves its instance ID and activates the owning Space, tab,
  and pane. No sidebar lookup table is persisted.
- `launch_agent` prompts for a local command, creates a shell split rooted in the
  current Space, starts the command through shell startup input, and attaches the
  durable identity. Restoring the topology launches a fresh command; it does not
  claim native process or conversation resume.
- The Agents rail uses the shared segmented pill layout. Its accent follows the
  pane-green role when focused, and an unavailable host is labeled `[exited]`.

### Phase 5: hardening and later resume hooks

**Status:** complete (2026-07-24)

- Add optional `.bak` recovery only if field evidence shows temp replacement is
  insufficient.
- Add schema-size limits and sensitive-field logging tests.
- Add native `AgentSessionRef` only alongside opt-in, product-specific resume commands,
  uniqueness/deduplication, and safe fallback to an ordinary restored shell.
- Keep terminal screen history off by default and separate from topology if ever added.

**Exit:** recovery behavior is diagnosable, bounded, and does not imply that processes
survive application exit.

Implementation notes:

- File loads reject snapshots larger than 4 MiB before allocating the parse buffer;
  direct decode enforces the same limit before TOML parsing. Value validation limits
  a Session to 64 Spaces, 128 tabs per Space, 256 panes per tab, 64 split-tree
  levels, 256 command-list entries, and bounded text fields.
- Parse and value-validation diagnostics name the violated structure or field without
  including its saved command, argument, or path content. Tests pin that behavior.
- A `.bak` file is not added: sibling-temp write plus atomic replacement already
  preserves the last good snapshot across known write failures, and there is no field
  evidence yet for a second recovery artifact and its lifecycle complexity.
- `AgentSessionRef` and native conversation resume are not added because no supported
  product-specific opt-in resume adapter exists. Restored agent panes launch their
  ordinary saved command as a fresh process, with safe shell behavior rather than an
  implied conversation continuation.
- Terminal screen history remains absent from the topology snapshot and off by
  default.

## Required tests

- v1 fixture migration into one default Space;
- v2 two-/three-Space round trip and stable ordering;
- inactive Spaces included in capture;
- saved active Space restored after all Spaces exist;
- missing active Space falls back deterministically;
- duplicate IDs rejected or repaired according to one documented rule;
- one failed pane prunes its branch without dropping unrelated Spaces;
- total restore failure retains the source file and creates a fresh runtime;
- next-ID counters cannot collide after partial restore;
- temp-write/replace failure leaves the previous snapshot readable;
- a change during a write schedules another save;
- named Session load rollback retains the old live controller;
- agent sidebar projection contains exactly the pane-owned agent identities;
- volatile agent status is never serialized.

Run at minimum:

```text
py do.py test
py do.py smoke
py do.py renderall
```

The render suite should include multiple restored Space pills and populated Agent rows
once Phase 4 lands.

## Completion validation

Completed on 2026-07-24:

- `py do.py test`: all 16 unit-test shards passed;
- `py do.py smoke`: the Debug executable smoke test passed;
- `py do.py renderall`: all five deterministic render comparisons passed;
- `py do.py hygiene`: repository hygiene passed;
- `git diff --check`: no whitespace errors in the scoped changes.

## Maintenance assessment

The recommended design is cleaner than both Draxul v1 and a literal Herdr port:

- it preserves Draxul's stable IDs instead of translating active indices;
- it reuses the tested split-tree and launch snapshot code;
- it has one owner for each fact;
- it isolates compatibility code at the decoder boundary;
- it makes file encoding replaceable without coupling it to restoration;
- it supports partial recovery without importing Herdr's server, PTY, and runtime-ID
  complexity.

The one deliberate cost is explicit C++ serialization code. Keep it maintainable with
round-trip fixtures, small per-type codec functions, and validation adjacent to each
snapshot type. If boilerplate becomes painful later, change the codec behind these
pure snapshot types; do not change the ownership model.

## Related material

- [Herdr research and Draxul terminology alignment](herdr-agent-harness-research.md)
- [App shell layout](app-shell-layout.md)
- [Draxul session snapshot](../app/session_state.h)
- [Draxul session codec](../app/session_state.cpp)
- [Draxul Space ownership](../app/space_controller.h)
- [Herdr repository](https://github.com/ogulcancelik/herdr/tree/e7fc85bfdb51f89488430adbfe5bbced3be79c2f)
