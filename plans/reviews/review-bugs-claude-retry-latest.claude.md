# Draxul bug review (whole repository, from the packed source file)

**What this review covered:** I read the single packed source file only (1,783 files, about 424k lines). Nothing was built, run or edited.

**How it was done:** I mapped the architecture from `CLAUDE.md`, `AGENTS.md`, `docs/module-map.md` and the Kanban boards. Four read-only subagents, using the same model and told to use high effort, then reviewed separate areas. A subagent's model can be set directly, but its reasoning effort could only be requested in its prompt. I re-read the code behind every accepted finding. Claims that did not hold up were dropped or moved to the unresolved-leads list.

**Already-tracked work:** I checked the root Kanban lanes (pending cards 01–68 and done cards 61, 62 and 66) and every product board. No accepted finding repeats an existing bug card. The root `kanban/.draxul-kanban.toml` still lists older card names that have no card file in any lane, so I did not treat them as tracked. Where a finding might overlap one of them, I say so.

## Accepted findings (ranked by severity)

### CRITICAL

#### C1. Any non-default glyph atlas size breaks text rendering
**Summary:** Text rendering breaks, or the GPU writes out of bounds, whenever the font-atlas size setting is changed from its default, because the font side always assumes the default size.
- **File and line:**
  - The GPU texture is created from the config value: `app/app.cpp:482-483` calls `create_renderer(config_.atlas_size, …)`; see also `libs/draxul-renderer/src/vulkan/vk_atlas.cpp:37` and `libs/draxul-renderer/src/metal/metal_renderer.mm:395-399`.
  - The font cache never receives that value: `libs/draxul-font/src/glyph_atlas_manager.h:27` calls `glyph_cache_.initialize(primary_face, point_size)`, and the default is 2048 (`libs/draxul-font/src/font_engine.h:116`).
  - The full upload uses the cache's 2048×2048 size: `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp:336-337`.
  - The upload copies with no bounds check: `vk_atlas.cpp:262-268`.
- **Severity:** CRITICAL
- **What goes wrong:** `atlas_size` is documented as 1024–8192.
  - With `atlas_size = 1024`, a 2048×2048 copy goes into a 1024×1024 image. That is an out-of-bounds GPU copy (undefined behaviour, possibly device loss); on Metal it fails blit validation.
  - With `4096` or `8192`, texture coordinates are computed against 2048 (`glyph_cache.cpp:564`), so every glyph samples the wrong texels on both backends.
- **Suggested fix:** Pass `atlas_size` through `TextServiceConfig` into `GlyphCache::initialize`, and reject or clamp upload rectangles larger than the texture in both backends.

#### C2. A replaced ("evicted") server overwrites the new server's saved sessions with stale state
**Summary:** An old server that has been replaced still writes its outdated window layout over the new server's saved files, so recent layout changes can be lost on the next start.
- **File and line:** `libs/draxul-server/src/server_kernel_lifecycle.cpp:395-416` (eviction sets `stop_requested` and breaks), then `:651-698`, which still runs `process_pending`, an unconditional `checkpoint_session` for every session, and a second "final" checkpoint pass.
- **Severity:** CRITICAL (overwrites saved data)
- **What goes wrong:**
  1. The metadata or runtime directory is replaced; the code comment says this happened in practice.
  2. A successor server starts and checkpoints its sessions.
  3. Within about one eviction interval, the old server sees two failed identity checks, leaves the loop, and rewrites the same session files with its stale topology.
  4. The successor only rewrites when its revision changes, so the stale file persists and is restored on the next launch. Both processes also use the same `.tmp` name, so their writes can interleave.
- **Suggested fix:** Record an `evicted` flag. When it is set, skip `process_pending` and all checkpoint scheduling and writes after the loop.

#### C3. MegaCity frees its debug-panel images into another pane's graphics memory pool
**Summary:** Closing one MegaCity pane while another is open releases its debug-panel images through the other pane's interface state, which corrupts graphics state and can crash.
- **File and line:**
  - `plugins/megacity/product/draxul-megacity/src/megacity_host.cpp:1197-1211`: `scene_pass_.reset()` runs before `imgui_.destroy()`, with no switch to this pane's ImGui context.
  - `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp:2016-2060` calls `ImGui_ImplVulkan_RemoveTexture`, guarded only by "some context exists".
  - `plugins/support/imgui/src/plugin_imgui_context.cpp:72-88`: `begin_frame` sets the global current context and never restores the previous one.
