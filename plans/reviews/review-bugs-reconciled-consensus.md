# Bug-review consensus

Verified both reviews listed in `INPUT_REVIEWS/index.md`; their hashes match the index. Current source supports **58 distinct findings: 22 CRITICAL, 21 HIGH, and 15 MEDIUM**. **34 are already tracked; 24 proposed cards follow below.**

Four read-only reviewers used `gpt-6.1-sol` with high reasoning effort, read the same inputs, and completed without nested delegation. I re-read their supporting source before accepting findings. No files were changed, and no builds, installations, tests, or project binaries were run.

`B1`–`B34` refer to Codex’s numbered findings. Claude references retain its `C`, `H`, and `M` identifiers. Existing-card mappings appear after the findings.

## Architecture and coverage

The application coordinates window input, panes, rendering, and built-in Markdown/Kanban views. The server owns terminal processes, session topology, and checkpoints; client workers deliver commands and project that state into the application. Neovim uses a separate process/pipe transport. Shared font, grid, renderer, plugin-host, and interface-support libraries serve these layers. All five initialized product submodules own their product behavior and trackers.

| Area | Reports’ complementary coverage | Verification limits |
|---|---|---|
| Client/server/control | Codex emphasized durable limits, identifiers, streams, and hooks; Claude emphasized retirement, retries, wakeups, and settings replacement. | Concurrency interleavings were traced statically, not reproduced. |
| Input, terminal, built-in views | Codex emphasized chunked text, neighboring cells, printing, and display density; Claude added zoomed hit-testing, shortcut prefixes, and filename exceptions. | No interactive input or display checks. |
| Rendering/fonts/plugin support | Codex emphasized allocation failures and upload lifetime; Claude added configured atlas dimensions and cross-pane texture ownership. | Native graphics validation and allocation fault injection remain outstanding. |
| Products | Codex emphasized geometry, uploads, routing clearance, scheduling, and recovery; Claude added parser limits, worker generations, imported values, and teardown. | Product algorithms and shaders were not exhaustively reviewed. |
| Tooling | Codex examined deployment packaging; Claude examined build-lock process checks. | No deployment or competing-build execution. |

Root and all five product `pending`, `ice-box`, and `done` lanes were inspected. Board configuration entries without an actual card file were not treated as tracked work.

## Confirmed findings — CRITICAL

### F01 — Configured font-image dimensions disagree

**Summary:** Use matching font-image dimensions so changing the size setting cannot break text or write outside drawing storage.

`app/app.cpp:483` creates the renderer using `config_.atlas_size`, but `libs/draxul-font/src/glyph_atlas_manager.h:27` initializes the cache with its default 2048 size. `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp:336` uploads that full image; `libs/draxul-renderer/src/vulkan/vk_atlas.cpp:268` performs the unchecked copy. **Trigger:** configure 1024, producing an oversized copy, or 4096/8192, producing incorrectly normalized texture coordinates. **Fix:** propagate one dimension through font initialization and renderer creation; validate upload bounds in both backends. **Reported:** Claude C1. **Tracking:** new core card.

### F02 — Retired server overwrites successor checkpoints

**Summary:** Stop retired servers from replacing newer saved layouts so restarting preserves the replacement server’s changes.

`libs/draxul-server/src/server_kernel_lifecycle.cpp:414` exits after losing endpoint ownership, but line 651 still processes queued requests and line 664 schedules final checkpoints. Detached writers at `libs/draxul-server/src/server_kernel_sessions.cpp:995` retain the same destination. **Trigger:** a successor saves changes before the displaced server finishes retiring. **Fix:** distinguish eviction from ordinary shutdown and prevent both newly scheduled and already-running writes from publishing after ownership is lost. **Reported:** Claude C2. **Tracking:** new core card.

### F03 — Failed hook installation deletes existing settings

**Summary:** Preserve existing settings when replacement fails so installing conversation hooks cannot erase them.

`libs/draxul-agent-integration/src/agent_integration.cpp:194` deletes the destination after a failed rename; line 202 deletes the staged replacement if the second rename fails. **Trigger:** Windows allows destination deletion while another process prevents renaming the temporary file. **Fix:** use nondestructive replacement and preserve recoverable contents on failure. **Reported:** Claude H5. **Tracking:** new core card.

### F04 — Diagnostic textures are released through another pane’s owner

**Summary:** Release each pane’s diagnostic images through their owner so closing one pane cannot corrupt another pane’s drawing state.

`plugins/megacity/product/draxul-megacity/src/megacity_host.cpp:1207` and `plugins/satview/src/runtime/satview_runtime.cpp:766` destroy scene passes before selecting their owning interface context. `plugins/support/imgui/src/imgui_impl_vulkan.cpp:1273` frees descriptors using the **current** context’s pool. **Trigger:** render two panes, then close the first while the second context is current; SatView requires diagnostic images to have been enabled. A newly created, unrendered SatView context also permits a null-backend dereference. **Fix:** bind texture removal to its owner and perform it before backend destruction. **Reported:** Claude C3 and C4, merged. **Tracking:** one new shared-boundary card.

### F05 — International Rezonality paths throw through plugin callbacks

**Summary:** Preserve international project names so opening or reloading a graphics project cannot close the application.

`plugins/rezonality/src/rezonality_plugin.cpp:552` serializes `generic_string()` into strict JSON without containment; line 500 uses an unchecked filename conversion. Host export calls at `libs/draxul-host/src/plugin_host.cpp:291` occur before its parse-only handler. **Trigger:** Windows uses a non-UTF-8 code page and a project path contains non-ASCII characters. **Fix:** serialize native paths explicitly as UTF-8 and contain failures in affected exported callbacks. **Reported:** Claude C5. **Tracking:** new Rezonality card.

### F06 — Kanban filename conversions escape error handling

**Summary:** Preserve card names in their original characters so opening a board cannot crash on international names.

`modules/kanban/draxul-kanban/src/kanban_store.cpp:658` calls `filename().string()` without containment; repository and lane names repeat this at lines 578 and 616. **Trigger:** Windows scans a card such as `07 🐛 crash -bug.md` under a code page that cannot represent its name. **Fix:** use explicit UTF-8 display strings while retaining native paths and contain scan failures. **Reported:** Claude C6. **Tracking:** new Kanban card; the related Markdown boundary at `modules/markdown/draxul-markdown/src/markdown_host.cpp:367` overlaps existing B18 work.

### F07 — Biology noise uses overflowing signed products

**Summary:** Use safe calculations for biological shapes so opening the biology view cannot cause invalid arithmetic.

`plugins/megacity/product/draxul-geometry/src/cell_generator.cpp:35` multiplies signed coordinates before converting to unsigned. Ordinary seed offsets and octave expansion exceed the safe range. **Trigger:** build normal biology geometry. **Fix:** convert before multiplication and use unsigned constants. **Reported:** Codex B1. **Tracking:** existing.

### F08 — Satellite asset-root path is concurrently mutated

**Summary:** Give background loaders a stable asset location so opening another satellite pane cannot disrupt file loading.

`plugins/satview/src/core/satview_texture_assets.cpp:96` assigns a module-global path while lines 88–89 can read it from catalog workers. **Trigger:** create another pane during bundled-file loading. **Fix:** use immutable per-service roots or shared synchronization for every access. **Reported:** Codex B2. **Tracking:** existing.

### F09 — Closed Neovim pipe can terminate the client

**Summary:** Handle a closed editor connection safely so sending input cannot terminate the application.

`libs/draxul-nvim/src/nvim_process.cpp:566` performs an unprotected POSIX pipe write. Production Neovim-only client startup does not establish parent-side signal suppression; the test runner does. **Trigger:** Neovim closes stdin immediately before a notification write. **Fix:** suppress parent-side `SIGPIPE` while preserving normal child signal behavior. **Reported:** Codex B3. **Tracking:** existing.

### F10 — Extreme scrolling overflows prompt-marker rows

**Summary:** Limit scrolling before moving prompt markers so extreme terminal output cannot cause invalid arithmetic.

`libs/draxul-terminal-core/src/terminal_core.cpp:533` subtracts an unclamped displacement before the grid clamps it. **Trigger:** record a marker at row one, then send `ESC[2147483647T`, or an extreme insert-line request. **Fix:** clamp displacement to the affected region before moving markers and cells. **Reported:** Codex B4 and Claude M7. **Tracking:** existing.

### F11 — Restored identifiers overflow their next-number calculation

**Summary:** Reject saved numbers that cannot support another item so restoring a session cannot overflow its counters.

`libs/draxul-session-model/src/session_state.cpp:599` permits `INT_MAX` tab identifiers. `libs/draxul-server/src/session_topology_bridge.cpp:224` subsequently computes `id + 1`; Space capture and standalone restoration repeat it. **Trigger:** restore a structurally valid checkpoint containing maximum signed identifiers. **Fix:** validate identifier/counter ranges before restoration and check increments. **Reported:** Codex B5. **Tracking:** existing.

