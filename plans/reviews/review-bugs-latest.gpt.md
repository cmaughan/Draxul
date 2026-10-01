Partial static review of `repomix-output.xml`. Four read-only workers completed their assigned reviews; their accepted findings were independently checked against surrounding source. No files were changed, and no builds, tests, installations, or project binaries were run. Findings below exclude defects already explicitly tracked in the embedded root or product Kanban lanes.

1. **Summary:** Use safe arithmetic so opening the biology view does not trigger undefined behavior.

   **Severity: CRITICAL** — `plugins/megacity/product/draxul-geometry/src/cell_generator.cpp:35`

   Coordinate multiplications occur as signed integers before conversion to unsigned. Ordinary biology construction (`plugins/megacity/product/draxul-megacity/src/biology_builder.cpp:727`) supplies noise offsets whose octave expansion overflows these multiplications.
   
   **Fix:** Convert coordinates before multiplying, for example `static_cast<uint32_t>(ix) * 73856093u`, and likewise for the other axes.

2. **Summary:** Protect the shared asset folder so opening another satellite view cannot disrupt background file loading.

   **Severity: CRITICAL** — `plugins/satview/src/core/satview_texture_assets.cpp:96`

   Pane creation assigns a module-global filesystem path while existing catalog workers can read it at lines 88–89. Worker calls through `plugins/satview/src/core/satview_catalog.cpp:692` have no shared synchronization. Opening a second pane during catalog loading creates a data race and undefined behavior.

   **Fix:** Synchronize reads and writes, or give each service an immutable asset-root copy.

3. **Summary:** Prevent a closed editor connection from terminating the application.

   **Severity: CRITICAL** — `libs/draxul-nvim/src/nvim_process.cpp:566`

   On macOS/POSIX, Neovim can close stdin between the running check and a key, paste, resize, or shutdown write. The unprotected pipe write raises `SIGPIPE`, terminating the GUI before its error handling runs. The test executable ignores this signal, masking the production failure.

   **Fix:** Suppress `SIGPIPE` for parent-side writes while retaining the child’s normal signal disposition.

4. **Summary:** Limit large scrolling requests before moving saved prompt markers.

   **Severity: CRITICAL** — `libs/draxul-terminal-core/src/terminal_core.cpp:533`

   With default shell integration, the sequence `ESC[2;1H ESC]133;A BEL ESC[2147483647T` records a marker at row 1 and scrolls by `-INT_MAX`. `mark.row -= rows` then overflows before the grid clamps scrolling.

   **Fix:** Clamp displacement to the scrolling region first, or calculate marker movement in a wider integer type.

5. **Summary:** Reject saved identifiers that cannot safely support the next identifier calculation.

   **Severity: CRITICAL** — `libs/draxul-server/src/session_topology_bridge.cpp:224`

   Saved tab and Space identifiers of `INT_MAX` pass validation in `libs/draxul-session-model/src/session_state.cpp:599` and `:797`. Subsequent checkpoint capture computes signed `id + 1` at bridge lines 224 and 230. Standalone GUI restoration also overflows at `app/space_controller.cpp:286`.

   **Fix:** Validate identifier and allocator ranges before restoration, and use checked increment operations.

6. **Summary:** Handle unusable saved practice data without crashing when a piece opens.

   **Severity: CRITICAL** — `plugins/scoreview/product/draxul-score-learn/src/player_model.cpp:512`

   Valid JSON such as `{"total_notes":"bad"}` passes syntax validation, then throws during typed extraction. Invalid numeric map keys also throw at lines 537 and 556. Exceptions escape source attachment and plugin initialization, terminating the client. Existing corruption coverage checks malformed JSON syntax.

   **Fix:** Validate into temporary state, catch conversion failures, and return `false` to activate the existing fresh-session fallback.

7. **Summary:** Report unavailable temporary storage as a failed shader edit instead of terminating the application.

   **Severity: CRITICAL** — `plugins/rezonality/src/live_project.cpp:1494`

   Temporary-directory lookup uses the throwing overload outside the candidate-build handler. If the configured temporary directory disappears, the next valid edit throws through the worker’s unguarded build call at line 1533 and invokes `std::terminate`.

   **Fix:** Use the error-code overload and contain exceptions around the complete worker build operation.