- **Severity:** CRITICAL (invalid Vulkan usage, possible crash)
- **What goes wrong:**
  1. Open two MegaCity panes; they share one module and so one global ImGui context pointer. With UI panels on (the default), both register G-buffer debug textures.
  2. Close pane A after pane B drew last.
  3. A's descriptor sets are freed (`vkFreeDescriptorSets`) into B's descriptor pool.
- **Suggested fix:** In `shutdown()`, call `ImGui::SetCurrentContext(imgui_.context())` before `scene_pass_.reset()`, or store the owning context with each registered texture.

#### C4. SatView has the same wrong-pool release, and can crash on a null pointer
**Summary:** Closing one SatView pane while another exists has the same wrong-owner problem, and can crash outright if the other pane has never drawn.
- **File and line:**
  - `plugins/satview/src/runtime/satview_runtime.cpp:753-776`: `scene_pass_.reset()` at line 766 runs before `imgui_.destroy()` at line 775.
  - `plugins/satview/src/render/satview_render_vk.cpp:369-400`: only checks `ImGui::GetCurrentContext()`, not whether a backend is initialised.
- **Severity:** CRITICAL
- **What goes wrong:**
  - If another SatView pane's context is current, the sets are freed into the wrong pool (as in C3).
  - If that pane was created but has not rendered yet (for example in a hidden tab), its backend data is null, and `RemoveTexture` dereferences null on pane close.
- **Suggested fix:** Same as C3, and also require `ImGui::GetIO().BackendRendererUserData != nullptr`.

#### C5. Rezonality throws C++ exceptions through the plugin's C interface on Windows paths with non-ASCII characters
**Summary:** On Windows, a Rezonality project stored under a folder name with accented or Asian characters can crash the whole app during plugin reload, because a text conversion error escapes the plugin.
- **File and line:**
  - `plugins/rezonality/src/rezonality_plugin.cpp:551-564` (`export_reload_json` builds JSON from `project_path.generic_string()` with no try/catch).
  - Other unguarded entry points: `:500` (`get_presentation_state`) and the `create_instance` → `DiagnosticsPublisher` path.
  - Host call site with no handler: `libs/draxul-host/src/plugin_host.cpp:291-305`.
- **Severity:** CRITICAL
- **What goes wrong:**
  - On Windows, `generic_string()` converts to the ANSI code page. A path like `C:\Users\José\…` gives byte 0xE9, and `json::dump()` then throws `type_error.316` (invalid UTF-8).
  - A character outside the code page makes `string()` itself throw `system_error`.
  - Either exception unwinds out of the plugin entry point into host code with no handler, so the app terminates on hot reload.
- **Suggested fix:** Use `u8string()` for any path that reaches JSON, and wrap every exported entry point in `try { … } catch (...) { return 0; }`.
- **Possible overlap:** the board config lists an older name, `43 rezonality-diagnostic-utf8 -bug`, but no such card file exists.

#### C6. Kanban and Markdown views crash on Windows when a file name has characters outside the code page
**Summary:** On Windows, a Kanban card or Markdown file whose name contains an emoji or Asian characters can crash the app, because a file-name conversion throws an error nothing catches.
- **File and line:**
  - `modules/kanban/draxul-kanban/src/kanban_store.cpp:578, 616, 658, 704` (`entry.path().filename().string()`).
  - `modules/kanban/draxul-kanban/src/kanban_host.cpp:228`.
  - `modules/markdown/draxul-markdown/src/markdown_host.cpp:135, 367, 372`.
- **Severity:** CRITICAL (crash, Windows only)
- **What goes wrong:**
  - On a non-UTF-8 code page, a card named `07 🐛 crash -bug.md` makes MSVC's `path::string()` throw `std::system_error`. The file monitor reloads automatically when such a file appears.
  - There is no try/catch anywhere in the modules, and the app has no top-level handler, so the app hits `std::terminate`.
  - Names that do fit the code page are displayed as mojibake.
  - macOS is unaffected because its paths are UTF-8.
- **Suggested fix:** Convert with `u8string()` or a shared UTF-8 path helper, and catch exceptions around `reload_board`.

### HIGH