### F12 — Mistyped practice data terminates initialization

**Summary:** Reject unusable saved practice data so opening a piece can start safely.

`plugins/scoreview/product/draxul-score-learn/src/player_model.cpp:512` performs unchecked typed extraction; line 537 converts map keys with `stoi`. Exceptions escape source attachment and initialization. **Trigger:** saved JSON contains `{"total_notes":"bad"}`, null counters, or invalid numeric keys. **Fix:** validate temporary state, contain conversion failures, and activate the existing fresh-session fallback. **Reported:** Codex B6 and Claude M9. **Tracking:** existing.

### F13 — Rezonality temporary-directory failure escapes its worker

**Summary:** Report unavailable temporary storage so a failed shader edit leaves the application running.

`plugins/rezonality/src/live_project.cpp:1494` performs throwing directory lookup outside candidate handling; line 1533 invokes the build without containment. **Trigger:** temporary storage disappears before a valid rebuild. **Fix:** use error-code lookup and contain the complete build operation, retaining the active generation. **Reported:** Codex B7. **Tracking:** existing.

### F14 — Excessive color outputs exceed native attachment limits

**Summary:** Reject excessive color outputs so an invalid scene cannot crash the drawing backend.

`plugins/rezonality/src/native_backend_metal.mm:661` indexes fixed attachment slots using an unrestricted target count. Vulkan also lacks device-limit validation. **Trigger:** a valid scene declares nine non-depth outputs. **Fix:** validate each backend’s output limit before native preparation or recording. **Reported:** Codex B8. **Tracking:** existing.

### F15 — Printing temporary-directory failure escapes the application

**Summary:** Show a printing error when temporary storage is unavailable so printing cannot close the application.

`app/app.cpp:2064` performs throwing lookup after capture and before print-error handling. **Trigger:** print successfully captured content after temporary storage becomes unavailable. **Fix:** report lookup failure through the existing print notification path. **Reported:** Codex B9. **Tracking:** existing.

### F16 — Failed Metal buffer growth leaves oversized draws

**Summary:** Skip drawing after larger storage cannot be allocated so resizing cannot read beyond the available memory.

`libs/draxul-renderer/src/metal/metal_renderer.mm:121` retains the old buffer after growth failure; line 882 ignores upload success and lines 932/943 draw the new instance counts. **Trigger:** allocation fails after a grid grows. **Fix:** return upload status and draw only when current storage holds the complete state. **Reported:** Codex B32. **Tracking:** existing.

### F17 — Cache replacement ignores final write failure

**Summary:** Check completed cache writes before replacement so storage failures cannot overwrite a valid saved copy with incomplete data.

`plugins/satview/src/services/satview_cache_publication.cpp:75` checks the write before destructor flush/close, then replaces the destination at line 83. The cloud writer repeats the pattern. **Trigger:** buffered output fails during final close because storage or quota is exhausted. **Fix:** explicitly close, check failure, and preserve the destination. **Reported:** Codex B29. **Tracking:** existing.

### F18 — Detached microphone opener can outlive its module

**Summary:** Finish microphone-opening work before releasing its code so quitting during a permission prompt cannot crash.

`plugins/scoreview/product/draxul-scoreview/src/mic_player_input.cpp:217` detaches the worker; destruction marks abandonment without joining. At process static teardown, `libs/draxul-plugin/src/plugin_manager.cpp:219` releases resident modules. **Trigger:** quit while the permission worker sleeps, then unload its module before it resumes. **Fix:** track worker completion and module lifetime using cancellable permission waits or retained module ownership; preserve nonblocking device handling. **Reported:** Claude M8. **Tracking:** new ScoreView card.

### F19 — Short ScoreView panes reverse clamp bounds

**Summary:** Fit the score within short panes so resizing cannot trigger an invalid layout operation.

`plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:402` and `score_presentation.cpp:152` use a minimum of `96 * scale` and maximum of `0.9 * height`. **Trigger:** a loaded Flow/Roll pane is 100 pixels high at scale one. The lower bound exceeds the upper bound, violating `std::clamp`’s precondition. **Fix:** derive ordered bounds or explicitly handle insufficient space. **Reported:** Claude M10. **Tracking:** new ScoreView card.

### F20 — Imported key signatures overflow analysis arithmetic

**Summary:** Validate extreme key signatures so imported scores cannot cause invalid musical calculations.

`plugins/scoreview/product/draxul-notation/src/musicxml_importer.cpp:268` accepts any representable integer for fifths; `plugins/scoreview/product/draxul-score-learn/src/piece_analysis.cpp:63` multiplies it by seven. **Trigger:** plain MusicXML specifies `<fifths>2147483647</fifths>` and reaches analysis. **Fix:** validate supported signatures and independently make pitch-class normalization safe. **Reported:** Claude M16, arithmetic portion. **Tracking:** new ScoreView card.

### F21 — Satellite catalog parser has unbounded recursion

**Summary:** Reject excessively nested catalog data so a malformed download or saved file cannot exhaust the application’s stack.

`plugins/satview/src/core/satview_catalog.cpp:274` recurses through object/array skipping; lines 296 and 314 have no depth limit. **Trigger:** an object field contains tens of thousands of nested arrays, well within the response-size limit. **Fix:** bound nesting before record validation and return diagnostics while preserving usable catalog state. **Reported:** Claude M13. **Tracking:** new SatView card.

### F22 — Extreme satellite identifiers reach unsafe conversion and absolute value

**Summary:** Reject invalid satellite identifiers before conversion so unusual catalog data cannot cause invalid arithmetic.

`plugins/satview/src/core/satview_catalog.cpp:560` compares against a rounded floating representation of `INT64_MAX`, admitting `2^63`. `plugins/satview/src/core/satview_propagation.cpp:264` calls `llabs(INT64_MIN)`. **Trigger:** with no usable startup catalog, a tampered bundled sample supplies an otherwise valid record with `INT64_MIN`; sample fallback bypasses the normal merge’s positive-ID check. **Fix:** enforce positive, representable identifiers at all entry points and protect propagation defensively. **Reported:** Claude M14. **Tracking:** new SatView card.

## Confirmed findings — HIGH

### F23 — Zoomed hit-testing changes focus to hidden panes

**Summary:** Keep mouse input on the visible zoomed pane so typing cannot move to a hidden pane.

`app/pane_manager.cpp:1193` hit-tests the stored split layout without checking zoom; line 1196 changes focus. `app/input_dispatcher.cpp:658` calls this on mouse movement. **Trigger:** zoom one pane, then move over another pane’s hidden rectangle. **Fix:** route hit-testing to the zoomed leaf and suppress hidden divider hits. **Reported:** Claude H2. **Tracking:** new core card.

### F24 — Neovim wide cells advance twice

**Summary:** Preserve the editor’s cell positions so wide characters do not shift the rest of a line.

