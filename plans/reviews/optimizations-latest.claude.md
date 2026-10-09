# Draxul runtime performance review

**Scope and method.** I reviewed the whole repository from the single packed file (`repomix-output.xml`), following `REVIEW_PROMPT.md`. Line references are original source lines, with a few pack line numbers where noted. Nothing was built or run, so **all evidence is static**; there are no measured timings, and no timings or speedups are invented. This covers every major area, but it is not exhaustive: the coverage table at the end lists what was only partly reviewed.

**Delegation.** Four read-only subagents each covered one area. They all used the same packed file, the Opus model, and were asked for high reasoning effort in the prompt; the tool has no reasoning-effort setting, so that could not be enforced. None of them delegated further.
- W1: server, client, control, protocol, terminal core and PTY.
- W2: renderer, fonts, grid, Markdown, Kanban and the chrome/overlay hosts.
- W3: SatView and Rezonality.
- W4: plugin host, SDK and support, MegaCity and ScoreView.

I reviewed the app frame loop, input dispatch, Neovim RPC, window, config, weather, system-resource and performance-collector paths myself. Before accepting a worker finding I re-read its cited code; where I only spot-checked, confidence is marked lower.

**Plan mode.** Plan mode was active in this session. Because you asked for a review with no files written, I did not create a plan file.

**Kanban exclusions.** I checked every `pending`, `ice-box` and `done` folder under the root board and all five product boards. None of the accepted findings duplicates a tracked card; related refactor cards are noted per finding. One caveat: the root `kanban/.draxul-kanban.toml` order list names cards that have **no file** in any lane, for example `09 ligature-expand-bounds -test`, `27 atlas-dynamic-growth -feature`, `15 rpc-queue-backpressure -test`, `17 sync-ipc-on-ui-thread -bug` and `34 hidden-remote-terminal-suspension -bug`. I treated the lane folders as authoritative, as CLAUDE.md says. Findings #3, #5 and #25 may overlap intent recorded under those stale names.

---

## Accepted findings (ranked by expected user benefit, confidence and risk)

### 1. P1 — The app wakes 20 times a second forever, even when completely idle
- **Location:**
  - `app/app.cpp:3201-3251` (`wait_timeout_ms`, `kHostPollIntervalMs = 50`).
  - `app/render_tree.cpp:49-61` (`walk_any_running`).
  - `libs/draxul-host/include/draxul/host.h:237-240`: the default `requires_periodic_wake()` returns `is_running()`.
  - Overlay hosts that always report running: `app/command_palette_host.cpp:57-60`, `app/toast_host.cpp:56-59` and `app/diagnostics_panel_host.cpp:23-26` return `true`; `app/chrome_host.cpp:80-83` returns `running_`.
- **Workload and impact:** any open window with no input and no output. The palette, toast and chrome nodes are always in `render_root_` and visible (`app.cpp:2326-2347`). So `any_host_running` is always true, and `SDL_WaitEventTimeout` is capped at 50 ms. Each wake runs a full `pump_once` pass (`app.cpp:2438-2553`), which includes:
  - two agent-projection computes per pass (`process_control_requests` calls the uncached `query()` at `app.cpp:5596`; `frame_agents()` runs again at 2504);
  - a control-journal diff;
  - `rebuild_render_tree` and `update_host_presentation_visibility` across all tabs;
  - `walk_pump` plus `pump_background_hosts`.

  This defeats the intent of `RemoteTerminalHost::requires_periodic_wake()` (`remote_terminal_host.cpp:1377-1381`), which returns false precisely so coordinator-fed panes don't poll.
- **Evidence:** static, high confidence. Only `PluginHost`, `RemoteTerminalHost` and MegaCity override the default.
- **Bottleneck:** unconditional 50 ms timed wakeups plus fixed per-pass work.
- **Smallest safe change:**
  - Override `requires_periodic_wake()` to return `false` in Chrome, Palette, Toast and Diagnostics. They are main-thread driven and already expose `next_deadline()`.
  - Keep the 50 ms safety cap only while a background-reader host (NvimHost) exists, or while the coordinator still has undelivered publications (`ready_` non-empty; see `remote_session_coordinator.cpp:1342-1356`). This preserves the recovery path for the macOS SDL lost-wake race described in the comment at 3204-3207.
  - Give `SystemResourceMonitor` and the weather pill a `next_deadline`, because they currently rely on the poll.
  - Use `frame_agents()` in `process_control_requests`.
- **Validation:**
  - Before/after count of loop iterations per second, idle for 60 s, with only server-backed shell panes. Target: under 1/s, excluding explicit deadlines.
  - macOS: `powermetrics` idle wakeups. Windows: ETW context switches.
  - Confirm remote output still appears with no user input on macOS, since that is the SDL race path.
- **Dependencies:** #2 is the same idle-power problem in the transport threads; #26 is amplified by this loop.

### 2. P1 — Transport and listener threads poll every 50–100 ms in both processes (from W1, verified)
- **Location:**
  - `libs/draxul-control/src/async_frame_stream_posix.cpp:125` (`::poll(...,50)`) and `:435` (accept).
  - `async_frame_stream_win32.cpp:159` (`WaitForSingleObject(...,50)`) and `:425`.
  - `control_transport_posix.cpp:364` (`poll(...,100)`) across 4 listener threads (453-462).
  - `control_transport_win32.cpp:512`.
- **Workload:** always on. In the GUI: the stream reader wakes 20/s and the control listeners 40/s. In the server: the listeners wake 40/s, the acceptor 20/s, and each connected reader another 20/s.
- **Evidence:** static, high confidence. The timeouts exist only to re-check `closed` or `stop_token`. `close()` already interrupts blocking waits (`shutdown(SHUT_RDWR)` on POSIX, `CancelIoEx` on Windows).
- **Bottleneck:** timed wakeups used as cancellation checks.
- **Change:**
  - Wait with no timeout, and register a `std::stop_callback` that calls `close()`.
  - POSIX listeners: add a self-pipe, because shutting down a listening socket does not reliably wake `poll` on macOS.
  - Keep a timed wait only while a write deadline is pending.