#### H1. Releasing the mouse over the tab bar, a pane label or an overlay leaves drags stuck
**Summary:** If you let go of the mouse over the tab bar or a pane label, the app never sees the release, so the split divider keeps resizing (or the selection keeps growing) as the mouse moves.
- **File and line:** `app/input_dispatcher.cpp:451-456` (overlay), `:536-540` (pill) and `:545-554` (chrome) all return before the release handling at `:583-589`. The divider drag is driven from `:626-644`.
- **Severity:** HIGH
- **What goes wrong:**
  - Drag a split divider into the tab bar and release there: `drag_divider_id_` stays set, and every later mouse move (with no button held) keeps resizing the split.
  - Drag-selecting text upward past the tab bar loses the release too, so the host's selection or mouse-report state remains in "pressed".
- **Suggested fix:** Handle left-button release (clear `drag_divider_id_` and `dragging_shell_divider_`, and send the release to the host that captured the press) before any chrome, pill or overlay early return.

#### H2. While a pane is zoomed, moving the mouse can silently move keyboard focus to a hidden pane
**Summary:** With one pane zoomed to fill the window, simply moving the mouse can send your typing to a pane you cannot see, because hit-testing still uses the normal split layout.
- **File and line:** `app/pane_manager.cpp:1190-1201` (`host_at_point` calls `update_focus` with no `zoomed_` check) and `:1203-1213` (`divider_at_point`). It is called on every mouse move from `app/input_dispatcher.cpp:658` via `host_for_mouse_pos` (`:113-129`).
- **Severity:** HIGH
- **What goes wrong:**
  1. Zoom a pane.
  2. Move the mouse over the area where another pane sits in the hidden layout.
  3. Focus and `deps_.host` switch to that invisible pane, and keystrokes go there.
  - Hidden divider positions inside the zoomed pane also capture clicks.
- **Suggested fix:** When `zoomed_` is set, return the zoomed leaf's host without changing focus, and make `divider_at_point` return `nullopt`.

#### H3. Wide (CJK and emoji) characters in Neovim shift the rest of the line one column right
**Summary:** Lines in Neovim that contain Asian characters or emoji draw with gaps and shifted text, because each wide character is moved past twice.
- **File and line:** `libs/draxul-nvim/src/ui_events.cpp:285-297` (`set_cell(...); col++; if (dw) col++;`).
- **Severity:** HIGH
- **What goes wrong:**
  - Neovim's `ext_linegrid` protocol sends the right half of a double-width character as an explicit `""` cell.
  - The code already advances two columns for the wide character, so the `""` cell lands one column too far and advances again.
  - Every wide character shifts the rest of the row by one, truncating the end of the line and misaligning later partial updates and the cursor.
  - The unit tests in `tests/ui_events_tests.cpp` (about lines 288-346) feed wide characters without the `""` follower, so they encode the wrong assumption. The real-Neovim `unicode-view` render reference should be re-checked.
- **Suggested fix:** Advance exactly one column per protocol cell, and treat a `""` cell that follows a wide leader as its continuation.

#### H4. MegaCity can replace the new city grid with routes computed on the old one
**Summary:** After a rebuild while a building is selected, MegaCity can draw routes for the old city or even revert to the old city grid.
- **File and line:** `plugins/megacity/product/draxul-megacity/src/megacity_host.cpp`:
  - `:1367-1374` and `:1416-1417` (a rebuild keeps the old grid without routes, then launches a new grid build);
  - `:1541-1556` (the pump requests routes on whatever grid is current);
  - `:727-745` (`consume_completed_routes` sets `city_grid_ = result->grid` unconditionally).
- **Severity:** HIGH
- **What goes wrong:**
  1. A building is selected and a rebuild runs (the Rebuild button, or an automatic rebuild after a config edit).
  2. The pump sees the old grid, now with no routes, and requests routes on it under the new generation.
  3. If the new grid finishes first, the route result then reverts `city_grid_` to the old grid. Otherwise the routes drawn belong to the old grid and are never recomputed.
- **Suggested fix:** Tag grids with their build generation, don't request routes until the grid matches the current layout, and drop route results whose source grid is no longer current.