`libs/draxul-nvim/src/ui_events.cpp:295` advances an extra column for a wide character before processing its explicit empty follower. **Trigger:** receive a real wide-character `grid_line` packet. **Fix:** advance once per protocol cell and handle continuation cells without clearing their leader. Neovim documents the empty follower explicitly. [Neovim UI protocol](https://neovim.io/doc/user/api-ui-events/#ui-event-grid_line) **Reported:** Claude H3; resolves Codex’s corresponding lead. **Tracking:** new core card.

### F25 — Routes from an old grid replace a rebuilt city

**Summary:** Keep routes tied to their city so rebuilding cannot restore an older grid.

`plugins/megacity/product/draxul-megacity/src/megacity_host.cpp:1549` can schedule routes using the preserved old grid after a new layout starts building; line 744 adopts the route result unconditionally. **Trigger:** rebuild while a building is selected and route/grid workers finish in either order. **Fix:** associate route requests with grid identity and layout generation, reject replaced-grid results, and rearm routes after grid publication. **Reported:** Claude H4. **Tracking:** new MegaCity card.

### F26 — Accepted topology exceeds checkpoint limits

**Summary:** Keep accepted layout changes within saving limits so successful changes survive a restart.

`libs/draxul-server/src/topology_service.cpp:130` accepts names up to 4096 bytes; durable validation permits 512. Counts and tree depth also disagree. **Trigger:** rename a pane, tab, or Space to 513 ASCII bytes. Subsequent checkpoints fail. **Fix:** share live/durable bounds and validate before publishing mutations. **Reported:** Codex B10. **Tracking:** existing.

### F27 — Windows stream failure waits for nonexistent work

**Summary:** Finish failed connection attempts promptly so they cannot block new connections or shutdown.

`libs/draxul-control/src/async_frame_stream_win32.cpp:445` waits indefinitely after any failed connection, including failures that never became pending. **Trigger:** an early connect/disconnect produces synchronous `ERROR_NO_DATA`. **Fix:** cancel and drain only genuinely pending operations. **Reported:** Codex B11. **Tracking:** existing.

### F28 — Agent hooks ignore the supplied executable path

**Summary:** Use the supplied application location so conversation reporting works outside the command search path.

`libs/draxul-agent-integration/src/agent_integration.cpp:50` invokes bare `draxul`; POSIX hooks do likewise despite receiving `DRAXUL_EXECUTABLE`. **Trigger:** launch from an unpacked directory or application bundle absent from `PATH`. **Fix:** safely invoke the supplied path. **Reported:** Codex B12. **Tracking:** existing.

### F29 — Narrow-cell updates erase a neighboring wide glyph

**Summary:** Preserve nearby wide characters when updating a narrow character that does not overlap them.

`libs/draxul-grid/src/grid.cpp:308` clears the next wide leader regardless of incoming width. **Trigger:** write `" 界"`, then replace column zero with `"A"`. **Fix:** restrict neighboring cleanup to actual overlap. **Reported:** Codex B13. **Tracking:** existing.

### F30 — Synchronous Neovim writes freeze the interface

**Summary:** Make large editor input cancellable so an editor that stops reading cannot freeze the interface.

`libs/draxul-nvim/src/rpc.cpp:269` writes the complete message synchronously under the write mutex. **Trigger:** paste beyond pipe capacity while Neovim is suspended or not reading. **Fix:** bounded outbound ownership, cancellable writes, and cancellation before shutdown waits. **Reported:** Codex B14. **Tracking:** existing.

### F31 — Dropped Neovim redraws have no recovery

**Summary:** Recover the complete editor display after queue overflow so discarded updates cannot leave it permanently wrong.

`libs/draxul-nvim/src/rpc.cpp:369` drops the oldest notification at capacity. **Trigger:** more than 4096 notifications arrive while the interface is delayed. **Fix:** preserve ordered delivery or explicitly invalidate and resynchronize display state. **Reported:** Codex B15 and Claude M6. **Tracking:** existing.

### F32 — Character clusters split across terminal feeds

**Summary:** Keep accented and joined characters together when terminal output arrives in separate pieces.

`libs/draxul-terminal-core/src/vt_parser.cpp:274` flushes the final cluster after every feed. **Trigger:** feed `"e"`, then a combining accent and `"X"`; the accent becomes another cell. **Fix:** retain enough preceding-cluster state to extend continued characters while preserving cursor and width metadata. **Reported:** Codex B16. **Tracking:** existing.

### F33 — Markdown retains old display density

**Summary:** Resize Markdown text after display changes so it keeps the intended readable size.

`modules/markdown/draxul-markdown/src/markdown_host.cpp:109` captures initial density; line 158 updates viewport geometry without rebuilding private fonts. **Trigger:** move the window between differently scaled displays. **Fix:** deliver density changes and rebuild fonts/layout transactionally. **Reported:** Codex B17. **Tracking:** existing.

### F34 — Windows Markdown launch paths use narrow conversion

**Summary:** Preserve source-path characters so Windows can open Markdown files with international names.

`modules/markdown/draxul-markdown/src/markdown_host.cpp:64` constructs native paths from UTF-8 launch strings through narrow constructors; CLI source parsing at `libs/draxul-app-cli/src/cli_args.cpp:220` crosses the same boundary. **Trigger:** open a non-English source under a non-UTF-8 code page. **Fix:** use explicit UTF-8 input/output boundaries and native filesystem paths internally. **Reported:** Codex B18; related Claude C6 Markdown portion. **Tracking:** existing.

### F35 — Windows deployment omits plugin packages

**Summary:** Include bundled product views in Windows downloads so the views supplied by the build can open.

`do.py:1294` copies only assets, fonts, and shaders alongside root libraries, omitting executable-adjacent plugin packages. **Trigger:** extract a deployment archive on a clean machine. **Fix:** stage complete published plugin packages and selection metadata. **Reported:** Codex B19. **Tracking:** existing.

### F36 — Paused satellite panes stop processing work

**Summary:** Keep paused satellite panes processing controls and completed downloads so changes can appear without resuming time.

`plugins/satview/src/satview_plugin.cpp:363` removes the tick deadline when paused; drawing requests and worker completion do not schedule another runtime pump. **Trigger:** paused startup, refresh, or camera-key input. **Fix:** schedule completion/control ticks while freezing simulation time. **Reported:** Codex B20. **Tracking:** existing.

### F37 — Pending sign uploads share overwritten storage

**Summary:** Give pending sign-image uploads separate storage so quick edits cannot replace pixels still being copied.

`plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp:1316` overwrites one staging allocation when its capacity suffices. **Trigger:** equal-size atlas revisions arrive while another frame’s copy remains pending. **Fix:** use frame-owned staging or separately retired allocations. **Reported:** Codex B21. **Tracking:** existing.

### F38 — Failed music-device selection deletes keyboard fallback

**Summary:** Keep keyboard input available when a selected music device cannot open.

`plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:1313` clears the input rig after selection has installed a keyboard fallback but returned hardware failure. **Trigger:** an enumerated music device fails to open. **Fix:** release the failed hardware lease without clearing the fallback. **Reported:** Codex B22. **Tracking:** existing.

### F39 — Wide-track blockers use the smaller via radius

**Summary:** Block the full required space around pads so wide tracks cannot pass too close.

`plugins/pcbview/src/autorouter.cpp:244` derives blocker iteration from via radius even when track radius is larger. **Trigger:** 2 mm track, 0.6 mm via, 0.5 mm grid, and a 1 mm pad can miss a track node 1.4 mm away. **Fix:** iterate using the larger radius while retaining separate distance checks. **Reported:** Codex B23. **Tracking:** existing.

### F40 — Clear route nodes can connect through a pad

**Summary:** Check clearance along complete tracks so diagonal sections cannot overlap unrelated pads.

`plugins/pcbview/src/autorouter.cpp:397` validates pattern nodes rather than their connecting segments; search and emitted endpoint connectors have the same gap. **Trigger:** the reported diagonal passes 0.212 mm from a pad despite a 0.35 mm keepout and individually clear nodes. **Fix:** validate segment clearance during search, patterns, connectors, and final output. **Reported:** Codex B24. **Tracking:** existing.

### F41 — Shared depth images lack inter-pass synchronization

**Summary:** Synchronize shared depth images so successive drawing passes preserve correct visibility.

`plugins/rezonality/src/native_backend_vulkan.cpp:1068` supplies dependencies covering color/sampling but omitting depth stages and accesses. **Trigger:** the bundled robot scene clears depth, then loads/tests/writes it in another pass. **Fix:** add required depth synchronization, including reuse across frames. **Reported:** Codex B25. **Tracking:** existing.

### F42 — Unsupported feedback reads the current output texture

**Summary:** Reject unsupported feedback inputs so scenes cannot read a texture while writing the same output.

`plugins/rezonality/src/native_backend_vulkan.cpp:1201` resolves previous-frame sampling to the current surface; Metal resolves the same alias. **Trigger:** `targets:(A), samplers:(!A)` or an unmarked same-pass alias. **Fix:** reject unsupported previous-frame inputs and output/input aliases before preparation. **Reported:** Codex B26. **Tracking:** existing.

### F43 — Filtered mouse releases leave drags active

**Summary:** Deliver releases to the active drag owner so selections and dividers stop moving when the button is released.

`app/input_dispatcher.cpp:536` and line 545 return before divider cleanup at line 583 and host release delivery. **Trigger:** release over a pane label, chrome, overlay, or another pane. **Fix:** deliver or cancel matching releases before filtering and retain press ownership. **Reported:** Codex B27 and Claude H1. **Tracking:** existing; its card already includes divider behavior.

## Confirmed findings — MEDIUM

### F44 — Retried layout commands discard status requests

**Summary:** Complete status requests even when a simultaneous layout request encounters a connection failure.

`libs/draxul-client/src/remote_session_client.cpp:886` removes a status request, then line 902 skips publication after a transient/resynchronizing command failure. **Trigger:** status or stop confirmation is queued beside a topology command during an outage or stale epoch. **Fix:** complete status independently or retain it for retry. **Reported:** Claude M1. **Tracking:** new core card.

### F45 — Server wake notifications can be lost

**Summary:** Make wake-up signals reliable so pending work does not wait for the idle timeout.

`libs/draxul-server/src/server_kernel_lifecycle.cpp:317` notifies without the waiter mutex used at line 643; terminal output and stop repeat this. **Trigger:** notification arrives after predicate evaluation but before the waiter blocks. **Fix:** coordinate flag publication and notification with the wait mutex. **Reported:** Claude M2. **Tracking:** new core card.

### F46 — Poll worker ignores remembered wake requests

**Summary:** Remember background wake requests so input and shutdown are not delayed during recovery.

`libs/draxul-client/src/remote_session_coordinator.cpp:2504` and line 2559 wait without testing `worker_pending_` or stopping. **Trigger:** input or stop arrives immediately before a recovery wait, potentially delaying the interface-thread join by five seconds. **Fix:** use the existing predicate-based worker wait helper in both branches. **Reported:** Claude M3. **Tracking:** new core card.

### F47 — First Windows control listener spins after an early disconnect

**Summary:** Reset failed listeners so a quick disconnect cannot leave one retrying forever.

`libs/draxul-control/src/control_transport_win32.cpp:499` permits synchronous connection errors to fall through; line 583 retains the first pipe without disconnecting it. **Trigger:** early disconnect leaves repeated `ERROR_NO_DATA`. **Fix:** disconnect/reset the instance and back off on connection failure. **Reported:** Claude M4. **Tracking:** new core card; distinct from F27’s stream wait.

### F48 — Shortcut chords forget their prefix

**Summary:** Remember which shortcut prefix was pressed so the second key runs the intended command.

`app/input_dispatcher.cpp:366` stores only prefix-active state; line 332 matches the second key against every chord. **Trigger:** configure `Ctrl+S, z` and `Ctrl+B, z`, then press the latter. **Fix:** retain normalized prefix key/modifiers and require them to match. **Reported:** Claude M5. **Tracking:** new core card.

### F49 — Coverage import misreads current function records

**Summary:** Read coverage records correctly so executed functions are not silently shown as uncovered.

`plugins/megacity/product/draxul-megacity/src/lcov_coverage.cpp:107` treats an optional end-line field as part of the function name and ignores `FNL`/`FNA`. **Trigger:** `FN:10,20,foo` followed by `FNDA:5,foo`, or current indexed records. **Fix:** parse numeric prefixes and indexed records while preserving commas inside names. The current format is documented by LCOV. [LCOV tracefile format](https://github.com/linux-test-project/lcov/blob/master/docs/man/geninfo.rst) **Reported:** Claude M11. **Tracking:** new MegaCity card.

### F50 — Failed source scans never become retryable

**Summary:** Recover from a failed source scan so a temporary folder error cannot leave the view permanently waiting.

`plugins/megacity/product/draxul-treesitter/src/treesitter.cpp:833` publishes an incomplete scan after iterator failure; `semantic_source_controller.cpp:68` rejects it, while line 30 prevents restarting. **Trigger:** a directory disappears during iterator advancement. Rebuild remains ineffective and visible panes keep polling. **Fix:** expose terminal failure, stop needless polling, and permit safe retry. **Reported:** Claude M12. **Tracking:** new MegaCity card.

### F51 — Non-finite satellite settings survive normalization

**Summary:** Reject invalid numeric settings so the satellite view keeps usable lighting and ground coordinates.

`plugins/satview/src/runtime/satview_config_io.cpp:55` lets NaN survive ordinary clamps; line 235 turns infinite longitude into NaN through `remainder`. **Trigger:** `tone_map_exposure = nan` or `ground_longitude_radians = inf`. **Fix:** check finiteness before clamping/wrapping and normalize direct runtime configuration consistently. **Reported:** Claude M15. **Tracking:** new SatView card.

### F52 — Imported fractions narrow to invalid values

**Summary:** Reject unrepresentable timing values so imported scores retain valid musical durations.

`plugins/scoreview/product/draxul-notation/include/draxul/notation/score_document.h:42` narrows reduced wide integers without checking range. **Trigger:** `<divisions>1073741824</divisions>` with `<duration>1</duration>` produces denominator `4294967296`, narrowed to zero on current platforms. **Fix:** validate normalized numerator/denominator and reject unusable values at import. **Reported:** Claude M16, fraction portion. **Tracking:** new ScoreView card.

### F53 — Build-lock liveness reads the wrong Windows error state

**Summary:** Keep build ownership locks when Windows cannot inspect the owner so another build cannot enter the same folder.

`do.py:59` reads `ctypes.get_last_error()` after using `windll` without last-error tracking. **Trigger:** querying another account’s live process fails with access denied, but the private error copy does not contain that result; the lock is reclaimed. **Fix:** configure bindings and error retrieval correctly and retain ownership when liveness is unknown. [Python ctypes error handling](https://docs.python.org/3/library/ctypes.html#ctypes.get_last_error) **Reported:** Claude M17. **Tracking:** new core card.

### F54 — Zoomed printing crops the stored split rectangle

**Summary:** Print the displayed zoomed pane so visible content is not cut off.

`app/app.cpp:2026` captures stored split geometry while `app/pane_manager.cpp:1476` displays a separate full viewport. **Trigger:** print a zoomed pane. **Fix:** snapshot displayed viewport geometry and the matching content hint. **Reported:** Codex B28. **Tracking:** existing.

### F55 — Partial mesh allocation leaks its vertex buffer

**Summary:** Release partially created drawing buffers so repeated allocation failures do not retain unused graphics memory.

`plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_vk_resources.cpp:69` returns after index allocation fails without releasing successful vertex allocation. **Trigger:** foliage replacement repeatedly encounters that allocation sequence. **Fix:** scoped ownership until complete success or explicit partial cleanup. **Reported:** Codex B30. **Tracking:** existing.

### F56 — Post-acquisition frame failure leaves synchronization outstanding

**Summary:** Recover failed frame preparation so the next frame can safely acquire a drawing surface.

`libs/draxul-renderer/src/vulkan/vk_renderer.cpp:1101` returns after acquisition without retiring the image or recovering its signaled semaphore. **Trigger:** command-buffer begin or atlas staging fails after acquisition. **Fix:** route all post-acquisition failures through frame-abort synchronization recovery. **Reported:** Codex B31; Claude listed the same lead as unresolved. **Tracking:** existing.

### F57 — Failed Metal font uploads are discarded

**Summary:** Retry failed font-image transfers so temporary memory shortages do not leave text missing.

`libs/draxul-renderer/src/metal/metal_renderer.mm:1065` clears pending pixels after staging failure; mapping failure repeats this, and encoder creation is unchecked. **Trigger:** an upload fails after font dirty state has already been cleared. **Fix:** retain pixels for retry and guard dependent drawing. **Reported:** Codex B33. **Tracking:** existing.

### F58 — Pane limit rejects replacement without adding a pane

**Summary:** Allow replacement in a full tab so an existing terminal can still become an agent.

`libs/draxul-server/src/topology_service.cpp:682` checks pane count before replacement handling, which updates an existing pane. **Trigger:** replace a terminal in a valid 256-pane tab. **Fix:** apply the limit only to operations that add a pane. **Reported:** Codex B34. **Tracking:** existing.

## Reconciliation and rejected portions

No accepted review finding was rejected wholesale. These details or proposed explanations did not survive verification:

- **C3/C4:** MegaCity already checks for non-null backend data; ownership remains wrong. SatView’s null-backend scenario requires previously registered diagnostic textures, which are disabled by default.
- **M1:** ordinary rejected layout commands publish their failure and do not discard status. The defect requires transient or resynchronizing failure.
- **M12:** individual file-inspection errors are skipped. Terminal iterator/root-scan failure causes the permanent waiting state.
- **M13:** bare nested top-level arrays fail early. Nesting inside an object field reaches the recursive parser.
- **M14:** normal merged catalogs reject nonpositive identifiers before propagation. Bundled sample fallback supplies the confirmed `INT64_MIN` path. Out-of-range `llround(2^63)` alone is a domain/range error, not guaranteed C++ undefined behavior.
- **M15:** ordinary clamps bound infinities; NaN survives, and infinite longitude becomes NaN. Worker speed handling also converts NaN speed to zero in one path.
- **M8:** module retention prevents ordinary pane-close/reload unloading. The remaining race occurs at process static teardown.
- **M11:** taking the last comma-separated field would damage function names containing commas. Parse the numeric fields instead.
- **M16:** fractional narrowing and signed key arithmetic are separate defects. Do not claim all playback timing comes from the semantic fraction model; much uses Verovio timemaps.

Severity conflicts were resolved using the requested definitions:

| Finding | Review position | Ruling |
|---|---|---|
| Prompt-marker overflow | Codex CRITICAL; Claude MEDIUM | CRITICAL: signed overflow. |
| Practice restoration | Codex CRITICAL; Claude MEDIUM | CRITICAL: uncaught initialization exception terminates the app. |
| Redraw overflow | Codex HIGH; Claude MEDIUM | HIGH: permanent incorrect display state. |
| Drag release | Codex MEDIUM; Claude HIGH | HIGH: normal user input leaves persistent dragging. |
| Hook replacement | Claude HIGH | CRITICAL: destroys user settings. |
| Metal buffer growth | Codex MEDIUM | CRITICAL: out-of-range graphics-buffer reads. |
| Cache close failure | Codex MEDIUM | CRITICAL: overwrites saved data with incomplete bytes. |
| Microphone lifetime, reversed clamps, parser depth, unsafe identifier absolute value, key arithmetic | Claude MEDIUM | CRITICAL: substantiated crash or undefined-behavior paths. |
| Vulkan acquired-frame recovery | Codex accepted; Claude unresolved | Accepted MEDIUM: source provides explicit post-acquisition failure paths. |
| Neovim wide-cell protocol | Claude accepted; Codex unresolved | Accepted HIGH: official protocol resolves the missing evidence. |

## Already-tracked work

These actual pending cards cover all 34 Codex findings. They are not reproduced or revised here.

| Review finding | Existing card |
|---|---|
| B1 | `plugins/megacity/kanban/pending/11 biology-noise-overflow -bug.md` |
| B2 | `plugins/satview/kanban/pending/09 asset-root-worker-race -bug.md` |
| B3 | `kanban/pending/00 nvim-closed-pipe-signal -bug.md` |
| B4 / Claude M7 | `kanban/pending/02 terminal-marker-scroll-overflow -bug.md` |
| B5 | `kanban/pending/03 session-identifier-overflow -bug.md` |
| B6 / Claude M9 | `plugins/scoreview/kanban/pending/07 progress-restoration-validation -bug.md` |
| B7 | `plugins/rezonality/kanban/pending/09 build-temporary-storage-failure -bug.md` |
| B8 | `plugins/rezonality/kanban/pending/10 color-attachment-count-validation -bug.md` |
| B9 | `kanban/pending/60 print-temporary-directory-failure -bug.md` |
| B10 | `kanban/done/62 topology-durable-limits -bug.md` |
| B11 | `kanban/done/66 windows-stream-accept-failure -bug.md` |
| B12 | `kanban/done/69 agent-hook-executable-path -bug.md` |
| B13 | `kanban/done/70 narrow-cell-wide-neighbor -bug.md` |
| B14 | `kanban/done/71 cancellable-nvim-output -bug.md` |
| B15 / Claude M6 | `kanban/done/72 nvim-redraw-queue-recovery -bug.md` |
| B16 | `kanban/done/73 terminal-cross-feed-clusters -bug.md` |
| B17 | `kanban/pending/74 markdown-display-density -bug.md` |
| B18 / related Claude C6 | `kanban/pending/75 windows-markdown-source-paths -bug.md` |
| B19 | `kanban/done/76 windows-deploy-plugin-packages -bug.md` |
| B20 | `plugins/satview/kanban/pending/11 paused-controls-completion-ticks -bug.md` |
| B21 | `plugins/megacity/kanban/pending/12 label-upload-staging-lifetime -bug.md` |
| B22 | `plugins/scoreview/kanban/pending/08 device-failure-keyboard-fallback -bug.md` |
| B23 | `plugins/pcbview/kanban/pending/04 wide-track-pad-blockers -bug.md` |
| B24 | `plugins/pcbview/kanban/pending/05 continuous-track-pad-clearance -bug.md` |
| B25 | `plugins/rezonality/kanban/pending/11 shared-depth-pass-synchronization -bug.md` |
| B26 | `plugins/rezonality/kanban/done/12 unsupported-texture-feedback -bug.md` |
| B27 / Claude H1 | `kanban/pending/77 mouse-drag-release-owner -bug.md` |
| B28 | `kanban/pending/78 zoomed-pane-print-crop -bug.md` |
| B29 | `plugins/satview/kanban/pending/10 cache-final-close-validation -bug.md` |
| B30 | `plugins/megacity/kanban/pending/13 mesh-partial-allocation-cleanup -bug.md` |
| B31 | `kanban/pending/79 vulkan-acquired-frame-recovery -bug.md` |
| B32 | `kanban/done/61 metal-grid-allocation-failure -bug.md` |
| B33 | `kanban/pending/80 metal-atlas-upload-retry -bug.md` |
| B34 | `kanban/pending/81 agent-replacement-pane-limit -bug.md` |

Broad refactor/performance cards were not counted as bug coverage when their acceptance criteria preserve behavior or omit the demonstrated failure. In particular, checkpoint extraction, atomic-file extraction, interface wrapping, worker extraction, configuration ownership, and viewport-performance work do not explicitly own the new corrections.

## Dependencies and remaining leads

**Summary:** Coordinate related fixes so shared boundaries are corrected without duplicating work.

- Retired-server protection must cover detached checkpoint publication, and coordinate with `kanban/pending/09 server-checkpoint-state -refactor.md` and `kanban/pending/10 session-model-store -refactor.md`.
- Safe hook replacement should establish the contract used by `kanban/pending/11 atomic-file-replacement -refactor.md`.
- Shared texture ownership spans MegaCity and SatView and should align with `kanban/pending/26 plugin-imgui-vulkan-api -refactor.md`.
- Neovim wide-cell decoding and existing neighboring-wide-cell work must preserve the same continuation-cell contract. Closed-pipe handling and cancellable writes must preserve child signal behavior.
- Zoomed input and zoomed printing share displayed-pane geometry.
- Satellite parser depth and identifier validation share catalog entry points; non-finite settings share normalization policy.
- The two MusicXML fixes share importer validation but protect separate consumers.
- Microphone lifetime work must preserve nonblocking shutdown.
- PCB blocker extent and continuous clearance need separate regressions; fixing one does not establish the other.

Other supplied leads remain unresolved and receive no cards:

| Area | Leads requiring further evidence |
|---|---|
| Core rendering/fonts | Upload-only frames presenting uninitialized images; partial swapchain-resource recreation; double NanoVG flush; oversized color-glyph reservation; blend-alpha differences; capture format assumptions. The negative-origin scissor concern also requires checking the current shared clamp. |
| Client/server/control | Compatibility worker command loss; ConPTY drainage during close; companion-owner removal; non-finite saved split restoration; mistyped control/metadata fields; PTY/grid resize disagreement; WinHTTP handle lifetime; stale subscribers; permanent stream-to-poll downgrade. |
| Terminal/Neovim/input | Bracketed-paste escape injection; multi-character text containing `<`; interrupted control-sequence parsing; colon subparameters; cursor movement outside margins; unanswered request after callback exceptions; `+` bindings; delayed tab input targeting; failed local terminal resize. |
| Product workers | Snapshot-exchange ordering; zero-span sampled ephemeris velocity; fetched-time overflow; music callback teardown; shared engraver state; repeated timemap identifiers; startup exceptions; other worker exception paths; cross-process compiler-directory collision; overlapping Metal audio-texture updates. |
| Tooling | Strict UTF-8 subprocess-output decoding. |

The supplied rejected part-ID escaping claim remains unsupported for valid MusicXML names. No new task is proposed for it.

Static verification establishes the cited branches and admissible interleavings, but does not measure failure frequency, stack thresholds, shutdown timing, graphics-driver outcomes, or speedups. Windows and macOS runtime validation remains outstanding.

## Proposed work cards

These are complete proposed contents for the trusted parent to create. No card files were created.

### kanban/done/82 configured-glyph-atlas-dimensions -bug.md

# Keep configured font-image dimensions consistent
**Summary:** Use matching font-image dimensions so changing the size setting cannot break text or write outside drawing storage.

**Priority:** 82  
**Severity:** CRITICAL  
**Source:** `libs/draxul-font/src/glyph_atlas_manager.h`  
**Reported by:** Claude C1; consensus F01.

**Evidence and trigger:** The renderer receives the configured size at `app/app.cpp:483`, while cache initialization at source line 27 retains 2048. Configuring 1024 causes an oversized upload; larger sizes produce incorrect sampling coordinates.

- [ ] **Investigate:** Trace dimensions through application initialization, font resets, configuration reload, and both renderer upload paths.
- [ ] **Fix:** Propagate one supported dimension through font/cache and renderer setup; handle reload consistently.
- [ ] **Fix:** Reject upload rectangles outside native texture bounds before recording.
- [ ] **Acceptance:** Supported non-default sizes render correct text on both backends without oversized copies; invalid uploads fail safely.
- [ ] **Validation:** Run core aggregate tests, affected text-render checks, and same-cache smoke; check Vulkan validation and Metal bounds handling.

### kanban/pending/83 retired-server-checkpoint-ownership -bug.md

# Prevent retired servers from publishing checkpoints
**Summary:** Stop retired servers from replacing newer saved layouts so restarting preserves the replacement server’s changes.

**Priority:** 83  
**Severity:** CRITICAL  
**Source:** `libs/draxul-server/src/server_kernel_lifecycle.cpp`  
**Reported by:** Claude C2; consensus F02.

**Evidence and trigger:** Eviction exits at line 414 but continues into queued requests and checkpoint scheduling at lines 651 and 664. Detached writers retain shared destinations. A successor checkpoint can be overwritten by stale state.

**Related:** `kanban/pending/09 server-checkpoint-state -refactor.md`; `kanban/pending/10 session-model-store -refactor.md`.

- [ ] **Investigate:** Trace endpoint ownership, eviction, final saves, already-running writers, and temporary-file publication.
- [ ] **Fix:** Distinguish eviction from normal shutdown and suppress post-eviction mutations and saves.
- [ ] **Fix:** Prevent outstanding retired-owner writes from publishing over successor state.
- [ ] **Acceptance:** A successor’s saved layout survives old-server retirement, including a controlled in-flight old writer.
- [ ] **Acceptance:** Ordinary graceful shutdown still saves the latest accepted state.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; exercise ownership replacement on supported platform paths.

### kanban/pending/84 nondestructive-agent-hook-replacement -bug.md

# Preserve settings when hook replacement fails
**Summary:** Preserve existing settings when replacement fails so installing conversation hooks cannot erase them.

**Priority:** 84  
**Severity:** CRITICAL  
**Source:** `libs/draxul-agent-integration/src/agent_integration.cpp`  
**Reported by:** Claude H5; consensus F03.

**Evidence and trigger:** Lines 194 and 202 delete the original and staged files after two rename failures. Windows file sharing can prevent replacement while allowing original deletion.

**Related:** `kanban/pending/11 atomic-file-replacement -refactor.md`.

- [ ] **Investigate:** Trace every hook/settings caller and the replacement, cleanup, permission, and recovery contracts.
- [ ] **Fix:** Replace files without deleting the destination as a fallback.
- [ ] **Fix:** Preserve recoverable staged contents when publication fails.
- [ ] **Acceptance:** Injected replacement failures preserve original bytes and provide a useful error; successful installation remains functional.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; exercise Windows sharing failures and POSIX replacement behavior.

### kanban/done/85 plugin-texture-release-ownership -bug.md

# Release diagnostic textures through their owning pane
**Summary:** Release each pane’s diagnostic images through their owner so closing one pane cannot corrupt another pane’s drawing state.

**Priority:** 85  
**Severity:** CRITICAL  
**Source:** `plugins/support/imgui/src/plugin_imgui_context.cpp`  
**Reported by:** Claude C3 and C4; consensus F04.

**Evidence and trigger:** MegaCity and SatView destroy scene passes before selecting their owning context. Shared Vulkan removal uses the current context’s descriptor pool. SatView additionally permits null-backend removal after another hidden pane creates a context.

**Related:** `kanban/pending/26 plugin-imgui-vulkan-api -refactor.md`; `plugins/satview/kanban/pending/08 satview-resize-target-lifetime -perf.md`.

- [ ] **Investigate:** Trace texture registration/removal ownership, context changes, scene teardown, resize retirement, and lazy backend initialization.
- [ ] **Fix:** Provide scoped owner-context handling or explicit owned texture removal and apply it to both product callers.
- [ ] **Fix:** Remove textures before their owning backend is destroyed and handle absent backend data safely.
- [ ] **Acceptance:** Closing either of two rendered product panes removes descriptors through the correct pool.
- [ ] **Acceptance:** Closing a diagnostic-enabled SatView pane while an unrendered pane’s context is current cannot dereference a null backend.
- [ ] **Validation:** Run core/product aggregates appropriate to this shared seam, Vulkan validation, relevant multi-pane checks, and same-cache smoke; inspect Metal teardown alignment.

### kanban/pending/86 kanban-international-filename-conversion -bug.md

# Preserve international Kanban names safely
**Summary:** Preserve card names in their original characters so opening a board cannot crash on international names.

**Priority:** 86  
**Severity:** CRITICAL  
**Source:** `modules/kanban/draxul-kanban/src/kanban_store.cpp`  
**Reported by:** Claude C6; consensus F06.

**Evidence and trigger:** Lines 578, 616, and 658 use throwing narrow filename conversions. On Windows, scanning an emoji or Asian card name outside the active code page can escape board loading.

**Related:** `kanban/pending/75 windows-markdown-source-paths -bug.md` owns the related Markdown path boundary.

- [ ] **Investigate:** Trace repository, lane, card, title, preview, and error-text conversions while preserving native paths.
- [ ] **Fix:** Convert displayed names explicitly to UTF-8 and contain scan/conversion failures at the board boundary.
- [ ] **Acceptance:** International repository/lane/card names load, display, reload, and select correctly on Windows with a non-UTF-8 code page.
- [ ] **Acceptance:** A failed scan leaves usable state and reports an error without terminating the application.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; verify Windows filename behavior and existing POSIX behavior.

### kanban/done/87 zoomed-pane-input-hit-testing -bug.md

# Route zoomed input to the visible pane
**Summary:** Keep mouse input on the visible zoomed pane so typing cannot move to a hidden pane.

**Priority:** 87  
**Severity:** HIGH  
**Source:** `app/pane_manager.cpp`  
**Reported by:** Claude H2; consensus F23.

**Evidence and trigger:** Lines 1193–1197 hit-test normal split rectangles and update focus while zoomed. Mouse movement can select a hidden leaf; hidden divider hits also capture input.

**Related:** `kanban/pending/78 zoomed-pane-print-crop -bug.md`.

- [ ] **Investigate:** Trace zoomed leaf identity, mouse movement/buttons/wheels, focus, and divider hit-testing.
- [ ] **Fix:** Resolve zoomed input against displayed geometry and suppress hidden dividers.
- [ ] **Acceptance:** Moving or clicking across a zoomed pane never focuses hidden panes or captures hidden dividers.
- [ ] **Acceptance:** Unzooming restores normal split hit-testing and intended focus.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; check zoomed input interactively.

### kanban/done/88 nvim-wide-cell-protocol-decoding -bug.md

# Decode wide editor cells without shifting the line
**Summary:** Preserve the editor’s cell positions so wide characters do not shift the rest of a line.

**Priority:** 88  
**Severity:** HIGH  
**Source:** `libs/draxul-nvim/src/ui_events.cpp`  
**Reported by:** Claude H3; consensus F24.

**Evidence and trigger:** Lines 294–296 advance two columns for a wide leader before its explicit empty continuation cell arrives. The official line-grid protocol sends that follower.

**Related:** `kanban/done/70 narrow-cell-wide-neighbor -bug.md`.

- [ ] **Investigate:** Trace protocol cells, repeats, partial updates, continuation handling, and grid cleanup.
- [ ] **Fix:** Advance once per protocol cell and handle empty wide followers without erasing their leader.
- [ ] **Acceptance:** Protocol-shaped wide-character packets preserve following text, highlights, cursor alignment, and end-of-line content.
- [ ] **Acceptance:** Updates beginning at a continuation cell and wide-to-narrow replacements retain valid grid state.
- [ ] **Validation:** Correct fixtures that omit explicit followers; run core aggregate tests, Unicode rendering checks, and same-cache smoke.

### kanban/pending/89 status-completion-during-command-retry -bug.md

# Preserve status requests during layout retries
**Summary:** Complete status requests even when a simultaneous layout request encounters a connection failure.

**Priority:** 89  
**Severity:** MEDIUM  
**Source:** `libs/draxul-client/src/remote_session_client.cpp`  
**Reported by:** Claude M1; consensus F44.

**Evidence and trigger:** Status is dequeued at line 886, then retry continuation at line 902 skips publication. This occurs for transient/resynchronizing command failures, not ordinary invalid commands.

- [ ] **Investigate:** Trace status ownership, command retry, recovery, stop confirmation, and shutdown.
- [ ] **Fix:** Process status independently or retain it before command retry continuation.
- [ ] **Acceptance:** A status/stop request queued beside a transiently failing command receives exactly one completion or explicit cancellation.
- [ ] **Acceptance:** Command retries and ordinary validation failures retain their existing completion behavior.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke with controlled retry failures.

### kanban/pending/90 server-state-loop-wake-race -bug.md

# Make server wake notifications reliable
**Summary:** Make wake-up signals reliable so pending work does not wait for the idle timeout.

**Priority:** 90  
**Severity:** MEDIUM  
**Source:** `libs/draxul-server/src/server_kernel_lifecycle.cpp`  
**Reported by:** Claude M2; consensus F45.

**Evidence and trigger:** Notifiers at lines 317, 337, and 716 do not coordinate with the wait mutex at line 643. A notification between predicate evaluation and blocking is lost; terminal output only notifies on its first pending transition.

- [ ] **Investigate:** Trace every control, stream, terminal-output, and stop notifier and the loop predicate.
- [ ] **Fix:** Coordinate flag publication/notification with the waiter mutex while preserving coalescing and teardown safety.
- [ ] **Acceptance:** Controlled publication at the predicate-to-wait boundary wakes the loop without waiting for the idle timeout.
- [ ] **Acceptance:** Stop and terminal-output bursts remain reliable without introducing busy polling.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; retain concurrency coverage.

### kanban/pending/91 session-poll-pending-wake-handling -bug.md

# Honor pending wakes in the session poll worker
**Summary:** Remember background wake requests so input and shutdown are not delayed during recovery.

**Priority:** 91  
**Severity:** MEDIUM  
**Source:** `libs/draxul-client/src/remote_session_coordinator.cpp`  
**Reported by:** Claude M3; consensus F46.

**Evidence and trigger:** Lines 2504 and 2559 wait without a predicate despite remembered pending state. A wake arriving before either wait can be lost, including during five-second recovery backoff.

- [ ] **Investigate:** Trace poll success/failure waits, pending state, input wake, stopping, and joining.
- [ ] **Fix:** Use the existing predicate-based worker wait helper and consume pending state consistently.
- [ ] **Acceptance:** A wake published before either wait is honored; stopping interrupts recovery backoff promptly.
- [ ] **Acceptance:** Healthy idle polling retains intended pacing without spinning.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke with controlled pre-wait input/stop interleavings.

### kanban/pending/92 windows-control-listener-disconnect-reset -bug.md

# Reset failed retained Windows listeners
**Summary:** Reset failed listeners so a quick disconnect cannot leave one retrying forever.

**Priority:** 92  
**Severity:** MEDIUM  
**Source:** `libs/draxul-control/src/control_transport_win32.cpp`  
**Reported by:** Claude M4; consensus F47.

**Evidence and trigger:** Synchronous errors at line 499 fall through without disconnecting the retained first pipe at line 583. Early disconnect can leave repeated `ERROR_NO_DATA`.

**Related:** `kanban/done/66 windows-stream-accept-failure -bug.md` owns a separate stream-listener failure.

- [ ] **Investigate:** Trace retained/additional listener states, synchronous errors, pending completion, and cancellation.
- [ ] **Fix:** Disconnect/reset failed retained instances and apply useful error reporting and bounded retry pacing.
- [ ] **Acceptance:** Early disconnect does not leave a tight retry loop or permanently consume a listener slot.
- [ ] **Acceptance:** Subsequent clients connect and listener shutdown completes.
- [ ] **Validation:** Run Windows control/server coverage, core aggregate tests, and same-cache smoke.

### kanban/pending/93 shortcut-chord-prefix-identity -bug.md

# Match shortcut chords to their starting prefix
**Summary:** Remember which shortcut prefix was pressed so the second key runs the intended command.

**Priority:** 93  
**Severity:** MEDIUM  
**Source:** `app/input_dispatcher.cpp`  
**Reported by:** Claude M5; consensus F48.

**Evidence and trigger:** Prefix activation at line 366 records no key/modifier identity; line 332 matches only the second key. Two prefixes sharing the same second key can execute the wrong binding.

- [ ] **Investigate:** Trace prefix activation, normalization, timeout, cancellation, pane selection, and configuration reload.
- [ ] **Fix:** Retain active prefix identity and require it to match during chord lookup.
- [ ] **Acceptance:** `Ctrl+S, z` and `Ctrl+B, z` execute their respective commands regardless of binding order.
- [ ] **Acceptance:** Timeout, cancellation, and unmatched chords preserve ordinary input behavior.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.

### kanban/pending/94 windows-build-lock-liveness-errors -bug.md

# Preserve build locks when process inspection is denied
**Summary:** Keep build ownership locks when Windows cannot inspect the owner so another build cannot enter the same folder.

**Priority:** 94  
**Severity:** MEDIUM  
**Source:** `do.py`  
**Reported by:** Claude M17; consensus F53.

**Evidence and trigger:** Lines 54–59 use unconfigured `windll` and read ctypes’ private last-error copy. Access denied can be mistaken for a dead owner, allowing lock removal at line 137.

- [ ] **Investigate:** Trace native error retrieval, handle types, liveness results, and stale-lock reclamation.
- [ ] **Fix:** Configure Windows bindings correctly and treat indeterminate liveness conservatively.
- [ ] **Acceptance:** Access denied for a live owner preserves the lock; a confirmed dead owner remains reclaimable.
- [ ] **Acceptance:** Native handles are closed correctly and normal build ownership behavior remains intact.
- [ ] **Validation:** Run relevant workflow coverage, core aggregate tests, and same-cache smoke; verify Windows error outcomes through controlled bindings.

### plugins/megacity/kanban/pending/14 route-grid-generation-ownership -bug.md

# Keep route results attached to the current city
**Summary:** Keep routes tied to their city so rebuilding cannot restore an older grid.

**Priority:** 14  
**Severity:** HIGH  
**Source:** `plugins/megacity/product/draxul-megacity/src/megacity_host.cpp`  
**Reported by:** Claude H4; consensus F25.

**Evidence and trigger:** A rebuild preserves the old grid while starting a new one. Route scheduling at line 1549 can use that old grid with new layout state, and line 744 adopts its result unconditionally.

**Related:** `plugins/megacity/kanban/pending/01 megacity-worker-ownership -refactor.md`.

- [ ] **Investigate:** Trace layout/grid generations, selected-building state, route inputs, and both completion orders.
- [ ] **Fix:** Bind route requests/results to current grid identity and generation; reject replaced-grid results.
- [ ] **Fix:** Request routes only after the matching grid is ready and rearm selection routing when it changes.
- [ ] **Acceptance:** Rebuilding a selected city cannot restore an older grid or leave routes missing because an old request was marked complete.
- [ ] **Validation:** Run the MegaCity-scoped aggregate, relevant route/view checks, and same-cache smoke.

### plugins/megacity/kanban/pending/15 current-coverage-function-records -bug.md

# Parse coverage function records correctly
**Summary:** Read coverage records correctly so executed functions are not silently shown as uncovered.

**Priority:** 15  
**Severity:** MEDIUM  
**Source:** `plugins/megacity/product/draxul-megacity/src/lcov_coverage.cpp`  
**Reported by:** Claude M11; consensus F49.

**Evidence and trigger:** Line 107 includes an optional end-line number in the function name; indexed `FNL`/`FNA` records are ignored. Both forms are documented coverage inputs.

- [ ] **Investigate:** Trace function names/counts from tracefile import into model matching and overlay presentation.
- [ ] **Fix:** Parse optional numeric fields and indexed leaders/aliases while preserving complete function names.
- [ ] **Acceptance:** Documented records preserve function names and execution counts, including names containing commas.
- [ ] **Acceptance:** Unsupported or malformed records produce controlled behavior without silently misrepresenting accepted records.
- [ ] **Validation:** Run the MegaCity-scoped aggregate, coverage overlay checks, and same-cache smoke.

### plugins/megacity/kanban/pending/16 failed-source-scan-recovery -bug.md

# Make failed source scans recoverable
**Summary:** Recover from a failed source scan so a temporary folder error cannot leave the view permanently waiting.

**Priority:** 16  
**Severity:** MEDIUM  
**Source:** `plugins/megacity/product/draxul-megacity/src/semantic_source_controller.cpp`  
**Reported by:** Claude M12; consensus F50.

**Evidence and trigger:** Iterator failure publishes incomplete state at `treesitter.cpp:833`. Controller line 68 rejects it indefinitely, while line 30 prevents restart. Rebuild cannot retry and visible panes continue periodic wakes.

- [ ] **Investigate:** Distinguish active scanning, completed failure, cancellation, and successful publication.
- [ ] **Fix:** Expose terminal failure, report it, and allow safe reset/retry through Rebuild.
- [ ] **Fix:** Stop periodic scan polling once work has ended unsuccessfully.
- [ ] **Acceptance:** A controlled iterator failure leaves useful diagnostics; repairing the folder and rebuilding completes successfully.
- [ ] **Acceptance:** Failed or canceled partial snapshots are not presented as complete source models.
- [ ] **Validation:** Run the MegaCity-scoped aggregate and same-cache smoke; verify retry and settled scheduling.

### plugins/satview/kanban/pending/12 catalog-parser-nesting-limit -bug.md

# Bound catalog parsing depth
**Summary:** Reject excessively nested catalog data so a malformed download or saved file cannot exhaust the application’s stack.

**Priority:** 12  
**Severity:** CRITICAL  
**Source:** `plugins/satview/src/core/satview_catalog.cpp`  
**Reported by:** Claude M13; consensus F21.

**Evidence and trigger:** Lines 274, 296, and 314 recursively skip object/array values without a depth cap. Deep nesting inside an object field reaches this path before record validation.

- [ ] **Investigate:** Trace network, startup-cache, and bundled-file parser entry points and failure publication.
- [ ] **Fix:** Enforce a bounded nesting depth for both arrays and objects and return useful diagnostics.
- [ ] **Acceptance:** Excessive nesting fails safely without stack exhaustion; permitted boundary depth parses normally.
- [ ] **Acceptance:** Failed refresh retains usable catalog state and startup failure follows the intended fallback.
- [ ] **Validation:** Run the SatView-scoped aggregate and same-cache smoke.

### plugins/satview/kanban/pending/13 catalog-identifier-domain-validation -bug.md

# Validate satellite identifiers before arithmetic
**Summary:** Reject invalid satellite identifiers before conversion so unusual catalog data cannot cause invalid arithmetic.

**Priority:** 13  
**Severity:** CRITICAL  
**Source:** `plugins/satview/src/core/satview_catalog.cpp`  
**Reported by:** Claude M14; consensus F22.

**Evidence and trigger:** Line 560 admits the rounded upper endpoint; propagation calls `llabs` at `satview_propagation.cpp:264`. A tampered bundled sample can supply `INT64_MIN` because startup fallback bypasses merge validation.

- [ ] **Investigate:** Trace exact identifier parsing and validation through network, cache, sample fallback, and direct model compilation.
- [ ] **Fix:** Enforce positive representable identifiers before conversion; avoid admitting the floating upper endpoint.
- [ ] **Fix:** Make propagation arithmetic safe for invalid direct inputs independently of parser validation.
- [ ] **Acceptance:** Minimum signed values, the exclusive upper endpoint, zero, and unsupported identifiers fail safely at every applicable boundary.
- [ ] **Acceptance:** Valid catalog identifiers retain their identity and compile normally.
- [ ] **Validation:** Run the SatView-scoped aggregate and same-cache smoke; use undefined-behavior instrumentation where available.

### plugins/satview/kanban/pending/14 nonfinite-settings-validation -bug.md

# Reject non-finite satellite settings
**Summary:** Reject invalid numeric settings so the satellite view keeps usable lighting and ground coordinates.

**Priority:** 14  
**Severity:** MEDIUM  
**Source:** `plugins/satview/src/runtime/satview_config_io.cpp`  
**Reported by:** Claude M15; consensus F51.

**Evidence and trigger:** NaN survives the clamp at line 55; infinite longitude becomes NaN at line 235. Lighting and ground calculations receive these invalid values.

**Related:** `plugins/satview/kanban/pending/02 satview-configuration-ownership -refactor.md`.

- [ ] **Investigate:** Trace saved/launch configuration and direct runtime application into lighting, projection, and simulation controls.
- [ ] **Fix:** Reject non-finite values before clamping or wrapping and retain valid defaults or previous state.
- [ ] **Acceptance:** NaN and infinities cannot reach affected rendering/ground calculations through configuration.
- [ ] **Acceptance:** Finite boundary settings retain documented clamping and wrapping behavior.
- [ ] **Validation:** Run the SatView-scoped aggregate, affected view checks, and same-cache smoke.

### plugins/scoreview/kanban/pending/09 short-pane-layout-bounds -bug.md

# Keep score layout bounds valid in short panes
**Summary:** Fit the score within short panes so resizing cannot trigger an invalid layout operation.

**Priority:** 09  
**Severity:** CRITICAL  
**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp`  
**Reported by:** Claude M10; consensus F19.

**Evidence and trigger:** Line 402 and `score_presentation.cpp:152` reverse clamp bounds below approximately 107 pixels times display scale. A short loaded Flow/Roll pane reaches both paths.

- [ ] **Investigate:** Trace band layout across short/zero viewports, scale, zoom, and supported score modes.
- [ ] **Fix:** Derive ordered bounds from available space or explicitly handle insufficient space in both calculations.
- [ ] **Acceptance:** Short panes produce defined geometry without assertions, reversed clamps, or drawing outside intended bounds.
- [ ] **Acceptance:** Normal pane sizing, score bands, and keyboard/waterfall layout remain correct.
- [ ] **Validation:** Run the ScoreView-scoped aggregate, affected render checks, and same-cache smoke.

### plugins/scoreview/kanban/pending/10 microphone-worker-module-lifetime -bug.md

# Keep microphone workers alive with their module
**Summary:** Finish microphone-opening work before releasing its code so quitting during a permission prompt cannot crash.

**Priority:** 10  
**Severity:** CRITICAL  
**Source:** `plugins/scoreview/product/draxul-scoreview/src/mic_player_input.cpp`  
**Reported by:** Claude M8; consensus F18.

**Evidence and trigger:** The worker detaches at line 217; destruction marks abandonment without completion ordering. Process static teardown can release the module while its permission worker sleeps.

- [ ] **Investigate:** Trace worker/code ownership through pending permission, device opening/resume, pane shutdown, and process module release.
- [ ] **Fix:** Track outstanding openers and order completion before module release, using cancellable waits or retained module ownership.
- [ ] **Fix:** Preserve asynchronous consent/device handling and avoid unbounded interface-thread joins.
- [ ] **Acceptance:** Controlled shutdown during pending permission cannot resume into unloaded module code.
- [ ] **Acceptance:** Abandoned opening/resuming operations clean up exactly once and ordinary input remains functional.
- [ ] **Validation:** Run the ScoreView-scoped aggregate and same-cache smoke; verify the macOS permission/shutdown path and inspect Windows lifetime behavior.

### plugins/scoreview/kanban/pending/11 imported-key-signature-overflow -bug.md

# Validate imported key-signature arithmetic
**Summary:** Validate extreme key signatures so imported scores cannot cause invalid musical calculations.

**Priority:** 11  
**Severity:** CRITICAL  
**Source:** `plugins/scoreview/product/draxul-score-learn/src/piece_analysis.cpp`  
**Reported by:** Claude M16; consensus F20.

**Evidence and trigger:** Source line 63 multiplies fifths by seven after the importer accepts unrestricted representable integers. `<fifths>2147483647</fifths>` overflows when analyzed.

- [ ] **Investigate:** Trace imported signatures through paged and conveyor analysis and define supported input policy.
- [ ] **Fix:** Reject unsupported imported signatures and independently use safe pitch-class normalization.
- [ ] **Acceptance:** Extreme positive/negative signatures cannot overflow; accepted ordinary signatures retain their expected analysis prior.
- [ ] **Acceptance:** Rejected import values produce controlled diagnostics rather than invalid model state.
- [ ] **Validation:** Run the ScoreView-scoped aggregate and same-cache smoke; use arithmetic instrumentation where available.

### plugins/scoreview/kanban/pending/12 imported-fraction-range-validation -bug.md

# Preserve valid imported musical fractions
**Summary:** Reject unrepresentable timing values so imported scores retain valid musical durations.

**Priority:** 12  
**Severity:** MEDIUM  
**Source:** `plugins/scoreview/product/draxul-notation/include/draxul/notation/score_document.h`  
**Reported by:** Claude M16; consensus F52.

**Evidence and trigger:** Line 42 narrows normalized wide integers without checks. Divisions `1073741824` and duration `1` produce a denominator that narrows to zero.

- [ ] **Investigate:** Trace duration/division parsing, fraction normalization, arithmetic, and import error propagation.
- [ ] **Fix:** Check normalized numerator/denominator ranges before narrowing and prevent publication of invalid fractions.
- [ ] **Fix:** Validate duration conversion inputs before rounding and report unusable imported timing coherently.
- [ ] **Acceptance:** The reported input cannot create a zero/negative denominator or infinite semantic duration; valid representable timing remains exact.
- [ ] **Validation:** Run the ScoreView-scoped aggregate and same-cache smoke.

### plugins/rezonality/kanban/pending/13 international-project-path-serialization -bug.md

# Serialize international project paths safely
**Summary:** Preserve international project names so opening or reloading a graphics project cannot close the application.

**Priority:** 13  
**Severity:** CRITICAL  
**Source:** `plugins/rezonality/src/rezonality_plugin.cpp`  
**Reported by:** Claude C5; consensus F05.

**Evidence and trigger:** Lines 500 and 552 perform narrow path conversions; reload JSON dumping can throw through exported callbacks. Diagnostic identity construction also converts paths without containment. Windows non-UTF-8 code pages expose the failures.

- [ ] **Investigate:** Trace path creation, diagnostic identity, presentation, and reload export/import boundaries.
- [ ] **Fix:** Serialize native paths explicitly as UTF-8 while retaining native filesystem paths internally.
- [ ] **Fix:** Contain failures in affected exported callbacks and preserve the active project on reload failure.
- [ ] **Acceptance:** Projects under accented, Asian, and emoji paths open and reload safely on Windows with a non-UTF-8 code page.
- [ ] **Acceptance:** Presentation and diagnostics remain valid text; existing publication error handling remains effective.
- [ ] **Validation:** Run the Rezonality-scoped aggregate, affected reload checks, and same-cache smoke; preserve macOS path behavior.

<model>gpt-6.1-sol</model>
