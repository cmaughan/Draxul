## Review — dynamic plugin hot reload (`kanban/pending/41 dynamic-plugin-hot-reload -feature.md`)

The feature is largely implemented and the single-host reload paths are well tested. What follows is what still needs fixing, most severe first.

---

### 1. A stale `error_` survives a successful reload — the pane never recovers

`libs/draxul-host/src/plugin_host.cpp:603, :667`

`error_` is set by `run_tick` and `accept_render_result`, and is **never cleared** anywhere in the file (only assignments exist; no `error_.clear()`). `start_instance` and `reload` don't reset it.

So after a plugin's tick or render fails, the developer rebuilds, publishes, and reloads successfully — but `status_text()` still returns the previous build's error (`:1180`) and `runtime_state().content_ready` stays `false` forever (`:1197`). This is the primary hot-reload workflow, and it silently reports the pane as still broken.

**Fix:** clear `error_` on a successful `start_instance`.

---

### 2. `reload_plugin` cannot reach a pane that fell back to `UnavailableHost`

`app/app.cpp:4618-4632`, `app/pane_manager.cpp:1403-1416`

When a plugin fails to load or start, the pane is replaced by an `UnavailableHost` placeholder, which carries only a message string — no plugin id (`libs/draxul-host/include/draxul/unavailable_host.h:36-38`). `App::reload_plugin` collects hosts via `dynamic_cast<PluginHost*>`, so those panes are invisible to it.

Publish a broken build → panes degrade → fix the build → `draxul plugin reload <id>` answers *"No local pane is running plugin 'x'."* The recovery that does work (`draxul pane restart`, which re-runs `create_host_for_leaf` from the saved `launch_options_`) is neither mentioned in the error nor in `docs/features.md:52-54`, which claims the placeholder is "actionable".

**Fix:** carry the requested plugin id on the placeholder (or read `launch_options_`), have `reload_plugin` re-create those leaves via `PaneManager::restart_leaf`, and count them in `matched`. At minimum, make the error name the recovery command.

---

### 3. Cohort rollback can land on mixed generations — the state the design forbids

`app/app.cpp:4671-4681` → `libs/draxul-host/src/plugin_host.cpp:467-486`

The rollback loop calls `reload(previous)` on already-swapped hosts. Inside `PluginHost::reload`, if `start_instance(previous)` fails, the fallback at `:472` is `start_instance(old_plugin)` — and in a rollback call `old_plugin` is the **new** generation. That pane comes back on the rejected build while its siblings are on the old one, which `plans/dynamic-plugin-hot-reload.md:131` explicitly rules out. The summary still reports `rolled_back = true, reloaded = 0`, which is now untrue.

**Fix:** on rollback failure, leave the pane in an explicit failed state rather than restarting the rejected generation, and report per-pane outcomes instead of a single boolean.

---

### 4. Storage-journal commit silently drops the remainder on the first I/O error

`libs/draxul-host/src/plugin_host.cpp:489-520`

`commit_storage_overlay` returns on the first failed write/remove and then calls `storage_overlay_.clear()`, discarding every un-applied entry. Because the overlay is an `unordered_map`, *which* writes landed is nondeterministic. The caller downgrades this to a warning (`app/app.cpp:4712-4717`), so a partially committed, partially lost plugin state is reported as a successful reload.

**Fix:** continue applying the remaining entries and aggregate the errors, or stage the whole commit so it is genuinely all-or-nothing.

---

### 5. Orphaned staging directories are never reclaimed

`libs/draxul-plugin/src/plugin_manager.cpp:150, :213, :235`

The runtime directory is per-process (`plugin-runtime/process-<pid>-<gen>`), and both the destructor and `discover()` only ever remove *this* process's directory. `plans/dynamic-plugin-hot-reload.md:217` requires "next-start cleanup removes host-private generations" — that is not implemented.