8. **Summary:** Reject excessive color outputs before they crash the graphics backend.

   **Severity: CRITICAL** — `plugins/rezonality/src/native_backend_metal.mm:661`

   The scene parser accepts nine color targets. Pipeline preparation then indexes beyond Metal’s eight color-attachment slots, raising an uncaught Objective-C exception instead of rejecting the candidate and retaining the active scene.

   **Fix:** Validate attachment counts against backend limits before indexing native descriptors.

9. **Summary:** Show a printing error when temporary storage is unavailable instead of crashing the client.

   **Severity: CRITICAL** — `app/app.cpp:2064`

   Printing calls throwing `temp_directory_path()` after capture. A missing configured temporary directory throws through `finish_print_capture()`, rendering, and the application entry point; the existing print-error handling begins only after this lookup.

   **Fix:** Use the error-code overload and report the failure through the existing print toast.

10. **Summary:** Keep accepted session changes within the limits that saved sessions can represent.

    **Severity: HIGH** — `libs/draxul-server/src/topology_service.cpp:130`

    Live topology accepts names up to 4,096 bytes, but saved-session validation permits only 512. Renaming a Space, tab, or pane to 513 ASCII bytes succeeds and then prevents every checkpoint (`libs/draxul-server/src/server_kernel_sessions.cpp:980`). Restart restores older state. Space, tab, and tree-depth limits also disagree.

    **Fix:** Share live and durable validation limits, and ensure successful mutations remain serializable.

11. **Summary:** Avoid an endless wait when a Windows stream connection fails immediately.

    **Severity: HIGH** — `libs/draxul-control/src/async_frame_stream_win32.cpp:441`

    A client connecting and disconnecting before `ConnectNamedPipe()` can produce synchronous `ERROR_NO_DATA`. No pending operation exists, but the failure branch waits indefinitely on its unsignaled event. Stream acceptance stops, and server shutdown hangs joining the acceptor.

    **Fix:** Cancel and drain only operations that actually returned `ERROR_IO_PENDING`.

12. **Summary:** Use the known executable location so agent hooks work when the application is outside the command search path.

    **Severity: HIGH** — `libs/draxul-agent-integration/src/agent_integration.cpp:50`

    Windows and POSIX hooks invoke bare `draxul`, ignoring `DRAXUL_EXECUTABLE`, which the server supplies. Launching from an unpacked directory or app bundle outside `PATH` makes conversation reporting fail; subsequent restoration cannot resume the native conversation.

    **Fix:** Prefer the supplied executable path, with a bare-name fallback.

13. **Summary:** Preserve a wide character when the narrow character immediately before it changes.

    **Severity: HIGH** — `libs/draxul-grid/src/grid.cpp:308`

    Neighbor cleanup runs even for narrow incoming characters. Printing `" 界"`, returning to column 0, and printing `"A"` clears the unrelated wide character and its continuation because the next cell is a wide leader.

    **Fix:** Clear a neighboring wide leader only when the incoming character actually overlaps it.

14. **Summary:** Keep large editor input from blocking the application indefinitely.

    **Severity: HIGH** — `libs/draxul-nvim/src/rpc.cpp:269`

    Clipboard paste synchronously writes the entire message while holding the write mutex. If Neovim is suspended or stops reading, a paste exceeding pipe capacity blocks the GUI indefinitely on both platforms. Shutdown sends another notification before terminating the process, so it cannot reliably release a blocked writer.

    **Fix:** Use a bounded, cancellable outbound writer and cancel blocked writes during shutdown.

15. **Summary:** Recover the editor display when its update queue fills instead of silently losing changes.

    **Severity: HIGH** — `libs/draxul-nvim/src/rpc.cpp:369`

    At 4,096 notifications, the reader discards the oldest entry. Redraw messages contain incremental grid and state updates, so a burst during startup commands or delayed GUI draining can permanently lose changes. No complete redraw or state resynchronization follows overflow.

    **Fix:** Preserve ordered updates or explicitly reset and resynchronize the UI after overflow.