#### H5. Installing agent hooks can delete the user's Claude or Codex config file
**Summary:** If replacing a Claude or Codex settings file fails twice in a row, Draxul deletes the user's original file and then its own copy too, so the settings are lost.
- **File and line:** `libs/draxul-agent-integration/src/agent_integration.cpp:190-204`. After a failed rename it calls `remove(path)`, then renames again, and if that also fails it calls `remove(temporary)`.
- **Severity:** HIGH (data loss)
- **What goes wrong:** On Windows, antivirus or a search indexer briefly holds `.draxul.tmp` open. Both renames fail, but deleting the destination succeeds. `settings.json`, `hooks.json` or `config.toml` is removed, and then the temp copy is deleted too.
- **Suggested fix:**
  - Never remove the destination.
  - On Windows, use `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` with a short retry.
  - Keep the temp file when replacement fails.
- **Related card:** `kanban/pending/11 atomic-file-replacement -refactor.md` plans to share this code but explicitly keeps each caller's current behaviour, so this bug is not tracked.

### MEDIUM

#### M1. A server-status request is silently lost when a queued layout command fails in the same pass
**Summary:** Choosing "Server status" or "Stop server" can silently do nothing when a queued layout command fails at the same moment, which is exactly when the server is misbehaving.
- **File and line:** `libs/draxul-client/src/remote_session_client.cpp:884-888` pops `status_id`; then the failed command takes the `continue` at `:891-902`, which skips the publish at `:906-917`.
- **Severity:** MEDIUM
- **What goes wrong:** No status completion is ever published, and the matching pending server-status action is never completed.
- **Suggested fix:** Publish the status before the `continue`, or push it back onto the front of `statuses_`.

#### M2. The server's main loop can miss wake-up signals
**Summary:** New requests, terminal output, or a stop signal can sit unnoticed for up to a second, because the server's wake-up signal can be missed.
- **File and line:**
  - The notifiers don't take the loop mutex: `libs/draxul-server/src/server_kernel_lifecycle.cpp:313-319`, `:332-339` and `:713-717`; `libs/draxul-server/src/server_kernel_terminals.cpp:119-125`.
  - The waiter: `server_kernel_lifecycle.cpp:643-649`.
- **Severity:** MEDIUM
- **What goes wrong:**
  - A flag is set and notified after the waiter has checked its predicate but before it blocks, so the notification is lost and the loop sleeps the full idle interval (1 s).
  - Terminal output notifies only on the false→true transition, so a whole output burst stalls.
- **Suggested fix:** Lock `loop_wake->mutex` (an empty lock section is enough) between setting the flag and calling `notify`.

#### M3. The session-poll worker ignores pending wake-ups, delaying shutdown and input
**Summary:** Closing the window or typing can wait up to five seconds behind a background poll that ignores wake-up requests.
- **File and line:** `libs/draxul-client/src/remote_session_coordinator.cpp:2503-2505` and `:2558-2559` (`worker_wake_.wait_for(lock, delay)` with no predicate, and `worker_pending_` is never cleared). The correct pattern is `wait_for_worker` at `:2337-2356`.
- **Severity:** MEDIUM
- **What goes wrong:** If `stop()` or new input arrives between the `stopping_` check and taking the mutex, the wake is lost. The UI-thread join then waits up to the recovery backoff (as much as 5 s), or input is delayed.
- **Suggested fix:** Use the predicate form (`worker_pending_ || stopping_`) and clear `worker_pending_`.

#### M4. The Windows control server can spin a CPU core forever on its first pipe instance
**Summary:** On Windows, a client that connects and immediately disconnects can leave the server busy-looping on one CPU core for the rest of its life.
- **File and line:** `libs/draxul-control/src/control_transport_win32.cpp:499-535` and `:582-587`. Errors from `ConnectNamedPipe` other than "already connected" or "pending" fall through with no `DisconnectNamedPipe` and no sleep while `keep_first_instance` is true.
- **Severity:** MEDIUM
- **What goes wrong:** A client connects and closes before `ConnectNamedPipe` runs. The call then returns `ERROR_NO_DATA`, and keeps returning it until `DisconnectNamedPipe` is called, so the loop spins and the listener loses one of its slots.
- **Suggested fix:** On those errors call `DisconnectNamedPipe(pipe)`, record `listener_error_`, and back off briefly.

#### M5. Keyboard chords ignore which prefix key started them
**Summary:** With two shortcut prefixes configured, the second key can trigger the shortcut that belongs to the other prefix.
- **File and line:** `app/input_dispatcher.cpp:328-349` matches only the second key; the prefix is never recorded at `:360-386`.
- **Severity:** MEDIUM
- **What goes wrong:** With bindings `Ctrl+S, z` and `Ctrl+B, z`, pressing `Ctrl+B` then `z` runs whichever binding comes first in the list.
- **Suggested fix:** Store the active prefix key and modifiers, and also require them to match in the chord loop.