Every crash or hard kill leaves a full copy of each loaded package (module + shaders + assets) in the user cache dir permanently. Compounding it: `prepare_reload` pushes every candidate into `resident_` (`:461-464`) regardless of whether the cohort later rolls back, so a dev session of repeated reloads accumulates one staged copy and one loaded module per attempt, uncapped.

**Fix:** sweep `<cache>/draxul/plugin-runtime/process-*` at `discover()` time, removing entries whose pid is not alive.

---

### 6. `publish_plugin.py` retention can delete the generation it just published

`tools/publish_plugin.py:46, :75-81`

`build_id` is derived from wall-clock `time.time_ns()`; retention sorts by name descending and deletes `retained[3:]`. If the clock steps backwards (NTP correction, VM restore, dual-boot), the new build sorts below the three existing generations and is pruned immediately *after* `current.json` was pointed at it.

The consequence is not graceful: `published_manifest` deliberately treats a present pointer as authoritative (`plugin_manager.cpp:181-184`), so discovery surfaces "Invalid plugin manifest metadata" and the plugin is unloadable until the next build. Separately, `shutil.rmtree(..., ignore_errors=True)` can leave a half-deleted generation with no diagnostic at all.

**Fix:** never prune the generation named in `current.json`; derive the ordering key from a monotonic counter over the existing generation list rather than the wall clock; log prune failures instead of swallowing them.

---

### 7. Same-name plugin-private dependencies defeat shadow-copying

`libs/draxul-plugin/src/plugin_manager.cpp:43-50`, `:384-387`

Staging copies the whole package, but on Windows the loader resolves an already-loaded DLL by module base name — `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` does not help once an image with that name is resident. A second generation shipping `foo.dll` silently reuses generation one's `foo.dll`. macOS has the analogous install-name problem.

`plans/dynamic-plugin-hot-reload.md:52-55` states this as a plugin-authoring requirement, but nothing validates it and nothing documents it — `docs/features.md` and the SDK headers say nothing about static linking or generation-unique dependency names. This is exactly the still-unchecked box *"Confirm Windows replacement avoids locking build output and macOS loads a distinct staged image"*.

**Fix:** detect sibling shared libraries at stage time and log a WARN naming them; document the rule in the SDK docs.

---

### 8. The cohort swap has no test coverage

`tests/plugin_manager_tests.cpp:782-938` covers single-host reload, state transfer, and single-host rollback well. But `grep -rn "reload_plugin" tests/` returns only `control_plane_tests.cpp:81`, which asserts a method name.

The multi-pane logic in `app/app.cpp:4642-4718` — partial-failure rollback, resume of not-yet-reloaded hosts, deferred storage finalize — is the riskiest code in the feature (findings 3 and 4 both live there) and is entirely untested. The kanban file tracks the render-smoke and two-client gaps but not this one.

**Fix:** add a two-pane cohort test using the existing spinning-triangle fixture: success, mid-cohort failure with rollback, and rollback-failure. Add the missing checkbox to the kanban file.

---

### Lower priority

- **`plugin.reload` doesn't validate the id** (`app/app.cpp:5541-5552`) — `PluginManager::valid_plugin_id` exists and is unused on this path. Harmless today since the id only matches live hosts, but it's a free consistency check.
- **`published_manifest` fallback misattributes the id** (`plugin_manager.cpp:184` → `parse_manifest:136-139`): when the pointer is dangling, the manifest is indexed under the *directory name*. If that differs from the real plugin id, `load()` reports "Plugin is not installed" instead of the actual publication error.
- **`pane restart` and `plugin reload` have different generation semantics**: `PluginManager::load` returns the cached `loaded_` entry before refreshing (`:344-347`), so restarting a plugin pane reuses the old generation while reload picks up the new one. Defensible, but undocumented and surprising.

---

Two kanban boxes are already open and consistent with the above: Windows/macOS staged-image confirmation (relates to finding 7) and Vulkan/Metal reload render-smoke plus two-client coverage (relates to finding 8). Findings 1–4 are new and would be my fix order.