16. **Summary:** Keep accented and joined characters intact when output arrives in separate chunks.

    **Severity: HIGH** — `libs/draxul-terminal-core/src/vt_parser.cpp:274`

    Each feed flushes its final cluster. Feeding `"e"` and then UTF-8 combining accent plus `"X"` commits the accent to a separate cell and places `"X"` at column 2 instead of column 1. PTY and remote-output boundaries can split characters this way.

    **Fix:** Extend the preceding cluster when continuation codepoints arrive across feeds, preserving cursor position and width metadata.

17. **Summary:** Resize Markdown text correctly when the window moves between displays.

    **Severity: HIGH** — `modules/markdown/draxul-markdown/src/markdown_host.cpp:158`

    Markdown captures display density during initialization and uses it for its private font service. Viewport updates change layout but never update that density or rebuild the fonts. The host inherits the no-op font-metrics callback, so moving between displays with different densities leaves text at the old physical size.

    **Fix:** Propagate density changes and rebuild the private fonts and layout transactionally.

18. **Summary:** Decode source paths correctly so Windows can open files with non-English names.

    **Severity: HIGH** — `modules/markdown/draxul-markdown/src/markdown_host.cpp:64`

    Windows arguments are converted to UTF-8, but source and working-directory strings enter filesystem paths through narrow constructors that use the active Windows code page. On systems without a UTF-8 code page, a path such as `C:\文档\a.md` resolves incorrectly. CLI parsing repeats this conversion at `libs/draxul-app-cli/src/cli_args.cpp:220`.

    **Fix:** Use UTF-8-aware path construction and UTF-8 serialization at these boundaries.

19. **Summary:** Include bundled plugins in Windows deployment archives so installed product views can open.

    **Severity: HIGH** — `do.py:1294`

    Windows deployment copies only `assets`, `fonts`, `shaders`, and root DLLs. CMake stages native plugin packages under executable-adjacent `plugins`, which runtime discovery requires. A clean extracted archive therefore lacks bundled product plugins. The deployment test reproduces the incomplete directory allowlist.

    **Fix:** Include the complete published `plugins` directory in the staged payload and archive.

20. **Summary:** Keep paused satellite views responsive to controls and completed downloads.

    **Severity: HIGH** — `plugins/satview/src/satview_plugin.cpp:359`

    Pausing removes the next tick deadline. Frame requests only request drawing, and worker completion does not request a tick. Starting paused or refreshing while paused therefore leaves completed catalog/cloud results unconsumed. Keyboard camera movement also depends on the unscheduled runtime pump.

    **Fix:** Schedule ticks for completed work and active controls while freezing simulation time.

21. **Summary:** Keep pending label uploads separate so quick sign-color changes cannot overwrite their source data.

    **Severity: HIGH** — `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp:1316`

    Label revisions reuse one mapped staging buffer whenever capacity suffices. Successive automatic rebuilds with different sign colors and unchanged atlas dimensions can overwrite that buffer while another frame’s copy remains queued. Current-slot fencing does not protect another slot’s upload.

    **Fix:** Use staging storage owned by each frame slot or retire a separate allocation for each revision.

22. **Summary:** Preserve keyboard input when opening a music device fails.

    **Severity: HIGH** — `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:1311`

    A MIDI device can enumerate but fail to open. Selection creates a keyboard fallback and returns `false` for the requested device; the runtime then clears the entire input rig, deleting that fallback. Practice receives no input until the user explicitly changes the selection.

    **Fix:** Release the unsuccessful hardware lease without clearing the fallback rig.

23. **Summary:** Block the full track-clearance area around pads.

    **Severity: HIGH** — `plugins/pcbview/src/autorouter.cpp:243`

    Blocker iteration uses only the via radius, even when tracks are wider. With track width 2 mm, via pad 0.6 mm, grid step 0.5 mm, and a 1 mm pad, the required track radius is 1.5 mm but iteration covers only the smaller via extent. A track 1.4 mm from that pad can be accepted.

    **Fix:** Compute the iteration extent from `max(track_radius, via_radius)`.

24. **Summary:** Check clearance along each segment so diagonal tracks cannot overlap pads.

    **Severity: HIGH** — `plugins/pcbview/src/autorouter.cpp:397`

    Pattern routing and search validate grid nodes rather than connecting segments. With step 0.5 mm, pad diameter 0.4 mm, and track width 0.3 mm, nodes `(4.5,5)` and `(5,4.5)` pass clearance around a pad at `(4.6,4.6)`, but their diagonal passes only 0.212 mm away—inside the 0.35 mm keepout.

    **Fix:** Check segment-to-pad clearance for search edges, patterns, and endpoint connectors.

