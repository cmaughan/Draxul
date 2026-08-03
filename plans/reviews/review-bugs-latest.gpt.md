# Bug review

Reviewed 696 source files under `app/`, `libs/`, `shaders/`, `tests/`, and `scripts/` directly from disk. No reproducible CRITICAL issue was found. Repository files were not edited.

## HIGH

### 1. Restored companion panes retain stale owner IDs

**File:** [libs/draxul-server/src/session_topology_bridge.cpp:301](/D:/dev/Draxul/libs/draxul-server/src/session_topology_bridge.cpp:301)

**What goes wrong:** Restore assigns every pane a new scoped ID at line 261, but copies `companion_owner_pane_id` unchanged. After restarting a session containing a Kanban pane and Markdown companion, the companion references the pre-restart owner ID. `PaneManager` cannot find that owner, so preview tracking, toggling, and lifecycle behavior break.

**Suggested fix:** Restore panes in two passes: build `saved pane_id -> scoped pane_id`, then translate every companion-owner reference through that map and reject missing owners.

### 2. Closing an owner pane leaves orphaned companions

**File:** [libs/draxul-server/src/topology_service.cpp:937](/D:/dev/Draxul/libs/draxul-server/src/topology_service.cpp:937)

**What goes wrong:** `ClosePane` removes only the requested pane. Closing a Kanban owner while its Markdown companion exists leaves the companion pointing to a nonexistent owner. With only those two panes, the orphan becomes the final pane and cannot itself be closed through `ClosePane`.

**Suggested fix:** Before erasing, either cascade-close panes whose `companion_owner_pane_id` matches the owner, or reject the operation until dependents are closed; preserve the final-pane invariant.

### 3. Shared preview is changed locally before the server accepts the update

**File:** [app/app.cpp:2526](/D:/dev/Draxul/app/app.cpp:2526)

**What goes wrong:** The Markdown host is refreshed before `UpdateClientPane` is enqueued, and the refresh result is ignored. If the queue is full or the server later rejects the command, the local UI displays the new card while authoritative topology still names the old one. Because the server signature did not change, later polling does not repair the divergence.

**Suggested fix:** Let the accepted server snapshot drive the local refresh, or retain the old source and roll back on both enqueue and completion failure.

### 4. Local preview refresh does not update persisted launch state

**File:** [app/pane_manager.cpp:488](/D:/dev/Draxul/app/pane_manager.cpp:488)

**What goes wrong:** Reusing a local Markdown preview dispatches `open_file:` but neither checks its result nor updates `launch_options_.source_path`. Open card A, switch the preview to card B, then restart: the host displayed B, but the session restores A. The existing-preview path also is not marked session-dirty.

**Suggested fix:**

```cpp
if (!refresh_markdown_preview(path))
    return kInvalidLeaf;
return markdown_preview_leaf_;
```

Also mark the session dirty after a successful local refresh.

### 5. Failed projected Markdown loads are recorded as successful

**File:** [app/pane_manager.cpp:875](/D:/dev/Draxul/app/pane_manager.cpp:875)

**What goes wrong:** During topology reconciliation, the return value from `dispatch_action("open_file:...")` is ignored and the new source is stored anyway. If another client selects a file absent or unreadable on this machine, the host keeps showing the old document while its metadata and committed topology signature claim the new file is loaded. No subsequent snapshot retries it.

**Suggested fix:** Only update `launch_options_` after a successful dispatch; otherwise fail reconciliation with context and keep the previous signature.

### 6. Working-directory changes are silently ignored by live projections

**File:** [app/pane_manager.cpp:859](/D:/dev/Draxul/app/pane_manager.cpp:859)

**What goes wrong:** The host-reuse predicate compares kind, terminal ID, client kind, and source, but not `working_dir`. A valid `UpdateClientPane` changing `D:/one` to `D:/two` updates the server and projection signature, yet the existing host and its launch metadata remain at `D:/one`. A later client-local restart therefore launches in the wrong directory.

**Suggested fix:**

```cpp
|| launch->second.working_dir != pane->second->launch.working_dir
```

Recreate the host when this launch-only field changes.

### 7. Project-board synchronization only reads the first 100 items

**File:** [scripts/sync_project_board.py:75](/D:/dev/Draxul/scripts/sync_project_board.py:75)

**What goes wrong:** The repository currently contains 193 syncable kanban cards, but `get_existing_items()` fetches only `items(first: 100)` with no cursor pagination. Existing items beyond that page are treated as missing and recreated as duplicate drafts on every synchronization run.

**Suggested fix:** Request `pageInfo { hasNextPage endCursor }`, supply `after: $cursor`, and accumulate pages until `hasNextPage` is false.

## MEDIUM

### 8. Pending preview state is shared across every Space and tab

**File:** [app/app.h:342](/D:/dev/Draxul/app/app.h:342)

**What goes wrong:** The pending flag, path, and close-after-create flag are application-global. If tab A has an asynchronous preview split pending, switching to tab B makes `is_markdown_preview_visible()` return true there as well. Pressing the preview toggle in B marks A’s pending preview for closure; when A’s command completes, the app switches back to A and closes the newly created preview.

**Suggested fix:** Key pending preview state by Space, tab, owner pane, and command ID; only expose it as visible for the matching active target.

### 9. Structural signatures can collide because fields are not escaped

**File:** [libs/draxul-client/src/topology_projection.cpp:136](/D:/dev/Draxul/libs/draxul-client/src/topology_projection.cpp:136)

**What goes wrong:** Arbitrary path strings are concatenated with `:` and `;`. On POSIX, these two valid descriptors produce the same signature:

- Working directory `a:b`, source `c`
- Working directory `a`, source `b:c`

When the server changes between them, `project_tab()` concludes that nothing structural changed and skips reconciliation, leaving the old source and directory active.

**Suggested fix:** Length-prefix every string, hash a structured serialization, or compare typed descriptor snapshots instead of delimiter-concatenated text.