- **Validation:** idle wakeups per process, with a target under 5/s excluding heartbeats. The existing stream-shutdown test must still pass ("Unix Session stream shutdown cancels active I/O within a bounded deadline").
- **Dependencies:** none. This pairs with #1 for idle power.

### 3. P1 (P0 on long runs) — Ligature dirty-cell expansion is quadratic in run length (from W2, verified)
- **Location:** `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp:64-117` (`expand_dirty_cells_for_ligatures_impl`), called from `flush()` at 297-300 whenever `enable_ligatures_` is set, which is the default.
- **Workload:** terminal or Neovim output of long runs of non-space cells with the same highlight: minified JS or JSON, base64, hashes, `━━━` or `███` bars. It also fires on scroll, which marks the whole region dirty.
- **Evidence (static cost model):**
  - Each dirty cell scans its whole run to the left and right, then pushes every cell in `[left, right]`, followed by `sort` and `unique`.
  - A fully dirty row that is one run of length L therefore produces L² entries.
  - At 300×80 with no spaces, that is about 7.2M 8-byte entries per flush, which are then sorted.
  - Reusing the scratch buffer avoids reallocation, but not the O(L²) work.
- **Bottleneck:** O(L²) expansion plus an O(n log n) sort.
- **Change:** build a per-row dirty bitmap, sweep each touched row once, and emit each run that contains or left-adjoins a dirty column. The output is already sorted, so no sort is needed. When `is_full_dirty()`, emit the whole grid directly. This keeps the regrouping behaviour described in the comment at lines 72-78.
- **Validation:** `flush` microbenchmark on a 300×80 grid of all-`x` cells (via mark-all-dirty and via scroll), which should scale linearly with columns. The existing `////` regrouping and ligature render tests must pass.
- **Dependencies:** same flush path as #4 and #5. See the stale card name `09 ligature-expand-bounds -test` above.

### 4. P1 — Terminal scrolling copies and dirty-marks the whole scroll region for every output line (from W1, verified)
- **Location:**
  - `libs/draxul-grid/src/grid.cpp:120-162` (`scroll_rows`) and `581-621` (`Grid::scroll`).
  - `mark_dirty_index` at 673-687, reached through a `std::function` and with its own `PERF_MEASURE`.
  - Caller: `libs/draxul-terminal-core/src/terminal_core.cpp:542-565` (`newline`), which calls `scroll_rows(top, bottom+1, 1)` once per line at the bottom row.