25. **Summary:** Synchronize shared depth buffers between rendering passes.

    **Severity: HIGH** — `plugins/rezonality/src/native_backend_vulkan.cpp:1068`

    Render-pass dependencies omit depth stages and accesses. The shipped robot scene clears a depth buffer in one pass and loads/tests/writes it in the next. There is no intervening depth barrier; the only depth transition initializes the resource once. Depth hazards can therefore produce incorrect occlusion.

    **Fix:** Include early/late fragment-test stages and appropriate depth read/write access in the dependencies.

26. **Summary:** Reject unsupported feedback inputs before a pass reads its current output texture.

    **Severity: HIGH** — `plugins/rezonality/src/native_backend_vulkan.cpp:1201`

    The parser accepts `targets:(A), samplers:(!A)` as previous-frame input, but neither backend implements alternate backing. Both resolve the target and sampler to the same texture. Vulkan additionally declares incompatible attachment and sampling layouts, producing an invalid feedback hazard.

    **Fix:** Reject unsupported previous-frame inputs and target/sampler aliases before preparing the candidate.

27. **Summary:** Deliver mouse releases to the pane that started a selection so dragging actually stops.

    **Severity: MEDIUM** — `app/input_dispatcher.cpp:536`

    A selection started inside a terminal loses its release when the pointer ends over a pane pill or chrome. The terminal remains in dragging state; subsequent movement back over it changes selection despite no button being held. Cross-pane routing can lose the release similarly.

    **Fix:** Retain the drag owner and deliver or cancel its release before chrome filtering.

28. **Summary:** Print the displayed pane area when a pane is zoomed.

    **Severity: MEDIUM** — `app/app.cpp:2026`

    Printing obtains the pane’s stored split rectangle. Zooming preserves that rectangle but displays the host across the full viewport (`app/pane_manager.cpp:1476`). Printing a zoomed pane therefore crops a stale subrectangle, potentially omitting content or using the wrong offset.

    **Fix:** Capture the current displayed viewport, including zoom state, when choosing the print crop.

29. **Summary:** Finish checking cache writes before replacing the last saved copy.

    **Severity: MEDIUM** — `plugins/satview/src/services/satview_cache_publication.cpp:75`

    The helper checks stream state immediately after writing, but does not check the destructor’s final flush/close. A full disk or exhausted quota during that flush can leave a truncated temporary file that subsequently replaces the valid cache and reports success.

    **Fix:** Explicitly close and check the stream before replacement; discard the temporary file on failure.

30. **Summary:** Release the first graphics buffer when creating the second one fails.

    **Severity: MEDIUM** — `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_vk_resources.cpp:69`

    Mesh upload allocates a vertex buffer, then returns on index-buffer allocation failure without releasing it. Foliage callers use local plain handle records with no destructor, so the successful allocation is lost. Repeated retries under memory pressure leak further buffers.

    **Fix:** Destroy the vertex allocation on failure or retain scoped ownership until both allocations succeed.

31. **Summary:** Retire an acquired drawing surface when frame preparation fails.

    **Severity: MEDIUM** — `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:1101`

    Command-buffer preparation or atlas staging failure after image acquisition returns without consuming/replacing the signaled acquisition semaphore or retiring the acquired image. The next frame reuses the same slot and semaphore, violating synchronization requirements and potentially stalling rendering.

    **Fix:** Route post-acquisition failures through synchronization recovery and frame abort/recreation.

32. **Summary:** Skip drawing a resized grid when its larger graphics buffer cannot be allocated.

    **Severity: MEDIUM** — `libs/draxul-renderer/src/metal/metal_renderer.mm:121`

    Failed buffer growth retains the old smaller buffer. Because upload returns no status, its caller draws the new larger instance counts using that allocation. The shader directly indexes by instance number, producing out-of-range graphics-buffer reads under memory pressure.

    **Fix:** Return upload success and skip drawing unless the allocation holds the current state.

