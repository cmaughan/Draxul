# Bug-triage consensus

All **34 reported defects are confirmed against current source**: **11 CRITICAL, 17 HIGH, and 6 MEDIUM**. No exact duplicate work items were found in the available root or product tracker lanes. The complete proposed cards appear below; no files were created or changed.

## Inputs, architecture, and coverage

Both reports were read completely and matched the hashes in `INPUT_REVIEWS/index.md`.

| Indexed report | Actual coverage |
|---|---|
| Claude Opus 5.5 | Only a progress announcement. It contains no findings or completed review evidence. |
| Codex GPT-6.1 Sol | A partial static review covering application orchestration, server/session state, terminal and Neovim handling, rendering, built-in modules, all five products, deployment, and trackers. It reports 34 defects and six groups of unresolved leads. |

Consequently, this is a source-verified synthesis of the Codex findings, not agreement between two completed company reviews.

The relevant boundaries are:

- `app/` coordinates input, pane viewports, printing, and display changes.
- Server topology and durable session validation span `draxul-server`, `draxul-protocol`, and `draxul-session-model`.
- Server-owned terminals use the shared terminal parser and grid; client-owned Neovim uses its own process transport and incremental redraw queue.
- Vulkan and Metal renderers implement shared presentation contracts.
- Product modules cross the native plugin boundary but own their internal workers, data, and rendering resources. Product-only defects therefore belong in their product trackers.

Four read-only verification workers used the same model and high reasoning effort, with no nested delegation. All completed, and their supporting source was reread before acceptance. No builds, tests, installations, formatting, or project binaries were run.

## Confirmed defects, ordered by severity

Every finding below was reported by **Codex GPT-6.1 Sol**. The original report number is retained as its identifier.

### CRITICAL

**Summary:** Use safe arithmetic when generating biological shapes so opening the biology view cannot cause invalid calculations.

**B01 — CRITICAL.** `plugins/megacity/product/draxul-geometry/src/cell_generator.cpp:35` multiplies signed coordinates before converting them to unsigned values. Ordinary biology construction reaches overflowing coordinates through octave expansion.

**Trigger:** Open biology mode using its default blob settings. **Fix:** Convert coordinates before multiplying by unsigned constants on all three axes. **Reported by:** Codex #1.

**Summary:** Protect the shared asset location so opening another satellite pane cannot interfere with background file loading.

**B02 — CRITICAL.** `plugins/satview/src/core/satview_texture_assets.cpp:96` modifies a global filesystem path while catalog workers read it at lines 88–89.

**Trigger:** Create another satellite pane while an existing catalog worker loads bundled lunar data. **Fix:** Give services immutable asset-root copies, or synchronize every read and write. **Reported by:** Codex #2.

**Summary:** Handle a closed editor connection safely so an input operation cannot terminate the application.

**B03 — CRITICAL.** `libs/draxul-nvim/src/nvim_process.cpp:566` writes to a POSIX pipe without suppressing `SIGPIPE`; default signal handling terminates the client before error handling executes.

**Trigger:** Neovim closes its input pipe just before a key, paste, resize, or shutdown write. Terminal-process suppression occurs in the separate server path; test-runner suppression masks this client failure. **Fix:** Suppress the signal for parent-side writes while preserving normal child signal handling. **Reported by:** Codex #3.

**Summary:** Limit scrolling before moving saved prompt markers so extreme output cannot cause invalid arithmetic.

**B04 — CRITICAL.** `libs/draxul-terminal-core/src/terminal_core.cpp:533` subtracts an unclamped scroll displacement from a signed marker row before the grid clamps it.

**Trigger:** `ESC[2;1H ESC]133;A BEL ESC[2147483647T` computes `1 - (-2147483647)`. **Fix:** Clamp displacement to the scrolling region before updating marks and grid. **Reported by:** Codex #4.

**Summary:** Reject saved numbers that cannot support the next item safely so restoring a session cannot overflow its counters.

**B05 — CRITICAL.** `libs/draxul-server/src/session_topology_bridge.cpp:224` increments saved identifiers accepted at `INT_MAX`; line 230 has the same defect. Standalone restoration also increments unsafely at `app/space_controller.cpp:286` and `app/tab_controller.cpp:420`.

**Trigger:** Restore an otherwise-valid session containing a maximum signed Space or tab identifier. **Fix:** Validate identifier and allocator ranges and use checked increments throughout restoration and capture. **Reported by:** Codex #5.

**Summary:** Reject unusable saved practice data so opening a piece can start safely.

**B06 — CRITICAL.** `plugins/scoreview/product/draxul-score-learn/src/player_model.cpp:512` performs unchecked typed extraction after syntax-only validation; numeric-key conversions at lines 537 and 556 also throw.

**Trigger:** Load progress containing `{"total_notes":"bad"}` or invalid numeric map keys. Exceptions escape plugin initialization, and state has already been cleared. **Fix:** Validate into temporary state, contain conversion failures, and return failure without publishing partial state. **Reported by:** Codex #6.

**Summary:** Report unavailable temporary storage so a failed shader edit leaves the application running.

**B07 — CRITICAL.** `plugins/rezonality/src/live_project.cpp:1494` uses throwing temporary-directory lookup outside the candidate-build handler; the worker’s call at line 1533 has no surrounding catch.

**Trigger:** Remove or invalidate configured temporary storage, then reload a valid scene. **Fix:** Use error-code lookup and contain the complete worker build operation, publishing diagnostics while retaining the active scene. **Reported by:** Codex #7.

**Summary:** Reject excessive color outputs so an invalid scene cannot crash the graphics backend.

**B08 — CRITICAL.** `plugins/rezonality/src/native_backend_metal.mm:661` indexes fixed Metal color-attachment slots using an unchecked target count.

**Trigger:** Prepare a valid raster scene targeting nine color surfaces. The parser accepts them, and the ninth native descriptor access exceeds Metal’s eight slots. Vulkan also lacks device-limit validation. **Fix:** Validate attachment counts before native preparation against each backend’s limits. **Reported by:** Codex #8.

**Summary:** Show a printing error when temporary storage is unavailable so a failed print cannot close the application.

**B09 — CRITICAL.** `app/app.cpp:2064` calls throwing temporary-directory lookup before print-error handling; no enclosing application handler contains it.

**Trigger:** Print a successfully captured pane after configured temporary storage becomes unavailable. **Fix:** Use error-code lookup and report the failure through the existing print notification. **Reported by:** Codex #9.

**Summary:** Check that cache files finish writing before replacing the saved copy so storage failures cannot publish incomplete data.