- **Workload:** `cat` of a large file, `yes`, build logs. The server feeds up to 1 MiB per pump.
- **Evidence (static):**
  - Each LF copies (rows−1)×cols 44-byte `Cell`s and makes rows×cols indirect calls. At 200×50 that is about 430 KB moved and 10k calls per line.
  - After one scroll the whole screen is dirty, so every delta and upload downstream is full-screen (#6, #7, #8).
  - The server runtime has no drain budget; the GUI-local host has an 8 ms budget.
- **Bottleneck:** O(rows×cols) work per line.
- **Change:**
  - Coalesce consecutive bottom-of-region scrolls within one `feed`: push rows into scrollback and apply a single `scroll(n)` before any row access.
  - Or add a row ring offset plus a bulk "dirty region" mark to `Grid`. This touches the Neovim grid too, so coordinate with the renderer path.
- **Validation:** headless replay of a 100k-line capture through `ServerTerminalRuntime::pump`, measuring lines/s and CPU. The terminal-core and scrollback ring/overflow/resize tests must pass.

### 5. P1 — Ligature and font-choice caches are cleared entirely when full, so they thrash on large grids (from W2, verified)
- **Location:**
  - `libs/draxul-font/src/ligature_analyser.h:184-189` and `font_selector.h:303-309`: `if (size >= limit) cache_.clear();`.
  - Limit is `DEFAULT_FONT_CHOICE_CACHE_LIMIT = 4096` (`text_service.h:16`).
  - Callers: `grid_rendering_pipeline.cpp:157-221` (`try_shape_ligature`, up to 5 candidate strings per dirty cell) and `glyph_atlas_manager.h:71`.
  - One `TextService` is shared by all panes.
- **Workload:** full-screen flushes of code or text on large grids (Neovim scroll, resize, `cat`) with ligatures on, which is the default.
- **Evidence (static):**
  - A single 80-row code view can exceed 4096 distinct candidates. The cache then empties itself and misses on the next pass.
  - Each miss costs `select()` (an `hb_shape` plus `FT_Load_Glyph` per glyph on a selector miss) plus two `shape()` calls.
  - Ligature candidates also go into the Regular selector cache, which evicts ordinary single-cell entries.
- **Change:** two-generation or LRU eviction, and a larger limit (a few MB at 64k entries). Invalidation is unchanged (`reset_cache` on font or size change).
- **Validation:** count `hb_shape` and `FT_Load_Glyph` calls per scroll step on a 300×80 code grid. Steady-state misses should be about the candidates of the newly exposed rows only.

### 6. P1 — Server re-encodes each terminal event many times, including a full JSON parse and re-dump of every stream frame (from W1, verified)
- **Location:**
  - `libs/draxul-server/src/remote_terminal_service.cpp:383-416` (`encode_event`): builds a JSON tree, then dumps it only to measure its size (`replace_safe_json_size`, 41-47).
  - `EncodedEvent::encoded` stores that JSON tree, but nothing ever reads it; it is only assigned at line 413.
  - `session_stream_service.cpp:508-514` (`writer_loop`): `nlohmann::json::parse(queued.encoded)`, sets `frame_serial`, then `dump()` again for every outbound frame.
  - Frames are built from events that are re-converted to JSON (`encode_frame`, 57-61).
- **Workload:** every delta, which is full-screen during output because of #4; snapshots up to 64k cells (about 1–1.5 MB JSON) on attach, resync and un-hide.
- **Evidence:** static, high confidence. It is about 2 tree builds, 3 dumps and 1 parse per delta on the server. Retained memory per queued event is larger than the 2 MiB byte bound suggests, because the bound counts serialised bytes and not the retained tree.
- **Change:**
  1. Splice `"frame_serial"` into the already-serialised frame as text instead of parse+dump.
  2. Store the serialised string, or just its size, instead of the tree.
  3. Optionally assemble event frames from pre-serialised event strings.
- **Validation:** frame bytes must be byte-identical before and after steps 1–2. Use `session_transport_load_tests` (`steady_frame_bytes`, `terminal_delivery_latency_us`) and time spent in `encode_event` and `writer_loop`, on macOS (UDS) and Windows (named pipe).

### 7. P1 — Deltas are produced eagerly even when the client can't take them, so queues overflow, deltas are discarded and a full resync follows (from W1, medium-high confidence)
- **Location:**
  - `remote_terminal_service.cpp:483-489` (`publish_runtime_updates`), `457-480` (`broadcast`) → `require_resync` on queue overflow.
  - `session_stream_service.cpp:887-929`: the server polls again only after the client's Update.
  - Queue bounds are 32 events and 2 MiB (`remote_terminal_protocol.h:20-22`).
- **Workload:** sustained output in a visible pane. Every server loop pass calls `take_delta`, encodes it and queues it. With full-screen deltas of about 270 KB at 200×50, about 8 of them fill the 2 MiB bound. The pending deltas are then thrown away and a full snapshot is encoded instead.
- **Mitigation that doesn't cover this:** `avoided_delta_encodes_` only covers suspended (hidden) subscribers.
- **Change:** while every active subscriber already has undelivered events queued, or is waiting for an Update, skip `take_delta`. Let the grid's dirty set accumulate, and build one delta when the next poll arrives. Keep a single sequence counter and keep ordering with clipboard and controller events.
- **Validation:** `delta_frames`, `resyncs`, `delta_bytes` and server CPU during `yes`. Target: close to zero resyncs. The convergence tests must pass.
- **Dependencies:** #4 and #6 reduce the size of each delta.

### 8. P1 — Each client publication copies the full snapshot twice (once on the GUI thread) and turns unconsumed deltas into full rebuilds (from W1, verified)
- **Location:**
  - `libs/draxul-client/src/remote_session_coordinator.cpp:932-987` (`publish_projection`): `.snapshot = live` is a full copy. If an earlier publication is still unconsumed, `grid_update = full_grid_update(snapshot)` (970-974).
  - `libs/draxul-host/src/remote_terminal_host.cpp:1199-1300`: `display_snapshot` is a second copy by value. `rebuild_grid` (1225-1228) then clears the grid and re-applies every cell.
- **Workload:** every keystroke echo and every output batch in a visible pane. At 200×60 that is 12k cells, each carrying 2 strings, copied twice whatever the delta size. Under sustained output most publications become full rebuilds, which means full uploads.
- **Change:**
  - Bind `display_snapshot` by `const&` unless a scrollback view is being composed.
  - Merge a new delta into an unconsumed one (same rule as `RemoteTerminalProjection::apply_delta`).
  - Carry the snapshot as `shared_ptr<const>`.
- **Validation:** keypress→`flush_grid` latency counter, and full rebuilds per second during `yes`. `remote_terminal_host_tests` must pass.

### 9. P1 — Every grid draw copies the whole cell buffer plus a fixed 1.8 MB overlay block, on both backends, and the existing dirty-range API is unused (from W2, verified)
- **Location:**
  - `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:203-225` (`upload_state` → `state_.copy_to(mapped)`, flush of the whole range).
  - `metal/metal_renderer.mm:106-133`, called before its zero-area check.
  - `renderer_state.cpp:62-79`: `memset` of `(16384+1 − used) × 112 B` on every call.
  - `renderer_state.cpp:360-369` (`copy_to`).
  - `copy_dirty_cells_to` and `dirty_cell_offset_bytes` (21-60) are referenced only by tests.
- **Workload:** every rendered frame (typing, cursor blink, toasts, output) copies every visible grid handle in full: `(cells + 16385) × 112 B` per handle. Typing in one pane re-copies all the others.
- **Why it isn't mitigated today:** with two frames in flight, one `clear_dirty()` can't serve both buffer slots, which is probably why full copies were chosen.
- **Change:**
  1. Add a content generation per state and per slot, and skip the copy when unchanged.
  2. Upload only `overlay_cell_count_ + 1` overlay cells and drop the tail memset; instance counts never read past it.
  3. Later: a per-slot dirty range, forcing a full upload after buffer reallocation or `relayout`.
- **Validation:**
  - A bytes-uploaded-per-frame counter in the diagnostics panel. A 1-cell edit in 1 of 4 panes should drop from about the whole grid to about 112 × span.
  - A test that the edit reaches both slots.
  - Vulkan and Metal render snapshots.
- **Dependencies:** #10.

### 10. P1 — Setting an unchanged grid size runs a full relayout, and chrome, toast and palette do it every frame or pump; each handle also carries a 16,384-cell overlay (from W2, verified)
- **Location:**
  - `renderer_state.cpp:88-143`: an unchanged size takes the `else { resize; relayout(); }` branch.
  - `relayout` (183-205) fills every cell and all 16,384 overlay cells.
  - `OVERLAY_CELL_CAPACITY = 16384` (`renderer_state.h:45`) is allocated per handle, on the CPU and in both GPU slots.
  - Callers every frame: `app/chrome_text_layer.cpp:135, 196, 239, 292, 340` (top bar, sidebar, agents, headers, one per pane status).
  - Callers every pump while active: `toast_host.cpp` `refresh`, and `command_palette_host.cpp:93`.
- **Workload:** every rendered frame costs about 1.84 MB of CPU fill per chrome handle, plus the #9 memset of mapped memory. Resident memory is about 5.5 MB per grid handle (CPU plus 2 GPU slots), mostly unused overlay.
- **Change:**
  - Make `set_grid_size` a no-op when the dimensions haven't changed. Chrome, toast and palette rewrite their cells anyway.
  - Grow overlay storage on demand, capped at 16,384.
  - Fill only the used overlay range.
- **Validation:** `relayout` calls per frame (target 0 in steady state); RSS and VRAM with 4 panes and the sidebar shown; chrome snapshots on both backends.

### 11. P1 — Scrollback is allocated at full capacity when a terminal is created (from W1, verified)
- **Location:**
  - `libs/draxul-terminal-core/src/scrollback_buffer.cpp:49-116`: `resize` allocates `std::vector<Cell>(capacity_ * cols)`, and every `Cell` is default-constructed, so every page is committed.
  - Called at spawn: `server_terminal_runtime.cpp:384` (POSIX) and `:323` (Windows).
  - Default is 10,000 lines (`server_kernel.h:80`); the budget is `kServerMaxScrollbackCells = 24'000'000` (`:26`).
- **Workload:** opening panes and restoring sessions with many panes.
- **Evidence (arithmetic, not measured):**
  - About 53 MB per terminal at 120 columns and about 88 MB at 200 columns, even for a shell that has printed nothing.
  - The budget of about 1 GB of cells is reserved against capacity, not use, so it runs out at roughly 20 panes of 120 columns.
  - A column resize allocates a second full buffer, so peak memory briefly doubles.
- **Change:** allocate in fixed row blocks as rows are pushed, keeping the same budget accounting. On resize, copy only the live rows.
- **Validation:** server RSS with 1, 10 and 30 idle panes; time to create a pane. The scrollback ring, overflow and resize tests must pass.

### 12. P1 — SatView propagates the whole catalog at 60 Hz while hidden or paused (from W3, verified)
- **Location:**
  - `plugins/satview/src/runtime/satview_simulation_worker.cpp:15` (`kPositionTick = 16ms`), `310` (`wait_until`), `376` (`propagate_satellites`), `476` (reschedule).
  - `satview_plugin.cpp:272-283`: `set_visible` never reaches the worker. Pause only skips the second propagation.
- **Workload:** a SatView pane in a background tab, or paused. The whole catalog is propagated through the multi-threaded executor (up to 16 threads) every 16 ms. The default CelesTrak "active" group is about 10k+ objects.
- **Change:**
  - Add `set_active(bool)`, driven by visibility and pause. While inactive, wait on the condition variable with no deadline and publish once when a setting changes.
  - Recompute the clock on resume, as `current_simulation_seconds_locked` already does.
- **Validation:** a propagations-per-second counter (target 0 while hidden, and 0 after the first snapshot while paused); CPU with a hidden pane over 60 s. Tests from done cards 15 and 16 must pass.
- **Dependencies:** #33.

### 13. P1 — SatView decodes and uploads about 700 MiB of 8k textures synchronously on its first frame, per pane, without mipmaps (from W3, verified)
- **Location:**
  - `satview_texture_assets.cpp:103-105, 119, 128, 155` (8k day, night, clouds and moon; 4k sun and Milky Way).
  - Vulkan: `satview_render_vk.cpp:502-547` (`ensure_texture_descriptors`, called inside `record_prepass`). Uploads go through `vk_resource_helpers.cpp:604-682`, which submits and then calls `vkQueueWaitIdle`. `generate_mips=false`.
  - Metal: synchronous `replaceRegion` with `mipmapped:NO`.
  - The live-cloud refresh, every 3 hours, uploads another 128 MiB behind `vkDeviceWaitIdle`.
- **Workload:** opening or restoring a SatView pane blocks the UI and render thread. VRAM grows linearly with the number of SatView panes because nothing is shared. The 700 MiB figure is estimated from filenames and dimensions; the image files themselves aren't in the pack.
- **Change:**
  - Decode on a background task and render with the existing 1×1 fallbacks until textures arrive.
  - Upload in the frame's command buffer and retire the staging buffer per slot (keeping the rule from done card 17).
  - Generate mips or ship BC-compressed or smaller variants.
  - Optionally share one texture cache across SatView instances in the process.
- **Validation:** the existing `PERF_MEASURE` scopes in `load_rgba8_image_impl` and `record_prepass`; VMA and Metal allocation totals with 1 and 2 panes; frame hitch on a forced cloud refresh.

### 14. P1 — Rezonality's watcher reads and hashes every byte of the project every 100 ms, per pane, even when hidden or paused (from W3, verified)
- **Location:**
  - `plugins/rezonality/src/live_project.cpp:1238-1279` (`fingerprint`: recursive walk, `read_text` of every file, FNV hash).
  - `1543-1556` (100 ms `wait_for` loop). `auto_reload` defaults to true.
  - Visibility and pause don't stop the watcher.
- **Workload:** permanent I/O and CPU that scales with project bytes. For example, `scene.bin` is 3.6 MB, so at least 36 MB/s per pane. The NYX layout opens about 9 panes, 8 of which watch the same `shaders` directory.
- **Change:** fingerprint on path, size and mtime, and content-hash only files whose metadata changed, plus an occasional slow full hash; or use native change notification with the existing debounce. Suspend polling while hidden and rescan on show. This keeps the any-include semantics from done card 20.
- **Validation:** bytes read per second while idle (target about 0); edit→rebuild latency for `.frag` and nested includes.

### 15. P1 — Resizing a Rezonality pane rebuilds its whole GPU generation every frame of the drag (from W3, verified)
- **Location:**
  - `native_backend_vulkan.cpp:2167-2180` and the Metal equivalent at 711-723: `active_compatible()` requires an exact width and height match.
  - `runtime_controller.cpp:37-45` (`desired` returns `active_build_`) and `47-57` (`ShaderBuild activated = *selected;`, a deep copy).
  - `create_generation` rebuilds surfaces, models, pipelines (without a pipeline cache) and acceleration structures. On Metal it also runs a SPIRV-Cross conversion and `newLibraryWithSource`.
- **Workload:** dragging a divider or resizing the window with Rezonality panes open. Viewport and scissor are already dynamic state, so only pane-sized surfaces actually depend on the size.
- **Change:**
  - Recreate only size-dependent resources on a size-only mismatch, or debounce the rebuild until the size settles.
  - Move rather than copy the build.
  - Skip republishing diagnostics when the generation number is unchanged.
- **Validation:** generation creations per resize step and frame time during a scripted drag, on both backends. Keep the goldens and the done-card-03 rejection tests.

### 16. P1 — MegaCity keeps redrawing at about 60 Hz while idle when the static LCOV overlay is on (from W4, verified)
- **Location:**
  - `megacity_host.cpp:1886-1894` (`next_deadline`): `is_overlay_active(LcovCoverage)` blocks the idle `nullopt`, and because LCOV is not a "live perf" overlay, the code returns `now + kMovementTick` (16 ms).
  - `megacity_plugin.cpp:317-320`: the tick returns that delay with a redraw request.
- **Workload:** an idle MegaCity pane with LCOV coverage shown runs the full deferred pipeline continuously. LCOV data is a one-time import that never changes between frames.
- **Change:** use `is_live_perf_overlay` (or exclude `LcovCoverage`) in the ticking condition.
- **Validation:** frames per second and CPU with LCOV on and no input (target 0 frames after settling).

### 17. P1 — MegaCity allocates 3×4096² shadow cascades per buffered frame and recreates them, with a device-idle wait, on every resize (from W4, verified)
- **Location:**
  - `codeviz_render_vk.cpp:308-309` (resolutions) and `2851-2924` (`ensure_gbuffer_targets`: on any width/height change, `wait_for_device_idle`, destroy all, then per frame 3 × 4096² D32 maps + a 1024² R32F cube + a 1024² D32 depth target).
  - Metal: `codeviz_render.mm:988-1088`, same layout.
- **Workload:** the arithmetic gives about 216 MiB of shadow targets per buffered frame per MegaCity pane (for example about 432 MiB with 2 frames). A divider drag or window resize reallocates all of it, and on Vulkan each step also stalls the device.
- **Change:**
  - Split the size-independent shadow and point-shadow targets from the size-dependent G-buffer, so a resize only recreates colour and depth.
  - Consider a single shared shadow set with write-after-read barriers (trade-off: less GPU overlap between frames).
- **Validation:** VRAM snapshot, and frame times during a resize drag.

### 18. P1 — MegaCity rebuilds the whole scene snapshot on camera-only changes (from W4, partially verified)
- **Location:**
  - `megacity_host.cpp:1499-1505`: `camera_changed` sets `scene_dirty_`, which leads to `publish_scene_snapshot`.
  - `scene_snapshot_builder.cpp:245-539`: per-entity `CodeVizRenderable` with about 9 string copies, a linear material lookup, and a rebuilt heat table.
  - Sorted by `codeviz_scene_sort.cpp:10-27`.
  - With a selection active, `apply_selection_opacity` (2281-2370) rebuilds identity keys as well.
- **Workload:** orbit, pan and zoom on large repositories at the 16 ms tick rate. The work is O(entities) on the UI thread when only the camera matrices changed.
- **Change:** add a separate "camera dirty" flag and keep the static object list across camera-only frames; intern the identity keys.
- **Validation:** `build_scene_snapshot` call count during a 5 s orbit, and frame CPU time.
- **Dependencies:** MegaCity pending card 02 (frame preparation) touches the adjacent renderer code.

### 19. P1 — A plugin that requests a tick from inside its own tick spins the main loop when nothing is rendered (from W4, verified)
- **Location:**
  - `libs/draxul-host/src/plugin_host.cpp:492-516` (`pump`) and `567-575` (`request_tick`: sets `tick_pending_` and calls `wake_window`).
  - `plugins/scoreview/src/scoreview_plugin.cpp:36-44`: `request_frame` always calls `request_tick`.
  - `score_runtime.cpp:1741-1742`: calls `request_frame` on every playing pump.
  - `app.cpp:2536` skips rendering while the window is minimised.
- **Workload:** ScoreView playing (or MegaCity with continuous refresh) while the window is minimised, or while hidden with background playback. Each tick pushes an SDL event, so `wait_events` returns immediately and there is no vsync to pace the loop. The result is a busy spin.
- **Change:**
  - In the host, coalesce ticks requested during `run_tick` to at least `now + min_interval`.
  - In the products, rely on the returned `next_tick_delay` instead of calling `request_tick` from inside a tick.
- **Validation:** CPU and ticks per second with ScoreView playing and the window minimised.

### 20. P1 — Every grid resize re-uploads the whole shared 16 MiB atlas, once per resized pane (from W2, verified)
- **Location:**
  - `libs/draxul-host/src/grid_host_base.cpp:196-211`: `apply_grid_size` calls `force_full_atlas_upload()` at line 208.
  - That path goes through `grid_rendering_pipeline.cpp:328-345` → `pending_atlas_upload.h:36-50` (16 MiB `assign`) → the Vulkan staging and copy, or Metal's `flush_pending_atlas_uploads`.
- **Workload:** every window-resize step, split, close, sidebar or diagnostics toggle, for each resized pane. The atlas is global and already uploaded incrementally every frame (`app.cpp:2396`); atlas resets already have a separate generation-based path (done card 06).
- **Change:** remove the force from `apply_grid_size`, or track the uploaded atlas generation in the renderer so repeated full uploads of the same generation are skipped. Keep the full upload on renderer or device recreation.
- **Validation:** queued atlas upload bytes during a 20-step resize with 4 panes, plus a new test next to the existing font-size-change atlas test.

### 21–48. P2 findings (bounded workloads; static evidence)

Each item names the location, the workload and cost, the change, and how to validate it.

21. **Clipboard polled on the UI thread** (mine + W2). `nvim_host.cpp:186-204` and `568-576`, with the 500 ms gate in `clipboard_poll_gate.h:14-27`.
    - Every NvimHost, including hidden ones pumped by `pump_background_hosts`, copies the entire OS clipboard every 500 ms even if Neovim never asks for it. Cost is 2 Hz × number of Neovim hosts × clipboard size.
    - Change: refresh on `SDL_EVENT_CLIPBOARD_UPDATE` or on focus, and share one cache across hosts.
    - Validate: clipboard reads per minute when idle with a 5 MB clipboard.

22. **Neovim notification deep-copied on the reader thread** (mine). `libs/draxul-nvim/src/rpc.cpp:356-385`: `notif.params = msg_array[2].as_array();` copies the whole decoded redraw tree (every nested string and vector) because the message is passed as `const&`. It then wakes SDL once per notification, with no coalescing (the coordinator does coalesce).
    - Change: move the decoded message through `dispatch_rpc_message`, and add a wake-pending flag.
    - Validate: allocations per redraw on a full-screen scroll.

23. **Command palette rebuilt every pump while open** (mine). `app/command_palette_host.cpp:77-100` runs `view_state` (copying entries), `render_palette` over the half-window grid, and `update_cells` on every pump pass (20 Hz or more plus every event), even when nothing changed. Filtering itself is cached in `refilter`.
    - Change: rebuild only on query, selection or size change (a dirty flag).
    - Validate: cells produced per second with the palette open and idle.

24. **Throwaway plugin discovery each time the palette opens** (W4, verified; corrected). `app/command_palette.cpp:76` and `app.cpp:75` call `PluginManager::discover_default()`. That creates, then `remove_all`s, a fresh runtime directory and TOML-parses every manifest, synchronously. It happens once per palette open or plugin tab creation, not per keypress.
    - Change: reuse the long-lived manager (`main.cpp:681`) or add a manifest-only scan.

25. **Vulkan swapchain rebuild on every resize step recreates too much** (W2, verified). `vk_renderer.cpp:756-815`: reloads grid pipelines from disk (`vk_pipeline.cpp`, no `VkPipelineCache`), creates a new descriptor pool and sets, and new render passes; Markdown then recreates its pipelines when the render-pass handle changes (`markdown_render_pass_vk.cpp:544-548`).
    - Change: recreate these only when formats change, and reuse the render-pass handles.
    - Validate: the existing `PERF_MEASURE` in `recreate_frame_resources` during a Windows resize drag. Vulkan only.

26. **Hidden Neovim grid hosts still request frames and blink** (W2). `grid_host_base.cpp:222-241`: `flush_grid` always calls `request_frame` and may set the title-bar colour. `apply_cursor_visibility` (443-455) requests frames on blink phases even when unfocused.
    - Remote terminals are already suspended while hidden, which is a safeguard; hidden Neovim panes still trigger foreground renders, which cost the full uploads of #9.
    - Change: override `set_presentation_visible` in `GridHostBase` to suppress frame requests while hidden, and stop blinking when unfocused.
    - Validate: frames per second with a hidden busy Neovim pane.

27. **Markdown deep-copies and sorts the whole layout on every scroll step** (W2, verified). `modules/markdown/.../markdown_host.cpp:555-588`: `LayoutDocument paint_document = layout_;` plus a sort, on every wheel tick, drag or key. Cost is O(document).
    - Change: draw from `layout_` by reference and append the scrollbar rects.
    - Validate: time per scroll on a 5k-row document. Separate from card 25.

28. **Rich text creates one full `TextService` per style** (W2, verified). `libs/draxul-font/src/rich_text_service.cpp:62-88` creates a separate `TextService` per (size, bold, italic). Each costs 16 MiB of CPU atlas, up to 4 FreeType libraries, and a matching GPU atlas texture, per Markdown or Kanban preview.
    - Change: key services by point size only and pass bold/italic through.
    - Validate: RSS and VRAM with one preview open.

29. **Toast draws through a full-window grid** (W2). `toast_host.cpp:121-144` uses a full-window grid for about 50 cells. With #9 and #10 that means a full upload plus full-screen alpha-blended background quads every 33 ms while a toast is visible.
    - Change: size the grid to the toast bounds.

30. **Kanban rescans every card on any watcher event** (W2). `kanban_host.cpp:252-282` → `kanban_store.cpp:567-687` opens every card file via `read_card_priority` on the UI thread, including after the app's own moves. Cost scales with cards across all boards.
    - Change: cache path, mtime and size → priority. Card 29 covers timing, not this cost.

31. **NanoVG creates GPU objects on every flush** (W2). `nanovg_vk.cpp:1442-1660` creates buffers, a framebuffer and a descriptor set per flush, and Metal creates two `newBufferWithBytes` buffers; the chrome vector pass flushes every frame.
    - Change: per-slot ring buffers and cached framebuffers. Distinct from cards 24 and 31.

32. **MegaCity builds perf-debug state every frame with the overlay off** (W4, verified). `megacity_host.cpp:1083-1085` → `metrics_overlay_controller.cpp:156-169` runs the full module × building × layer walk with string keys even for `OverlayMode::None`.
    - Change: return `nullptr` for None, and cache the result on (mode, model, snapshot generation).

33. **SatView double propagation and per-frame marker rebuild** (W3). The worker propagates twice per 16 ms tick. `draw` recomposes and re-uploads every 80-byte marker per generation (runtime 1286-1308), even though the shader already interpolates between `position0` and `position1`.
    - Change: publish every 100–250 ms and let the GPU interpolate.
    - Validate: marker error against direct SGP4 at 1×, 60× and 3600×.

34. **SatView parses the catalog cache on the UI thread** (W3). `satview_catalog_service.cpp:317-417` parses the SATCAT CSV and GP JSON synchronously in `create_instance`, then deep-copies the catalog twice (485-489; runtime 826-827).
    - Change: load on the refresh worker and hand over `shared_ptr<const>`.

35. **SatView lunar tracks rebuilt every frame** (W3). With a lunar track present and Earth+Moon shown, `lunar_tracks_need_reanchoring` forces a full track rebuild and upload every frame (runtime 1217-1273).
    - Change: re-anchor only lunar tracks, or re-anchor on a pixel-scale tolerance.

36. **SatView filter runs over the whole catalog several times per frame** (W3; medium confidence). With a search string active, each candidate allocates strings (`filter.cpp:15-53, 155-164`).
    - Change: a cached visibility bitset keyed on filter revision and catalog generation.

37. **SatView HDR/MSAA targets reallocated on every size change** (W3). Every frame slot's targets are reallocated per size change, with `vkDeviceWaitIdle` on Vulkan (`satview_render_vk.cpp:1057-1099`), and the debug `msaa_difference` target is always allocated.
    - Change: debounce the reallocation and allocate debug targets lazily.

38. **Rezonality re-imports models and textures on every rebuild** (W3). `build_candidate` re-imports models (Assimp) and textures even for a `.frag` edit (`live_project.cpp:1316-1385`). Staging buffers stay resident for the generation, so each texture exists three times.
    - Change: cache decoded assets keyed on path, size and mtime, and free staging after upload.

39. **Rezonality's Neovim plugin polls constantly** (W3, verified defaults). It polls every 500 ms and spawns `draxul pane list` every 2 s in every Neovim where it is installed (`init.lua:29-31`, 650-666), re-setting diagnostics each tick.
    - Change: mtime gating, or `uv.new_fs_event`.


41. **ScoreView Clock view replays the whole piece every frame without culling** (W4). The Clock conveyor and monolithic flow replay the whole piece through NanoVG every 16 ms (`score_render_nvg.cpp:134-353`; `record_flow` 133-368).
    - Change: cull by precomputed x-extents.
    - Validate: NanoVG path counts on a long score.

42. **ScoreView zoom and view toggles re-engrave synchronously on the UI thread** (W4; medium confidence). `score_runtime.cpp:547-650` renders every page SVG, and the timemap is rendered and parsed twice per relayout.
    - Change: parse the timemap once; use the existing latest-wins engraver.

43. **ScoreView redraws on every pointer move or click** (W4, verified). `score_runtime.cpp:2178-2194` ignores the `WantCaptureMouse` result and redraws unconditionally.
    - Change: redraw only when ImGui wants the mouse, as MegaCity does.

44. **ScoreView polls the background engraver by redrawing at 60 Hz** (W4, verified). `next_deadline` 1748-1749 returns 16 ms while `engrave_busy()`, with a redraw each time.
    - Change: have the worker call the thread-safe `request_tick` when it finishes.

45. **ScoreView saves progress at every bar crossing on the UI thread** (W4, verified). `score_session_controller.cpp:105-130` serialises and atomically writes the progress file each bar while dirty.
    - Change: throttle, or write on a worker.

46. **ScoreView audio and microphone processing on the UI thread** (W4). Audio is synthesised on the UI thread with a queue of about 70 ms (`score_audio_controller.cpp:184-270`), so any main-thread stall from another pane (#13, #15, #42) can cause audible dropouts.
    - Change: a larger queue target, or an SDL audio-stream callback.

47. **Server-side terminal latency and wakeups** (W1, verified or spot-checked):
    - (a) Shell exit is never signalled to the server loop: `unix_pty_process.cpp:479-512` and the ConPTY reader exit without `on_output_available_`, so pane close waits up to the 1 s idle interval.
    - (b) The agent-process observer wakes on every 4 KiB PTY chunk (`agent_process_observer.cpp:41-45, 76-86`) and probes the process table about once a second during output, plus 1 Hz per idle terminal.
    - (c) The server loop pumps all terminals, with no drain budget, before dispatching keystrokes, and calls `waitpid` for every terminal on every wake.
    - (d) The scrolled-back view fetches scrollback twice and rebuilds the grid on every output batch.
    - (e) A selection drag rebuilds the overlay even when the cell under the mouse hasn't changed (`selection_manager.cpp:48-57`).
    - Changes:
      - (a) wake the loop on reader EOF;
      - (b) notify only on state-relevant activity;
      - (c) handle commands between terminal pumps with a per-terminal drain budget, and cache exit status;
      - (d) request the anchored page in one round trip;
      - (e) return early if the cell under the mouse is unchanged.

48. **Weather worker sleeps in 100 ms steps** (mine). `libs/draxul-weather/src/weather_service.cpp:17-18, 129-131` sleep-polls every 100 ms for the 10-minute interval, about 6,000 wakes per fetch while `weather_location` is set.
    - Change: `condition_variable::wait_until` with stop notification.

**Also accepted from W4 at lower confidence** (spot-checked or code-shape only):
- MegaCity draws about 11× per object with no instancing and re-renders the point-shadow cube every frame. Skip the cube when the light and scene revision are unchanged.
- MegaCity semantic projection and city build run synchronously on the UI thread, with a deep copy of an already-`shared_ptr` layout (`megacity_host.cpp:1332-1334, 1437-1439`). The authors' own timing logs show this is measurable.
- Plugin staging recursively copies the whole package on first load and every reload (`plugin_manager.cpp:395-425`), including the ScoreView SoundFont and Verovio data. Use hard links for non-library files.
- Metal material textures are created with a `waitUntilCompleted` per texture (`codeviz_render.mm:669-684`); Vulkan records them without waiting.
- `ChromeHost` calls the plugin's `get_state` about 4× per pane per frame.
- MegaCity `clear_active_routes` copies the whole city grid (`megacity_host.cpp:705-725`).

---

## Unresolved leads (not accepted; evidence still needed)


---

## Workload and benchmark plan for the highest-value findings

1. **Idle power (#1, #2, #21, #48):** 60 s idle with 3 shell panes and 1 Neovim pane on macOS and Windows. Record loop iterations per second, per-process idle wakeups (`powermetrics` / ETW), and clipboard reads. Target: fewer than 5 wakes/s per process, excluding heartbeats.
2. **Terminal throughput (#4, #6, #7, #8):** headless replay of `yes` and a 100k-line log through the server runtime, plus an end-to-end GUI run at 200×50. Record lines/s, server CPU, `delta_bytes`, `resyncs`, `terminal_delivery_latency_us`, and client full rebuilds per second. Target: resyncs near 0, and linear cost per line rather than per screen.
3. **Grid flush and upload (#3, #5, #9, #10, #20):** `GridRenderingPipeline::flush` microbenchmarks at 300×80 (code text with one-row scrolls; spaceless runs), plus a bytes-uploaded-per-frame and `relayout`-count counter. Scripted run: 4 panes at 4K, type 100 characters, then a 30-step resize. Compare Vulkan and Metal snapshots for regressions.
4. **Plugin idle and resize (#12–#17, #19):** hidden-tab CPU for SatView, Rezonality and MegaCity-with-LCOV over 60 s (target: close to 0 frames and propagations); VRAM per pane; scripted divider drag counting generation and target reallocations; ScoreView playing with the window minimised.
5. **Memory (#11, #28, #10):** server RSS with 1, 10 and 30 panes at 120 and 200 columns; app RSS and VRAM with a Markdown preview and many grid handles.

For CI, record baseline ratios (counts and bytes) rather than machine-dependent nanosecond thresholds, following the project's own `performance-instrumentation-audit.md` guidance.

## Existing performance safeguards to preserve

- Render on demand (`frame_requested_`), skip minimised windows, and coalesce resize requests into one per pass.
- The disabled `PERF_MEASURE` path is a single atomic check.
- Terminals:
  - server loop woken by an atomic exchange, with a 1 s idle ceiling;
  - hidden remote terminals suspended (`avoided_delta_encodes_`, visibility generations);
  - encoded events shared across subscribers;
  - ack-gated, byte-bounded stream lanes;
  - PTY input/output caps with backpressure;
  - the local host's 8 ms drain budget;
  - client wake coalescing (`mark_ready` / `acknowledge_wake`).
- Grid dirty-list deduplication; one atlas dirty-rect upload per frame into reused scratch memory; retained ligature scratch buffer.
- Frame slots and deferred reclamation (no normal-path waits); Metal completion handlers.
- Markdown and Kanban:
  - Markdown: dirty flags, binary-search visible rows, revision-gated uploads;
  - Kanban: `set_cell_if_changed`, the bounded-dirty perf tests, native watchers with a 150 ms debounce.
- Plugins:
  - `PluginHost` consumes render deadlines, drops no-op viewport changes and skips hidden draws;
  - SatView, Rezonality tick deadlines go idle when paused or hidden (the render side);
  - ScoreView's latest-wins window engraver and analysis cache;
  - revision-gated uploads in MegaCity and SatView.
- `SystemResourceMonitor` samples at 1 Hz and only publishes changes.
- Checkpoints are trailing-edge, and written on a worker only when the revision changes.

## Coverage

| Area | Representative paths / workloads examined | Inspected by | Remaining gaps |
|---|---|---|---|
| App frame loop, input, config, window | `app.cpp` pump_once/render_frame/wait_timeout/agents/diagnostics/resize; render_tree; input_dispatcher (keys, mouse, wheel, divider); sdl_window wait/wake; control_event_journal | Coordinator | pane_manager, space/tab controllers, gui_action_handler and main.cpp only skimmed; config I/O not reviewed (load-time only) |
| Neovim RPC / protocol / input | rpc.cpp reader and dispatch; mpack_codec; ui_events redraw/grid_line; input wheel/drag; nvim_host pump/clipboard | Coordinator (+W2 for nvim_host) | nvim_process pipes only grepped |
| Weather, perf collector, system resources | weather_service loop; perf_timing collector; system_resource_monitor | Coordinator | http clients not reviewed |
| Client/server/control/session | server loop, remote_terminal_service, session_stream/poll, coordinator, remote_terminal_host, async_frame_stream, control transports | W1, verified by coordinator | topology_service, server_kernel_requests/sessions partial; session_state TOML codec, control_codec, CSI/SGR not reviewed |
| Terminal core / PTY | scrollback_buffer, terminal_core newline/write, vt_parser ground state, unix_pty and ConPTY readers, agent observer/probe | W1 (+coordinator grid.cpp check) | terminal_core_csi, alt screen, Linux paths |
| Rendering, fonts, platform backends | renderer_state, vk_renderer/vk_atlas/vk_pipeline/vk_context, metal_renderer, text_service/selector/ligature/glyph_cache, grid_rendering_pipeline | W2, verified by coordinator | GPU-side costs unmeasured; Metal Markdown/NanoVG internals partial; box_drawing, text_atlas_builder |
| Chrome / overlays / GUI / UI | chrome_host, chrome_text_layer, vector pass, toast, palette (host and model), diagnostics host, ui_panel | W2 + coordinator | palette_renderer partial |
| Markdown / Kanban modules | markdown_host, draw list, Vulkan pass; kanban_host reload, kanban_store, file monitor (mac/win) | W2 | md4c parser, full layout builder, kanban layout/navigation |
| Plugin seam / SDK / support | plugin_host, plugin_storage, render passes, plugin_manager, plugin support (ImGui, NanoVG, adapter) | W4, verified by coordinator | vk_hdr_scene_pipeline, camera_input, tooltip |
| MegaCity | host pump/draw/deadline, scene snapshot, metrics overlay, semantic controller, Vulkan/Metal targets and prepass | W4 (key items verified) | builders, routing, geometry, shaders, tests |
| ScoreView | runtime pump/draw/relayout/input, presentation, NanoVG render, engraver, audio, session save | W4 (key items verified) | flow/stream/source_slicer internals, Verovio cost, analysis |
| SatView | worker, propagation executor, runtime draw, composer, filter, catalog/cloud services, Vulkan/Metal textures and HDR | W3 (key items verified) | shaders, draw internals, tools, most tests |
| Rezonality | watcher, build pipeline, runtime controller, Vulkan/Metal generation, Neovim Lua integration | W3 (key items verified) | image loader, camera, audio capture, Metal record |
| Build / tests / scripts / docs | CLAUDE.md, module-map, perf audit doc, perf benchmarks list, kanban lanes (root and 5 products) | Coordinator | build and test speed not assessed: no repeated-workflow cost was established |

**Delegation issues:** none. All four workers completed. Their findings were accepted only after I re-read the cited code; findings I only spot-checked are marked "medium" or "lower confidence" above. Two worker claims were corrected: palette discovery runs once per open, not per keypress (#24), and the hidden-host frame requests apply to Neovim hosts, not remote terminals (#26).