33. **Summary:** Retry failed font-image uploads so temporary allocation failures do not leave text missing.

    **Severity: MEDIUM** — `libs/draxul-renderer/src/metal/metal_renderer.mm:1065`

    Staging allocation or mapping failure clears queued atlas uploads after CPU dirty state has already been cleared. Cached glyphs produce no replacement upload, leaving missing or stale text until a reset. A failed blit-encoder creation is also unchecked before clearing the queue.

    **Fix:** Preserve pending uploads on failure and retry before drawing dependent glyphs.

34. **Summary:** Allow replacing a terminal with an agent when a tab is full.

    **Severity: MEDIUM** — `libs/draxul-server/src/topology_service.cpp:682`

    The pane-count guard rejects `agent start --replace-pane` at 256 panes before examining replacement mode. The replacement branch updates one existing pane and adds none, so a full tab with one terminal and 255 client-local panes is incorrectly rejected.

    **Fix:** Apply the pane-count limit only when adding a pane.

Unresolved leads, excluded from accepted findings:

| Lead | Evidence still needed |
|---|---|
| Neovim wide-cell decoding at `libs/draxul-nvim/src/ui_events.cpp:293` | A packed protocol specification or real wire fixture establishing explicit empty continuation cells. |
| Remote compatibility worker command loss at `libs/draxul-host/src/remote_terminal_host.cpp:652` | A supported application path using this worker instead of the coordinator; coordinator failures requeue commands. |
| Pseudoconsole shutdown at `libs/draxul-terminal-process/src/conpty_process.cpp:826` | Confirmation that native close can require output drainage after this reader stops. |
| Companion-owner removal | A demonstrated user-visible failure after server topology closes an owner while retaining its companion. |
| Non-finite saved split ratios | A complete standalone restoration trace showing the value survives later guards and causes failure. |
| Product worker publication and Metal audio texture updates | Complete interleavings establishing stale publication or overlapping graphics-resource use. |

Coverage is representative, not exhaustive. All four delegated reviews completed; there were no delegation failures.

| Area | Representative paths or flows examined | Reviewer | Remaining gaps |
|---|---|---|---|
| Architecture and configuration | Embedded guidance, module map, feature documentation, CMake ownership, startup/reload | Coordinator and workers | Documentation and configuration combinations sampled |
| Application orchestration | `app/app.cpp`, pane/tab/Space controllers, input routing, printing, shutdown | Coordinator | Some GUI actions and platform UI branches |
| Client/server/control/session | State loop, leases, authentication, topology, checkpoints, poll/stream, recovery | Server worker; coordinator verification | Discovery, codecs, registry and CLI branches partly reviewed |
| Terminal/PTY/Neovim | Parser, scrolling, marks, process lifecycle, RPC, input and shutdown | Terminal worker; coordinator verification | Exhaustive modes, real wide-cell packets, ConPTY argument probing |
| Rendering/fonts/window | Vulkan and Metal frame/resource lifecycle, grids, atlases, shaping, SDL and NanoVG | Terminal worker; coordinator verification | Shader/stencil internals, native fault behavior and some shared GPU helpers |
| Built-in modules | Markdown fonts/layout/rendering/navigation; Kanban storage, monitoring and previews | Coordinator | Exhaustive document and UI cases |
| Plugin host and SDK | Discovery, ABI callbacks, storage, quiescence, reload and spinning-triangle lifecycle | Coordinator | External plugins and complete SDK consumer combinations |
| MegaCity/BioView | Workers, scene publication, geometry, input, native uploads and retirement | Product worker; coordinator verification | Semantic analysis, geometry variants and rendering mathematics sampled |
| SatView | Catalog/cloud/simulation workers, cache publication, pause/input scheduling, assets | Product worker; coordinator verification | Astronomy algorithms, panels and shaders sampled |
| ScoreView | Initialization, progress, device input, audio and engraver ownership | Product worker; coordinator verification | Large notation, analysis, scoring and transport portions |
| PCBView | Model validation, routing blockers, patterns, search and legalization | Product worker; coordinator verification | Multilayer search and detailed resource-footprint branches |
| Rezonality | Parser/build/watch, candidate lifecycle, native preparation, barriers and recording | Product worker; coordinator verification | Shader conversion, ray paths and editor integrations |
| Build/scripts/tests/tracking | Deployment, plugin staging/publishing, SDK targets, relevant tests and actual Kanban cards | Coordinator and workers | Tests inspected selectively; no execution or runtime validation |