**B29 — CRITICAL.** `plugins/satview/src/services/satview_cache_publication.cpp:75` checks the stream before its final close, then replaces the destination without observing close failure. `satview_cloud_service.cpp:75` repeats the defect.

**Trigger:** Buffered writes fail during final flush or close because storage is full or quota is exhausted. A truncated catalog can replace the valid cache; complete prefix rows can still parse and appear fresh. **Fix:** Explicitly close and check the stream before replacement, preserving the destination and cleaning the private temporary file on failure. **Reported by:** Codex #29.

**Summary:** Skip drawing when larger drawing memory cannot be created so a resize cannot read beyond the available storage.

**B32 — CRITICAL.** `libs/draxul-renderer/src/metal/metal_renderer.mm:121` returns after failed buffer growth while retaining the smaller allocation; drawing at lines 882–943 continues with new instance counts. `shaders/grid.metal:53` and `:124` directly index those instances.

**Trigger:** Grow a previously rendered grid and fail its replacement allocation. **Fix:** Return upload success and capacity explicitly; skip drawing until sufficient storage contains the current state. **Reported by:** Codex #32.

### HIGH

**Summary:** Keep accepted session changes within saving limits so successful changes survive a restart.

**B10 — HIGH.** `libs/draxul-server/src/topology_service.cpp:130` accepts 4,096-byte names, while durable validation allows 512 bytes. Space, tab, and split-depth limits also disagree.

**Trigger:** Rename a Space, tab, or pane to 513 ASCII bytes, create a 65th Space, or create a 129th tab. Subsequent checkpoints fail and restart restores older state. **Fix:** Share live and durable limits and reject invalid mutations before publishing their effects. **Reported by:** Codex #10.

**Summary:** Finish failed Windows connection attempts promptly so they cannot stop new connections or block shutdown.

**B11 — HIGH.** `libs/draxul-control/src/async_frame_stream_win32.cpp:441` waits indefinitely for completion even when connection establishment failed synchronously and no operation is pending.

**Trigger:** A client connects and disconnects before `ConnectNamedPipe`, producing `ERROR_NO_DATA`. **Fix:** Cancel and drain only operations that actually returned `ERROR_IO_PENDING`. **Reported by:** Codex #11.

**Summary:** Use the supplied application location so agent conversations can be reported outside the command search path.

**B12 — HIGH.** `libs/draxul-agent-integration/src/agent_integration.cpp:50` invokes bare `draxul`; the other Windows and POSIX hooks do likewise despite the server supplying `DRAXUL_EXECUTABLE`.

**Trigger:** Start a managed conversation from an unpacked directory or application bundle absent from `PATH`. Reporting fails and the native conversation reference is unavailable for resumption. **Fix:** Prefer the supplied executable path, passing it safely on both platforms. **Reported by:** Codex #12.

**Summary:** Preserve a nearby wide character when updating a narrow character that does not overlap it.

**B13 — HIGH.** `libs/draxul-grid/src/grid.cpp:308` clears a following wide leader regardless of the incoming character’s width.

**Trigger:** Print `" 界"`, return to column zero, and print `"A"`; the unrelated wide character disappears. **Fix:** Clear a neighboring leader only when the replacement actually overlaps it, retaining necessary continuation cleanup. **Reported by:** Codex #13.

**Summary:** Make large editor input cancellable so an editor that stops reading cannot freeze the interface.

**B14 — HIGH.** `libs/draxul-nvim/src/rpc.cpp:269` synchronously writes an entire message under the write mutex using blocking platform pipes.

**Trigger:** Suspend Neovim and paste more than the pipe can buffer. The interface blocks, and shutdown sends another notification before terminating the child. **Fix:** Use bounded outbound work and cancellable writes, with cancellation before shutdown waits. **Reported by:** Codex #14.

**Summary:** Recover the complete editor display when queued updates overflow so changes are not permanently lost.

**B15 — HIGH.** `libs/draxul-nvim/src/rpc.cpp:369` discards the oldest notification at a depth of 4,096, including incremental redraw state, without resynchronization.

**Trigger:** Delay draining while Neovim emits enough notifications to discard a unique update. **Fix:** Preserve ordered delivery or explicitly invalidate and resynchronize the display after overflow. **Reported by:** Codex #15.

**Summary:** Keep accented and joined characters together when terminal output arrives in separate pieces.

**B16 — HIGH.** `libs/draxul-terminal-core/src/vt_parser.cpp:274` flushes the final cluster on each feed and retains no state for later continuation characters.

**Trigger:** Feed `"e"`, then `"\xCC\x81X"`; the accent occupies another cell and `X` moves to column two. **Fix:** Extend the previous cluster across feeds while preserving cursor, width, wrapping, and control boundaries. **Reported by:** Codex #16.

**Summary:** Resize Markdown text when the window moves between displays so it keeps the intended readable size.

**B17 — HIGH.** `modules/markdown/draxul-markdown/src/markdown_host.cpp:158` updates layout without refreshing the private font service’s initial display density.

**Trigger:** Move an open Markdown pane between displays with different densities. The host inherits an empty font-metrics callback. **Fix:** Propagate current density and transactionally rebuild private fonts, metrics, layout, and drawing state. **Reported by:** Codex #17.

**Summary:** Read file names in their original characters so Windows can open Markdown files with non-English names.

**B18 — HIGH.** `modules/markdown/draxul-markdown/src/markdown_host.cpp:64` constructs paths from UTF-8 text using narrow Windows conversion; working-directory construction at line 68 and CLI assignment at `libs/draxul-app-cli/src/cli_args.cpp:220` repeat the problem.

**Trigger:** Open `C:\文档\a.md` on Windows with a non-UTF-8 active code page. **Fix:** Decode and serialize paths explicitly as UTF-8 at CLI and host-launch boundaries, using native paths for filesystem operations. **Reported by:** Codex #18.

**Summary:** Include bundled product views in Windows downloads so users can open the views provided by the build.

**B19 — HIGH.** `do.py:1294` copies selected resource directories and root libraries but omits executable-adjacent native plugin packages.

**Trigger:** Extract a deployment archive containing a product-enabled build onto a clean machine. Runtime discovery finds no bundled products. **Fix:** Include complete published plugin packages and test nested package contents in the archive. **Reported by:** Codex #19.

**Summary:** Keep paused satellite panes processing controls and completed downloads so changes can appear without resuming time.