#### M6. Neovim screen updates are dropped under load and never recovered
**Summary:** When the UI stalls during heavy Neovim output, screen updates are thrown away, leaving the display out of sync with Neovim.
- **File and line:** `libs/draxul-nvim/src/rpc.cpp:366-373`: when the queue is full, the oldest notification is popped.
- **Severity:** MEDIUM
- **What goes wrong:** The UI thread is blocked (for example a Windows modal resize) while Neovim sends more than 4096 notifications. Lost `grid_line`, `grid_scroll` or `hl_attr_define` events leave the grid permanently wrong until a full redraw.
- **Suggested fix:** Don't drop redraws. Apply backpressure, or on overflow request a full redraw or re-attach.
- **Possible overlap:** the board config lists an older name, `15 rpc-queue-backpressure -test`, which has no card file.

#### M7. Terminal shell-integration marks can hit signed integer overflow
**Summary:** A deliberately huge scroll or insert-line count from a terminal program can push a saved prompt-marker row past the integer limit, which is undefined behaviour.
- **File and line:** `libs/draxul-terminal-core/src/terminal_core.cpp:533` (`mark.row -= rows`). The inputs come from `terminal_core_csi.cpp:444-448` (SD) and `:464-468` (IL), and out-of-range parameters are clamped to `INT_MAX` at `:174-175`.
- **Severity:** MEDIUM (undefined behaviour; in practice the mark is usually just erased)
- **What goes wrong:** With an OSC 133 mark at row 1 or below, sending `ESC[99999999999L` computes `row + INT_MAX`.
- **Suggested fix:** Clamp `rows` to `±(bottom - top)` before adjusting marks.

#### M8. ScoreView's microphone-open thread is detached and can run module code after unload
**Summary:** Quitting while the microphone permission prompt is still pending can crash on exit, because a background thread keeps running plugin code after the plugin is unloaded.
- **File and line:** `plugins/scoreview/product/draxul-scoreview/src/mic_player_input.cpp:123-217` (the thread is detached) and `:220-235` (the destructor never joins).
- **Severity:** MEDIUM
- **What goes wrong:**
  - Plugin modules stay loaded until the plugin manager shuts down (`libs/draxul-plugin`: "remain resident until manager/process shutdown"), so this is an exit-time risk.
  - On macOS, the thread can be sleeping in the 100 ms permission poll when the module is `dlclose`d. It then wakes and runs unmapped code.
- **Suggested fix:** Keep the thread joinable and join it before module release, or pin the module while the thread is alive.

#### M9. A wrongly-typed ScoreView progress file crashes the app every time that piece is opened
**Summary:** A practice-progress file that is valid JSON but has an unexpected value type crashes Draxul every time that piece is opened.
- **File and line:** `plugins/scoreview/product/draxul-score-learn/src/player_model.cpp:519-537` (`.value(...)` and `std::stoi(key)` throw on type mismatch). There is no catch in `score_session_controller.cpp:38-49` or in `create_instance`.
- **Severity:** MEDIUM
- **What goes wrong:** For example, `"hit": null` (which nlohmann also writes for NaN) or a non-numeric pitch key throws `type_error` out of `create_instance`, terminating the host.
- **Suggested fix:** Wrap `deserialize` in try/catch and return `false` on failure.