**B20 — HIGH.** `plugins/satview/src/satview_plugin.cpp:359` removes the tick deadline while paused; redraw requests do not run the runtime pump, and worker completion requests no tick.

**Trigger:** Start paused or refresh while paused, then let a download finish; results remain unconsumed. Camera keys also require the unscheduled pump. **Fix:** Schedule completion and control ticks, retaining bounded movement deadlines while freezing simulation time. **Reported by:** Codex #20.

**Summary:** Give pending sign-image uploads separate storage so quick color changes cannot replace pixels still being copied.

**B21 — HIGH.** `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp:1316` overwrites one shared mapped staging buffer for successive atlas revisions of unchanged size.

**Trigger:** Rebuild sign colors while another frame slot’s copy remains outstanding. Waiting for the current slot does not protect that earlier upload. **Fix:** Use storage owned by each frame slot or a separately retired allocation per revision. **Reported by:** Codex #21.

**Summary:** Keep keyboard input available when a selected music device cannot open.

**B22 — HIGH.** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:1311` clears the entire input rig after hardware selection has already installed a keyboard fallback.

**Trigger:** A listed music device fails to open after enumeration or lease acquisition. **Fix:** Release only the failed hardware lease while preserving fallback input and the failed-request result. **Reported by:** Codex #22.

**Summary:** Block the entire required space around pads so wide tracks cannot pass too close.

**B23 — HIGH.** `plugins/pcbview/src/autorouter.cpp:243` limits blocker iteration using the via radius even when the track radius is larger.

**Trigger:** With 0.5 mm grid spacing, 2 mm track width, 0.6 mm via diameter, zero clearance, and a 1 mm pad at `(5.1,5)`, a track at `x=6.5` receives no blocker despite being only 1.4 mm away. **Fix:** Use the larger radius for iteration and retain separate exact track and via checks. **Reported by:** Codex #23.

**Summary:** Check clearance along complete tracks so diagonal sections cannot overlap unrelated pads.

**B24 — HIGH.** `plugins/pcbview/src/autorouter.cpp:397` validates route nodes rather than connecting segments; search and emitted endpoint connectors have the same omission.

**Trigger:** With 0.5 mm spacing, a 0.3 mm track, and a 0.4 mm pad at `(4.6,4.6)`, nodes `(4.5,5)` and `(5,4.5)` pass the 0.35 mm keepout, but their diagonal passes approximately 0.212 mm from the pad. **Fix:** Validate continuous segment clearance throughout routing and final geometry checks. **Reported by:** Codex #24.

**Summary:** Synchronize shared depth images so successive drawing passes preserve correct visibility.

**B25 — HIGH.** `plugins/rezonality/src/native_backend_vulkan.cpp:1068` defines dependencies without depth-test stages or depth attachment accesses.

**Trigger:** Open the shipped robot scene, where one pass clears shared depth and the next loads, tests, and writes it. **Fix:** Include appropriate early/late depth stages and read/write dependencies across passes and frame reuse. **Reported by:** Codex #25.

**Summary:** Reject unsupported feedback inputs so scenes cannot read a texture while writing the same output.

**B26 — HIGH.** `plugins/rezonality/src/native_backend_vulkan.cpp:1201` resolves previous-frame sampling to the same image currently used as a target. Metal also ignores the parsed previous-frame flag.

**Trigger:** Use `targets:(A), samplers:(!A)` and sample that input; plain same-pass aliases fail similarly. **Fix:** Reject unsupported previous-frame inputs and same-pass aliases before native preparation. **Reported by:** Codex #26.

### MEDIUM

**Summary:** Stop dragging when the mouse button is released anywhere so selections do not keep moving afterward.

**B27 — MEDIUM.** `app/input_dispatcher.cpp:536` consumes releases over pane pills, and chrome filtering at line 545 also precedes drag cleanup.

**Trigger:** Start terminal selection or divider dragging, release over chrome, then move without holding the button. Cross-pane releases can also reach the wrong host. **Fix:** Retain the drag owner and deliver or cancel its release before filtering; clear ownership on lifecycle cancellation. **Reported by:** Codex #27.

**Summary:** Print the full visible zoomed pane so its contents are not cut off.

**B28 — MEDIUM.** `app/app.cpp:2026` records the stored split rectangle, while zoomed presentation uses a separate full viewport at `app/pane_manager.cpp:1476`.

**Trigger:** Zoom a split pane and print it. **Fix:** Capture the displayed viewport and matching content hint, including zoom and chrome offsets. **Reported by:** Codex #28.

**Summary:** Release partially created drawing buffers when allocation fails so retries do not retain unused graphics memory.

**B30 — MEDIUM.** `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_vk_resources.cpp:69` returns after index allocation failure without releasing the successful vertex allocation.

**Trigger:** Foliage replacement allocates vertices successfully but fails index allocation; local plain handles lose ownership and retries leak more memory. **Fix:** Retain scoped ownership until both allocations succeed or explicitly clean up partial allocation. **Reported by:** Codex #30.

**Summary:** Recover a drawing surface after frame preparation fails so rendering can safely continue.

**B31 — MEDIUM.** `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:1101` bypasses recovery after successful image acquisition; atlas-recording failure at line 1108 does likewise.

**Trigger:** Acquisition succeeds, then command preparation or atlas staging fails. The next frame reuses the acquired-image semaphore without retiring the prior acquisition. **Fix:** Route all post-acquisition failures through synchronization recovery and frame abort or recreation. **Reported by:** Codex #31.

**Summary:** Retry failed font-image transfers so a temporary memory shortage does not leave text missing.

**B33 — MEDIUM.** `libs/draxul-renderer/src/metal/metal_renderer.mm:1065` clears pending uploads after staging failure; mapping failure and unchecked encoder creation similarly lose transfers.

**Trigger:** Fail allocation, mapping, or encoder creation after cached glyph pixels have been queued and their original dirty state cleared. **Fix:** Retain pending pixels until successful recording, retry, and avoid drawing dependent glyphs with incomplete atlas data. **Reported by:** Codex #33.

**Summary:** Allow an agent to replace an existing terminal even when the tab has reached its pane limit.

**B34 — MEDIUM.** `libs/draxul-server/src/topology_service.cpp:682` rejects launches at the pane limit before checking replacement mode.

**Trigger:** Replace a terminal in a valid 256-pane tab; replacement adds no pane but returns `limit_reached`. **Fix:** Apply the count guard only when adding a pane. **Reported by:** Codex #34.

## Reconciliation and dependencies

There are no conflicting substantive positions from Claude because its indexed report is unfinished. Two Codex severity ratings change:

| Finding | Codex rating | Ruling |
|---|---|---|
| B29 | MEDIUM | **CRITICAL** under the explicit data-corruption rule. Impact is limited to regenerable downloaded caches, but valid saved data can be replaced with incomplete data that still parses. |
| B32 | MEDIUM | **CRITICAL** because drawing beyond the retained allocation is directly established. A particular driver crash is not claimed. |

B31 remains MEDIUM: the invalid synchronization recovery path is confirmed, but a particular native crash or stall was not reproduced.

No reported defect was rejected. Shared causes were consolidated within B27, including divider releases, and within B29, including cloud-cache publication.

All initialized product boards and their actual lanes were inspected. No accepted defect is explicitly covered by an existing card. Nearby ownership and performance cards are related work, not duplicates. Root `kanban/ice-box/` is absent; root board metadata contains references to absent historical cards, including worker-exception and keyboard-resume cards. Their unavailable contents cannot establish duplicate coverage.

**Summary:** Coordinate fixes that share the same boundary so one change does not invalidate another.

- B03 and B14 should share outbound-pipe error and shutdown design.
- B05 and B10 share session validation and must coordinate with `kanban/pending/10 session-model-store -refactor.md`.
- B09 and B28 share print orchestration.
- B02 and B20 should coordinate with `plugins/satview/kanban/pending/06 satview-catalog-publication -perf.md`; completion-driven updates must also preserve the settling behavior sought by `plugins/satview/kanban/pending/04 satview-inactive-propagation -perf.md`.
- B15 should coordinate with `kanban/pending/45 nvim-notification-move-and-wake -perf.md`.
- B23 and B24 require independent regression cases: fixing blocker extent does not make diagonal segments safe.
- B08 and B26 share candidate validation, but B25’s depth synchronization cannot make same-pass feedback valid.
- B31–B33 should coordinate renderer failure handling with `kanban/pending/23 native-renderer-operation-tests -refactor.md` and `kanban/pending/39 slot-aware-grid-uploads -perf.md`.

## Unresolved leads and verification gaps

These remain excluded from accepted work and receive no cards.

| Lead | Missing evidence |
|---|---|
| Neovim wide-cell decoding | A real wire fixture or supplied protocol evidence establishing explicit empty continuation cells. |
| Remote compatibility-worker command loss | A supported application path using that worker rather than the coordinator, whose failures requeue commands. |
| Pseudoconsole shutdown | A demonstrated native close requiring output drainage after the reader stops. |
| Companion-owner removal | A demonstrated user-visible failure after an owner closes while its companion remains. |
| Non-finite saved split ratios | A complete restoration trace proving the value survives later guards and causes failure. |
| Product worker publication and Metal audio updates | Complete interleavings establishing stale publication or overlapping graphics-resource use. |

Coverage remains partial. Native failures, thread interleavings, graphics validation, and platform behavior were assessed statically, without execution. The incomplete Claude report supplies no additional coverage assurance.

## Proposed work items

The following are complete proposed cards for the trusted parent process.

### kanban/pending/00 nvim-closed-pipe-signal -bug.md

# Handle closed editor pipes safely

**Summary:** Handle a closed editor connection safely so an input operation cannot terminate the application.

**Priority:** 68  
**Severity:** CRITICAL  
**Source:** `libs/draxul-nvim/src/nvim_process.cpp`

**Evidence and trigger:** B03; line 566 writes without parent-side signal suppression. An editor closing its input before a notification can terminate the macOS client.

- [ ] **Investigate:** Trace parent signal handling and child inheritance, including the test runner’s existing suppression.
- [ ] **Fix:** Suppress the signal for parent writes while preserving default child handling; coordinate cancellable writes with B14.
- [ ] **Acceptance:** An isolated client with default initial signal handling survives a closed-pipe write and reports failure.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; verify the macOS production path.

### kanban/pending/02 terminal-marker-scroll-overflow -bug.md

# Clamp scrolling before moving prompt markers

**Summary:** Limit scrolling before moving saved prompt markers so extreme output cannot cause invalid arithmetic.

**Priority:** 69  
**Severity:** CRITICAL  
**Source:** `libs/draxul-terminal-core/src/terminal_core.cpp`

**Evidence and trigger:** B04; line 533 moves markers before clamping. A row-one prompt marker followed by maximum downward scrolling overflows signed arithmetic.

- [ ] **Investigate:** Trace upward and downward displacement through parsing, markers, and grid scrolling.
- [ ] **Fix:** Clamp once to the valid scrolling region before moving either markers or cells.
- [ ] **Acceptance:** Extreme scroll requests produce defined arithmetic and correct marker retention or removal.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.

### kanban/pending/03 session-identifier-overflow -bug.md

# Validate restored identifiers and counters

**Summary:** Reject saved numbers that cannot support the next item safely so restoring a session cannot overflow its counters.

**Priority:** 70  
**Severity:** CRITICAL  
**Source:** `libs/draxul-session-model/src/session_state.cpp`

**Evidence and trigger:** B05; maximum signed identifiers pass validation, then overflow during server capture or standalone Space and tab restoration.

- [ ] **Investigate:** Inventory identifier and next-counter validation and increment sites across both restoration paths.
- [ ] **Fix:** Reject unsupported ranges and use checked increments with explicit exhaustion handling.
- [ ] **Acceptance:** Boundary identifiers and allocator values cannot overflow during restore, capture, or subsequent creation.
- [ ] **Validation:** Retain valid-session round trips; run core aggregate tests and same-cache smoke.

### kanban/pending/60 print-temporary-directory-failure -bug.md

# Report unavailable temporary storage during printing

**Summary:** Show a printing error when temporary storage is unavailable so a failed print cannot close the application.

**Priority:** 71  
**Severity:** CRITICAL  
**Source:** `app/app.cpp`

**Evidence and trigger:** B09; line 2064 performs throwing temporary-directory lookup outside error handling after successful capture.

- [ ] **Investigate:** Trace temporary-path creation through capture completion and print notifications.
- [ ] **Fix:** Use error-code lookup and report failure before constructing the output document.
- [ ] **Acceptance:** Missing or inaccessible temporary storage leaves the client running and produces a useful print error.
- [ ] **Validation:** Verify normal printing remains functional; run core aggregate tests and same-cache smoke.

### kanban/done/61 metal-grid-allocation-failure -bug.md

# Skip grids whose larger drawing allocation failed

**Summary:** Skip drawing when larger drawing memory cannot be created so a resize cannot read beyond the available storage.

**Priority:** 72  
**Severity:** CRITICAL  
**Source:** `libs/draxul-renderer/src/metal/metal_renderer.mm`

**Evidence and trigger:** B32; failed growth retains a smaller buffer, while drawing continues with the resized grid’s instance counts.

- [ ] **Investigate:** Trace slot capacity, upload success, mapping, and draw counts after resize failure.
- [ ] **Fix:** Return upload success and skip drawing until sufficient storage contains current state.
- [ ] **Acceptance:** Injected growth and mapping failures emit no draw that addresses insufficient or stale storage; later frames recover.
- [ ] **Validation:** Run core aggregate tests, same-cache smoke, and relevant macOS rendering checks.

### kanban/pending/62 topology-durable-limits -bug.md

# Keep live session changes within saving limits

**Summary:** Keep accepted session changes within saving limits so successful changes survive a restart.

**Priority:** 73  
**Severity:** HIGH  
**Source:** `libs/draxul-server/src/topology_service.cpp`

**Evidence and trigger:** B10; live names, Space counts, tab counts, and split depth can exceed durable limits, preventing subsequent checkpoints.

- [ ] **Investigate:** Compare every relevant live mutation limit with current-format session validation.
- [ ] **Fix:** Share bounds and validate before publishing mutation side effects.
- [ ] **Acceptance:** Over-limit names and layouts are rejected without changes; accepted boundary values save and restore successfully.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.

### kanban/pending/66 windows-stream-accept-failure -bug.md

# Avoid waiting for nonexistent connection operations

**Summary:** Finish failed Windows connection attempts promptly so they cannot stop new connections or block shutdown.

**Priority:** 74  
**Severity:** HIGH  
**Source:** `libs/draxul-control/src/async_frame_stream_win32.cpp`

**Evidence and trigger:** B11; synchronous connection failure reaches an infinite wait on an unsignaled event without pending work.

- [ ] **Investigate:** Distinguish synchronous success, synchronous failure, pending completion, and cancellation paths.
- [ ] **Fix:** Cancel and drain only genuinely pending connection operations.
- [ ] **Acceptance:** Immediate disconnect and injected synchronous errors return promptly; listener shutdown completes.
- [ ] **Validation:** Run Windows control/server coverage, core aggregate tests, and same-cache smoke.

### kanban/pending/69 agent-hook-executable-path -bug.md

# Use the supplied executable location in agent hooks

**Summary:** Use the supplied application location so agent conversations can be reported outside the command search path.

**Priority:** 75  
**Severity:** HIGH  
**Source:** `libs/draxul-agent-integration/src/agent_integration.cpp`

**Evidence and trigger:** B12; generated hooks invoke bare `draxul` despite receiving its executable location from the server.

- [ ] **Investigate:** Trace executable environment propagation and command construction for both agent integrations.
- [ ] **Fix:** Prefer the supplied location and pass paths safely, including spaces and non-English characters.
- [ ] **Acceptance:** Both integrations report native conversation references when the application is absent from the command search path.
- [ ] **Validation:** Check Windows and macOS hook behavior; run core aggregate tests and same-cache smoke.

### kanban/done/70 narrow-cell-wide-neighbor -bug.md

# Preserve nonoverlapping wide characters

**Summary:** Preserve a nearby wide character when updating a narrow character that does not overlap it.

**Priority:** 76  
**Severity:** HIGH  
**Source:** `libs/draxul-grid/src/grid.cpp`

**Evidence and trigger:** B13; writing a narrow character immediately before a wide leader clears that unrelated leader and continuation.

- [ ] **Investigate:** Characterize narrow replacements, wide replacements, and writes over continuation cells.
- [ ] **Fix:** Restrict neighboring-leader cleanup to actual overlap.
- [ ] **Acceptance:** Updating the first cell of `" 界"` to `"A"` preserves the wide glyph; overlapping writes still remove orphaned continuations.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.

### kanban/pending/71 cancellable-nvim-output -bug.md

# Make editor writes bounded and cancellable

**Summary:** Make large editor input cancellable so an editor that stops reading cannot freeze the interface.

**Priority:** 77  
**Severity:** HIGH  
**Source:** `libs/draxul-nvim/src/rpc.cpp`

**Evidence and trigger:** B14; synchronous message writes block the interface when a nonreading editor exhausts pipe capacity.

- [ ] **Investigate:** Trace request, notification, reply, and shutdown writes on both platforms.
- [ ] **Fix:** Introduce bounded outbound ownership and cancellable writes; cancel before shutdown waits.
- [ ] **Acceptance:** Oversized input to a nonreading child leaves the interface responsive, preserves message ordering, and permits bounded shutdown.
- [ ] **Validation:** Exercise both platform transports; run core aggregate tests and same-cache smoke.

### kanban/pending/72 nvim-redraw-queue-recovery -bug.md

# Recover display state after editor queue overflow

**Summary:** Recover the complete editor display when queued updates overflow so changes are not permanently lost.

**Priority:** 78  
**Severity:** HIGH  
**Source:** `libs/draxul-nvim/src/rpc.cpp`

**Evidence and trigger:** B15; the bounded queue drops incremental notifications without notifying the host or requesting resynchronization.

- [ ] **Investigate:** Trace overflow across redraw transactions, flushes, requests, and host draining.
- [ ] **Fix:** Preserve ordered delivery or introduce explicit invalidation and complete display recovery.
- [ ] **Acceptance:** A burst beyond queue capacity cannot leave a discarded unique update permanently missing; memory remains bounded.
- [ ] **Validation:** Coordinate existing notification work; run core aggregate tests and same-cache smoke.

### kanban/done/73 terminal-cross-feed-clusters -bug.md

# Preserve character clusters across output chunks

**Summary:** Keep accented and joined characters together when terminal output arrives in separate pieces.

**Priority:** 79  
**Severity:** HIGH  
**Source:** `libs/draxul-terminal-core/src/vt_parser.cpp`

**Evidence and trigger:** B16; final clusters are committed at every feed, so a later combining accent or joined character cannot extend its base.

- [ ] **Investigate:** Trace cluster continuation across output chunks, cursor movement, wrapping, and control sequences.
- [ ] **Fix:** Preserve enough preceding-cluster state to extend continued characters without delaying ordinary visible output.
- [ ] **Acceptance:** Chunked and unchunked accented and joined text yield matching cells and cursor positions, including wrap boundaries.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.

### kanban/pending/74 markdown-display-density -bug.md

# Refresh Markdown fonts after display changes

**Summary:** Resize Markdown text when the window moves between displays so it keeps the intended readable size.

**Priority:** 80  
**Severity:** HIGH  
**Source:** `modules/markdown/draxul-markdown/src/markdown_host.cpp`

**Evidence and trigger:** B17; private fonts retain initialization density after the application updates shared fonts for a different display.

- [ ] **Investigate:** Trace display-density delivery to private rich-text fonts and companion previews.
- [ ] **Fix:** Propagate current density and rebuild fonts, metrics, layout, and draw state transactionally.
- [ ] **Acceptance:** Moving between differently scaled displays updates text and hit geometry; failed rebuilding retains usable presentation.
- [ ] **Validation:** Verify display movement on available platforms; run core aggregate tests, relevant rendering checks, and same-cache smoke.

### kanban/pending/75 windows-markdown-source-paths -bug.md

# Preserve source-path characters through launch

**Summary:** Read file names in their original characters so Windows can open Markdown files with non-English names.

**Priority:** 81  
**Severity:** HIGH  
**Source:** `libs/draxul-app-cli/src/cli_args.cpp`

**Evidence and trigger:** B18; UTF-8 arguments undergo narrow Windows path conversion in CLI parsing and Markdown source resolution.

- [ ] **Investigate:** Trace source and working-directory decoding and serialization through CLI and host launch.
- [ ] **Fix:** Use explicit UTF-8 boundaries while retaining native filesystem paths.
- [ ] **Acceptance:** Absolute and relative non-English source paths open on Windows with a non-UTF-8 code page; ordinary paths remain valid.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke; verify the Windows path scenario.

### kanban/pending/76 windows-deploy-plugin-packages -bug.md

# Include native plugin packages in Windows deployment

**Summary:** Include bundled product views in Windows downloads so users can open the views provided by the build.

**Priority:** 82  
**Severity:** HIGH  
**Source:** `do.py`

**Evidence and trigger:** B19; deployment omits the published plugin directory required by runtime discovery.

- [ ] **Investigate:** Compare staged package contents with deployment and archive contents.
- [ ] **Fix:** Copy complete published packages, including selection metadata, native modules, private dependencies, and assets.
- [ ] **Acceptance:** A nested package survives archive extraction and is discoverable without independently installed user plugins.
- [ ] **Validation:** Extend deployment coverage; run core aggregate tests and same-cache smoke, plus relevant packaged-product startup checks.

### kanban/pending/77 mouse-drag-release-owner -bug.md

# Deliver releases to the active drag owner

**Summary:** Stop dragging when the mouse button is released anywhere so selections do not keep moving afterward.

**Priority:** 83  
**Severity:** MEDIUM  
**Source:** `app/input_dispatcher.cpp`

**Evidence and trigger:** B27; chrome, pills, panel capture, and cross-pane routing can prevent terminal or divider drag cleanup.

- [ ] **Investigate:** Trace press ownership and release filtering for terminal selections and split dividers.
- [ ] **Fix:** Deliver or cancel matching releases before filtering, and clear ownership on focus or lifecycle cancellation.
- [ ] **Acceptance:** Releasing over chrome, another pane, or an overlay stops selection and divider dragging; ordinary clicks still route correctly.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.

### kanban/pending/78 zoomed-pane-print-crop -bug.md

# Print the displayed zoomed viewport

**Summary:** Print the full visible zoomed pane so its contents are not cut off.

**Priority:** 84  
**Severity:** MEDIUM  
**Source:** `app/app.cpp`

**Evidence and trigger:** B28; printing crops with the stored split rectangle rather than the separately displayed zoomed viewport.

- [ ] **Investigate:** Compare split geometry, displayed host geometry, chrome offsets, and content hints.
- [ ] **Fix:** Snapshot the displayed viewport and matching print hint for capture.
- [ ] **Acceptance:** Printing zoomed left, right, top, and bottom panes captures their visible content with correct offsets; unzoomed printing remains correct.
- [ ] **Validation:** Run core aggregate tests, relevant capture checks, and same-cache smoke.

### kanban/pending/79 vulkan-acquired-frame-recovery -bug.md

# Recover failed frames after image acquisition

**Summary:** Recover a drawing surface after frame preparation fails so rendering can safely continue.

**Priority:** 85  
**Severity:** MEDIUM  
**Source:** `libs/draxul-renderer/src/vulkan/vk_renderer.cpp`

**Evidence and trigger:** B31; command preparation and atlas-recording failures return after acquisition without retiring the image or recovering synchronization.

- [ ] **Investigate:** Inventory every failure after successful acquisition and existing abort/recreation behavior.
- [ ] **Fix:** Route those failures through synchronization recovery and image retirement or swapchain recreation.
- [ ] **Acceptance:** Injected post-acquisition failures permit the next frame without reusing an outstanding acquisition semaphore or leaking acquired images.
- [ ] **Validation:** Run core aggregate tests, Vulkan validation and relevant render checks, and same-cache smoke.

### kanban/pending/80 metal-atlas-upload-retry -bug.md

# Retain failed font-image uploads for retry

**Summary:** Retry failed font-image transfers so a temporary memory shortage does not leave text missing.

**Priority:** 86  
**Severity:** MEDIUM  
**Source:** `libs/draxul-renderer/src/metal/metal_renderer.mm`

**Evidence and trigger:** B33; allocation, mapping, or encoder failure loses queued glyph pixels after their original dirty state was cleared.

- [ ] **Investigate:** Trace pixel ownership and dirty-state transitions through queuing, recording, and dependent draws.
- [ ] **Fix:** Preserve uploads on failure, check encoder creation, and schedule recovery before dependent glyph drawing.
- [ ] **Acceptance:** Injected failures retain pixels and recover on a later frame without requiring font reset.
- [ ] **Validation:** Run core aggregate tests, relevant macOS text-render checks, and same-cache smoke.

### kanban/pending/81 agent-replacement-pane-limit -bug.md

# Permit replacement at the pane limit

**Summary:** Allow an agent to replace an existing terminal even when the tab has reached its pane limit.

**Priority:** 87  
**Severity:** MEDIUM  
**Source:** `libs/draxul-server/src/topology_service.cpp`

**Evidence and trigger:** B34; the pane-count guard precedes replacement handling, rejecting an operation that adds no pane.

- [ ] **Investigate:** Trace replacement and additional-pane allocation, including failure rollback.
- [ ] **Fix:** Apply the count limit only to operations that increase pane count.
- [ ] **Acceptance:** Replacement succeeds in a full valid tab, new-pane launch remains rejected, and invalid replacement targets remain rejected.
- [ ] **Validation:** Run core aggregate tests and same-cache smoke.

### plugins/megacity/kanban/pending/11 biology-noise-overflow -bug.md

# Use defined arithmetic for biological noise

**Summary:** Use safe arithmetic when generating biological shapes so opening the biology view cannot cause invalid calculations.

**Priority:** 11  
**Severity:** CRITICAL  
**Source:** `plugins/megacity/product/draxul-geometry/src/cell_generator.cpp`

**Evidence and trigger:** B01; coordinate products overflow before unsigned conversion under ordinary biology settings.

- [ ] **Investigate:** Trace default noise offsets, octave expansion, and negative-coordinate behavior.
- [ ] **Fix:** Convert coordinates before multiplication and use unsigned constants.
- [ ] **Acceptance:** Default biology construction and boundary coordinates execute without signed overflow and remain deterministic.
- [ ] **Validation:** Run the MegaCity-scoped aggregate, relevant biology rendering checks, and same-cache smoke; use undefined-behavior instrumentation where available.

### plugins/megacity/kanban/pending/12 label-upload-staging-lifetime -bug.md

# Separate outstanding sign-image uploads

**Summary:** Give pending sign-image uploads separate storage so quick color changes cannot replace pixels still being copied.

**Priority:** 12  
**Severity:** HIGH  
**Source:** `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp`

**Evidence and trigger:** B21; same-sized atlas revisions overwrite shared staging while an earlier frame may still copy it.

- [ ] **Investigate:** Trace revisions, slot completion, and staging retirement across automatic color rebuilds.
- [ ] **Fix:** Use staging per frame slot or separately retired allocations per revision.
- [ ] **Acceptance:** Outstanding same-sized revisions retain their own source bytes until completion and render the intended labels.
- [ ] **Validation:** Preserve Metal behavior; run the MegaCity-scoped aggregate, Vulkan label checks, and same-cache smoke.

### plugins/megacity/kanban/pending/13 mesh-partial-allocation-cleanup -bug.md

# Clean up partially allocated mesh buffers

**Summary:** Release partially created drawing buffers when allocation fails so retries do not retain unused graphics memory.

**Priority:** 13  
**Severity:** MEDIUM  
**Source:** `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_vk_resources.cpp`

**Evidence and trigger:** B30; vertex allocation survives failed index allocation and is lost by local foliage replacement handles.

- [ ] **Investigate:** Inventory mesh-upload ownership and failure cleanup in callers.
- [ ] **Fix:** Retain scoped ownership until complete success or destroy partial allocations before returning.
- [ ] **Acceptance:** Repeated index-allocation failures leave no retained vertex allocations; successful uploads still transfer ownership correctly.
- [ ] **Validation:** Run the MegaCity-scoped aggregate, relevant Vulkan rendering checks, and same-cache smoke.

### plugins/satview/kanban/pending/09 asset-root-worker-race -bug.md

# Give background loaders safe asset locations

**Summary:** Protect the shared asset location so opening another satellite pane cannot interfere with background file loading.

**Priority:** 09  
**Severity:** CRITICAL  
**Source:** `plugins/satview/src/core/satview_texture_assets.cpp`

**Evidence and trigger:** B02; pane creation modifies a shared path while existing catalog workers resolve bundled files.

- [ ] **Investigate:** Trace every asset-root read and write across multiple panes, loading, and shutdown.
- [ ] **Fix:** Prefer immutable per-service roots; otherwise synchronize reads and writes using the same protection.
- [ ] **Acceptance:** Controlled overlapping pane creation and catalog loading cannot access a path concurrently with mutation or mix pane asset roots.
- [ ] **Validation:** Run the SatView-scoped aggregate, multi-pane checks, and same-cache smoke on both supported platform paths.

### plugins/satview/kanban/pending/10 cache-final-close-validation -bug.md

# Check completed cache writes before replacement

**Summary:** Check that cache files finish writing before replacing the saved copy so storage failures cannot publish incomplete data.

**Priority:** 10  
**Severity:** CRITICAL  
**Source:** `plugins/satview/src/services/satview_cache_publication.cpp`

**Evidence and trigger:** B29; catalog and cloud writers replace saved files without checking final flush or close failure, potentially publishing truncated data.

- [ ] **Investigate:** Trace both publication helpers and catalog acceptance of truncated complete-row prefixes.
- [ ] **Fix:** Explicitly close and check streams before replacement; remove only the writer’s temporary file on failure.
- [ ] **Acceptance:** Injected close-stage failure returns failure, preserves destination bytes, and cleans temporary files for catalog and cloud writes.
- [ ] **Validation:** Check both platform replacement branches; run the SatView-scoped aggregate and same-cache smoke.

### plugins/satview/kanban/pending/11 paused-controls-completion-ticks -bug.md

# Process controls and completed work while paused

**Summary:** Keep paused satellite panes processing controls and completed downloads so changes can appear without resuming time.

**Priority:** 11  
**Severity:** HIGH  
**Source:** `plugins/satview/src/satview_plugin.cpp`

**Evidence and trigger:** B20; paused ticks remove their deadline, while worker completion and redraw-only input do not schedule another pump.

- [ ] **Investigate:** Trace adapter scheduling for completion, refresh, camera keys, visibility, and pause.
- [ ] **Fix:** Request completion/control ticks and bounded active-control deadlines without advancing paused simulation time.
- [ ] **Acceptance:** Paused startup and refresh consume completed results; camera controls work; inactive panes settle without busy loops.
- [ ] **Validation:** Exercise the real plugin adapter scheduling boundary; run the SatView-scoped aggregate and same-cache smoke.

### plugins/scoreview/kanban/pending/07 progress-restoration-validation -bug.md

# Reject invalid saved practice values safely

**Summary:** Reject unusable saved practice data so opening a piece can start safely.

**Priority:** 07  
**Severity:** CRITICAL  
**Source:** `plugins/scoreview/product/draxul-score-learn/src/player_model.cpp`

**Evidence and trigger:** B06; valid syntax with wrong field types or invalid numeric keys throws through initialization after clearing model state.

- [ ] **Investigate:** Inventory typed fields, numeric keys, bounds, and partial mutation during restoration.
- [ ] **Fix:** Validate temporary state, contain conversion failures, and publish only complete valid state.
- [ ] **Acceptance:** Wrong types and invalid keys return failure without exceptions or partial state; source attachment reaches the intended fresh-session behavior.
- [ ] **Validation:** Preserve valid progress round trips; run the ScoreView-scoped aggregate and same-cache smoke.

### plugins/scoreview/kanban/pending/08 device-failure-keyboard-fallback -bug.md

# Preserve keyboard input after hardware failure

**Summary:** Keep keyboard input available when a selected music device cannot open.

**Priority:** 08  
**Severity:** HIGH  
**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp`

**Evidence and trigger:** B22; hardware selection installs fallback input, then runtime cleanup deletes that fallback when opening fails.

- [ ] **Investigate:** Trace immediate device-open failure, fallback selection, lease release, and later recovery.
- [ ] **Fix:** Release the unsuccessful hardware lease without clearing the installed keyboard input.
- [ ] **Acceptance:** Injected device-open failure leaves keyboard practice usable and releases hardware ownership; successful selection still works.
- [ ] **Validation:** Run the ScoreView-scoped aggregate and same-cache smoke; check supported device paths on both platforms.

### plugins/pcbview/kanban/pending/04 wide-track-pad-blockers -bug.md

# Generate blockers for the larger routing radius

**Summary:** Block the entire required space around pads so wide tracks cannot pass too close.

**Priority:** 04  
**Severity:** HIGH  
**Source:** `plugins/pcbview/src/autorouter.cpp`

**Evidence and trigger:** B23; blocker iteration follows via radius even when accepted track settings require a larger pad exclusion area.

- [ ] **Investigate:** Trace iteration bounds, pad snapping, and track/via radius validation.
- [ ] **Fix:** Derive iteration extent from the larger radius while retaining separate distance checks.
- [ ] **Acceptance:** The wide-track example at a 1.4 mm pad distance is blocked; smaller tracks and via clearances remain correctly handled.
- [ ] **Validation:** Run the PCBView-scoped aggregate, relevant routing/render checks, and same-cache smoke.