#### M10. Small ScoreView panes call `std::clamp` with its bounds reversed
**Summary:** Making a ScoreView pane very short trips a debug-build abort and mis-lays-out the score in release builds.
- **File and line:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:402` and `score_presentation.cpp:152`.
- **Severity:** MEDIUM
- **What goes wrong:** When pane height is below about 107 px × the UI scale, the lower bound `96*scale` exceeds the upper bound `vh*0.9`. That is undefined; the MSVC debug STL asserts, and release builds produce a band taller than the pane.
- **Suggested fix:** Use `std::min(lo, hi)` as the lower bound, or return early when the pane is too small.

#### M11. MegaCity's coverage parser misreads newer LCOV function records
**Summary:** Coverage colouring shows zero hits for every function when the coverage file comes from newer LCOV versions.
- **File and line:** `plugins/megacity/product/draxul-megacity/src/lcov_coverage.cpp:101-110` (the name is everything after the first comma).
- **Severity:** MEDIUM
- **What goes wrong:** LCOV 2.x writes `FN:<start>,<end>,<name>`, so the stored name becomes `"<end>,<name>"` and `FNDA` lookups never match.
- **Suggested fix:** Take the last comma-separated field as the name, and accept the `FNL`/`FNA` record types.

#### M12. One filesystem error during the source scan leaves MegaCity permanently unbuilt and polling
**Summary:** If a folder disappears while MegaCity is scanning the source tree, the city never builds and the plugin keeps waking every 50 ms until restarted.
- **File and line:**
  - `plugins/megacity/product/draxul-treesitter/src/treesitter.cpp:788-843` (`complete = !ec && !stopped`);
  - `plugins/megacity/product/draxul-megacity/src/semantic_source_controller.cpp:28-39` (`start()` is a no-op once `started_` is set) and `:62-69` (`poll()` returns nothing until the snapshot is complete).
- **Severity:** MEDIUM
- **What goes wrong:** A directory is removed mid-iteration, so the snapshot is never complete. `poll()` then never yields a result and Rebuild cannot restart the scan.
- **Suggested fix:** Skip the failing entry and continue, or allow `start()` to restart a scan that finished incomplete.

#### M13. SatView's satellite-catalog JSON parser has no depth limit
**Summary:** A malicious or tampered satellite catalogue (from the network or the local cache) can crash SatView by nesting brackets very deeply.
- **File and line:** `plugins/satview/src/core/satview_catalog.cpp:238-322` (`parse_value`, `skip_object` and `skip_array` recurse without limit).
- **Severity:** MEDIUM
- **What goes wrong:** A response or cache file with tens of thousands of `[` overflows the worker thread's stack. The network cap is 64 MB, so such input fits.
- **Suggested fix:** Add a depth counter (for example a maximum of 64) and fail the parse beyond it.

#### M14. SatView's catalog-ID bounds check lets 2^63 and INT64_MIN through
**Summary:** An extreme catalogue ID passes the range check and then triggers undefined integer behaviour.
- **File and line:** `plugins/satview/src/core/satview_catalog.cpp:559-562` (`>` should be `>=` on the upper bound) and `plugins/satview/src/core/satview_propagation.cpp:264` (`std::llabs(INT64_MIN)`).
- **Severity:** MEDIUM
- **What goes wrong:** An ID of 9.223372036854775808e18 overflows `llround`. An ID of −2^63 passes the check, and `llabs` of it is undefined.
- **Suggested fix:** Use `>=` for the upper bound and reject `INT64_MIN`, or take the absolute value on an unsigned type.

#### M15. SatView config clamps let NaN and infinity through
**Summary:** Writing `nan` or `inf` for a SatView setting bypasses the range limits and sends invalid numbers to the shaders and the camera.
- **File and line:** `plugins/satview/src/runtime/satview_config_io.cpp:51-64` (`std::clamp(NaN, …)` returns NaN) and `:217-241`.
- **Severity:** MEDIUM
- **What goes wrong:** TOML allows `nan` and `inf`. They reach exposure, white point, `time_speed` and ground latitude/longitude unchanged.
- **Suggested fix:** Reject non-finite values before clamping.

#### M16. The MusicXML importer silently truncates large fractions, and a huge key signature overflows
**Summary:** Unusual MusicXML values (very large `<divisions>`, durations or key signatures) are silently truncated and produce garbage timing, or overflow.
- **File and line:** `plugins/scoreview/product/draxul-notation/include/draxul/notation/score_document.h:27-43` (`static_cast<int>` of `long long`) and `plugins/scoreview/product/draxul-score-learn/src/piece_analysis.cpp:63` (`fifths * 7`).
- **Severity:** MEDIUM
- **What goes wrong:**
  - A denominator of 2^32 truncates to 0 or a negative value, so `to_double` returns inf or NaN.
  - A huge `<fifths>` value overflows signed arithmetic.
  - This is the plain `.musicxml` importer, which the hostile-`.mxl` card does not cover.
- **Suggested fix:** Reject values outside `int` range in `Fraction::of`, and clamp `fifths`.

#### M17. `do.py` can misjudge "access denied" as "process not running" and break the build-tree lock
**Summary:** On Windows, the build script can wrongly decide that another build owning the cache has exited, and start a second, competing build in the same folder.
- **File and line:** `do.py:50-59` calls `ctypes.get_last_error()` after `ctypes.windll.kernel32.OpenProcess`.
- **Severity:** MEDIUM
- **What goes wrong:**
  - `windll` is not loaded with `use_last_error=True`, so `get_last_error()` returns ctypes' private copy, which is normally 0.
  - When the lock owner cannot be queried (for example a build running under another account or service), the function returns `False`. The live lock is then deleted (`:137-141`) and the build proceeds concurrently.
- **Suggested fix:** Use `ctypes.WinDLL("kernel32", use_last_error=True)`, or treat any `OpenProcess` failure other than "invalid parameter" as "running".

## Unresolved leads (not accepted; evidence still needed; no cards recommended)

**Renderer and fonts**
- **Vulkan and Metal present an uninitialised image when a frame has atlas uploads but no draw** (`vk_renderer.cpp:1110`, `:1532-1573`; `metal_renderer.mm:645`). The logic is real, but I found no frame that uploads glyphs without drawing.
- **Vulkan `begin_frame` failure after acquiring an image** (`vk_renderer.cpp:1097-1109`) leaves a signalled semaphore behind. A partial failure in `recreate_frame_resources` (`:789-796`) can leave too few semaphores for the image count. Both need an allocation failure to trigger.
- **NanoVG Vulkan double flush:** `nvgVkSetFrameState` frees per-slot resources (`nanovg_vk.cpp:1948`), so flushing the same context twice in one frame would destroy resources still in use. I found no caller that does this.
- **Glyph cache:** `reserve_region` has no `w <= atlas_size_` check after colour-glyph rescaling (`glyph_cache.cpp:324-341`, `531-549`).
- **Small edge leads:** Vulkan and Metal foreground blending differ in destination alpha; the scissor width is not reduced when a negative origin is clamped to 0; capture readback assumes BGRA.

**Client, server and control**
- **Control and metadata JSON `.value()` throws on a mistyped field** (`control_codec.cpp:269-292`, `control_metadata.cpp:61`), possibly on threads with no handler. The old board name `05 control-version-type-validation -bug` may cover this.
- **Stream-to-poll fallback is one-way:** with a 1 s heartbeat and a 3 s client timeout, M2-style stalls could permanently downgrade a UI to polling. Stall lengths were not measured.
- **Column resize near the scrollback cell budget** can fail and desynchronise the PTY and the client grid (`server_terminal_runtime.cpp:459-479`). The old board name `25 pty-grid-dimension-desynchronization -bug` may cover this.
- **WinHTTP:** the watcher can close the request handle between the main thread loading it and using it (`http_client_win.cpp:145-207`).
- **Stale subscribers:** server-side subscribers are never removed when a client registration is dropped. This wastes broadcast work rather than producing wrong results.

**Terminal, Neovim and input**
- **Bracketed paste is not sanitised:** clipboard text containing `ESC[201~` can escape bracketed paste (`terminal_host_base.cpp:196-203`).
- **Neovim text input escapes only a bare `<`:** multi-character IME text containing `<` could be read as a keycode (`input.cpp:209-216`).
- **CSI parsing:**
  - an interrupted sequence is not aborted on ESC, CAN, SUB or C0 controls (`vt_parser.cpp:152-172`);
  - colon sub-parameters in SGR make the whole sequence, including resets, get dropped (`terminal_core_csi.cpp:171-177`);
  - CNL/CPL move the cursor the wrong way when it is outside the scroll margins (`:320-329`).
- **Unanswered Neovim RPC request:** if `on_request` throws, no reply is sent (`rpc.cpp:341-353`).
- **`+` cannot be bound** as a key (`keybinding_parser.cpp:67-93`).
- **Tab switching** doesn't update the input target until the next pump (`app.cpp:5482-5498`).
- **Failed local PTY resize** still resizes scrollback (`local_terminal_host.cpp:386-387`, `437-478`), which could corrupt the screen. I found no realistic way to make the resize fail.

**Product plugins**
- **SatView:** snapshot-exchange memory ordering; `SDL_InitSubSystem(AUDIO)` called on a background thread in Rezonality; NaN velocity from a zero time span in the sampled ephemeris; overflow from a huge `fetched_unix` value in cache metadata.
- **ScoreView:** RtMidi callback teardown order; Verovio toolkits possibly sharing global state; repeated note IDs across repeats in the timemap.
- **Startup throws:** `temp_directory_path()` can throw inside ScoreView's `create_instance`, and several product worker lambdas have no try/catch (so `bad_alloc` would terminate).
- **Rezonality** temporary SPIR-V directory is keyed by the `this` pointer, so two processes could collide.

**Tooling**
- **`do.py`'s `_capture_owned_process`** decodes output strictly as UTF-8 (`do.py:1617-1636`), so non-UTF-8 output would raise an uncaught `UnicodeDecodeError`.

**Rejected:** the worker's ScoreView `source_slicer.cpp:305` unescaped part-id claim. Valid MusicXML part IDs are XML names, which cannot contain `"`, `&` or `<`.

## Coverage

| Area | Representative paths and flows examined | Who inspected | Remaining gaps |
|---|---|---|---|
| Architecture, guidance, trackers | `CLAUDE.md`, `AGENTS.md`, `docs/module-map.md`, root and product Kanban lanes, board config | Coordinator | `docs/features.md` only skimmed |
| Client/server/control/session | Server lifecycle, requests, sessions, terminals; stream and poll services; topology service; `remote_session_client` and `_coordinator`; recovery; Win32 control transport; ConPTY/Unix PTY; agent integration; HTTP (Windows); weather; the server branch of `main` and `App::shutdown` | Worker 1, then coordinator re-verified C2, H5 and M1–M4 | `topology_projection`, `topology_client`, `agent_client`, `server_agent_service`, session/topology protocol decoders, the CLI sources, `http_client_mac.mm` |
| App orchestration, hosts, terminal, Neovim, input, modules | `input_dispatcher`, `pane_manager`, `split_tree`, `gui_action_handler`, `control_request_router`, `rpc` and `ui_events`, `nvim_host`, `input`, VT parser and terminal core, grid, Unicode, SDL window and event translation, `plugin_manager`, `file_monitor`, Kanban store and host, Markdown host (skimmed) | Worker 2, then coordinator re-verified C6, H1–H3 and M5–M7 | Much of `app.cpp` (remote topology, agents, session persistence), `chrome_*`, command palette, toast host, Space/tab controllers, `macos_*.mm`, `plugin_host.cpp` internals, `plugin_storage`, `mpack_codec`, `nvim_process`, config I/O, Markdown and Kanban layout and render passes |
| Rendering, fonts, UI, SDK, plugin support | Vulkan renderer, atlas, pipeline and swapchain; Metal renderer; glyph cache and atlas manager; shaper; ligatures; NanoVG Vulkan; BMP; plugin adapter and API; Vulkan resource helpers; spinning-triangle | Worker 3, then coordinator re-verified C1 and C5 | `nanovg_mtl.mm`, Metal native backend, most of `native_backend_vulkan.cpp`, plugin ImGui backends, `vk_hdr_scene_pipeline`, `draxul-ui` panels, box drawing |
| SatView, ScoreView, MegaCity | Plugin entry points and lifecycle; simulation worker; propagation; catalog parser and service; config I/O; HDR target teardown. ScoreView importer, slicer, microphone and MIDI input, progress model, runtime layout. MegaCity host workers, routes and grid, tree-sitter scanner, LCOV, codeviz teardown | Worker 4, then coordinator re-verified C3, C4, H4 and M8–M16 | All Metal product backends; much of the SatView and MegaCity Vulkan renderers; ScoreView Verovio, SVG, stream composer and synth code; MegaCity city builder, picking and layout; product shaders |
| Build, scripts, tooling | `do.py` lock and process ownership, server-helper preflight, validation runner; `review.py` execution path | Coordinator | `scripts/*.py`, `tools/publish_plugin.py`, CMake modules beyond the module map, CI workflow (all skimmed or not reviewed) |
| Tests | Used to check for existing guards (`ui_events`, the eviction test, render `unicode-view` fixture, LCOV tests) | Workers and coordinator | No systematic review of test correctness |

**Delegation:** All four subagents finished. None failed or delegated further. Their reasoning effort could only be requested in the prompt, not set as a parameter.

**Limits:** This is a partial-depth review of a 424k-line codebase. The unreviewed areas in the table, especially the Metal backends and large parts of the product renderers and `app.cpp`, may hold more defects. Every finding comes from reading code; none was reproduced at runtime.