### plugins/pcbview/kanban/pending/05 continuous-track-pad-clearance -bug.md

# Validate clearance along emitted track segments

**Summary:** Check clearance along complete tracks so diagonal sections cannot overlap unrelated pads.

**Priority:** 05  
**Severity:** HIGH  
**Source:** `plugins/pcbview/src/autorouter.cpp`

**Evidence and trigger:** B24; clear nodes can connect through a pad keepout, and snapped endpoint connectors receive no continuous check.

- [ ] **Investigate:** Inventory pattern, search, layer-change, endpoint connector, and final legalization paths.
- [ ] **Fix:** Check segment-to-pad clearance for candidate edges and final emitted geometry.
- [ ] **Acceptance:** The diagonal 0.212 mm example is rejected; unsafe endpoint connectors are rejected; valid routes retain intended endpoint connections.
- [ ] **Validation:** Keep a separate regression for blocker extent; run the PCBView-scoped aggregate and same-cache smoke.

### plugins/rezonality/kanban/pending/09 build-temporary-storage-failure -bug.md

# Contain temporary-storage failures during rebuilding

**Summary:** Report unavailable temporary storage so a failed shader edit leaves the application running.

**Priority:** 09  
**Severity:** CRITICAL  
**Source:** `plugins/rezonality/src/live_project.cpp`

**Evidence and trigger:** B07; throwing temporary-directory lookup precedes candidate handling and escapes the unguarded worker build call.

- [ ] **Investigate:** Trace all operations outside existing build handlers and worker exception containment.
- [ ] **Fix:** Use error-code lookup and convert complete-build exceptions into ordinary generation diagnostics.
- [ ] **Acceptance:** Missing temporary storage preserves the active scene, publishes an error, and permits a later successful rebuild.
- [ ] **Validation:** Run the Rezonality-scoped aggregate, relevant reload checks, and same-cache smoke.

### plugins/rezonality/kanban/pending/10 color-attachment-count-validation -bug.md

# Reject unsupported color-output counts

**Summary:** Reject excessive color outputs so an invalid scene cannot crash the graphics backend.

**Priority:** 10  
**Severity:** CRITICAL  
**Source:** `plugins/rezonality/src/native_backend_metal.mm`

**Evidence and trigger:** B08; unchecked scene targets exceed Metal’s fixed slots, and Vulkan also lacks a device-limit check.

- [ ] **Investigate:** Trace target counting and validation before pipeline preparation and recording.
- [ ] **Fix:** Reject unsupported counts before native indexing, using each backend’s actual limit.
- [ ] **Acceptance:** A nine-color Metal candidate and Vulkan candidates exceeding the device limit fail with diagnostics while retaining the active scene.
- [ ] **Validation:** Check accepted boundary counts; run the Rezonality-scoped aggregate, both backend checks, and same-cache smoke.

### plugins/rezonality/kanban/pending/11 shared-depth-pass-synchronization -bug.md

# Synchronize reused depth images

**Summary:** Synchronize shared depth images so successive drawing passes preserve correct visibility.

**Priority:** 11  
**Severity:** HIGH  
**Source:** `plugins/rezonality/src/native_backend_vulkan.cpp`

**Evidence and trigger:** B25; the shipped robot scene reuses depth across clear/load/test/write passes, but dependencies cover only color and sampling.

- [ ] **Investigate:** Trace depth initialization, pass dependencies, loads/stores, and reuse across frames.
- [ ] **Fix:** Add the necessary depth stages and read/write dependencies without weakening color synchronization.
- [ ] **Acceptance:** The robot scene and another shared-depth case pass synchronization validation and preserve expected occlusion.
- [ ] **Validation:** Preserve Metal behavior; run the Rezonality-scoped aggregate, Vulkan rendering checks, and same-cache smoke.

### plugins/rezonality/kanban/pending/12 unsupported-texture-feedback -bug.md

# Reject unsupported previous-frame and aliased inputs

**Summary:** Reject unsupported feedback inputs so scenes cannot read a texture while writing the same output.

**Priority:** 12  
**Severity:** HIGH  
**Source:** `plugins/rezonality/src/live_project.cpp`

**Evidence and trigger:** B26; the parser accepts previous-frame sampling, but both backends resolve it to the current target texture.

- [ ] **Investigate:** Trace sampler flags and target identity through both native backends.
- [ ] **Fix:** Reject unsupported previous-frame inputs and same-pass target/sampler aliases before preparation.
- [ ] **Acceptance:** Previous-frame and plain aliased inputs fail with useful diagnostics while preserving the active scene; valid earlier-pass inputs remain accepted.
- [ ] **Validation:** Run the Rezonality-scoped aggregate, relevant checks on both backends, and same-cache smoke.

**Model:** `gpt-6.1-sol`
