# Draxul runtime optimization consensus

**Model:** `gpt-6-astra`  
**Inputs:** the two reports named in `INPUT_REVIEWS/index.md`: Claude Opus 5.5 and Codex GPT-6 Sol. I checked their claims against current source, the feature inventory, available metrics and benchmarks, and the initialized root and product Kanban lanes. This was a read-only review; no build, test, or project binary ran.

The reports examined complementary workloads. Claude gave broader coverage of idle work, memory, native resource lifetime, and product startup. Codex supplied tighter workload boundaries for fragmented RPC, Kanban ordering, PCB routing, ScoreView layout, and several renderer paths. **Every accepted cost below is a static cost model, not a measured speedup.** Existing `PERF_MEASURE` scopes and the Session transport load-test metrics offer starting points, but do not measure these proposed gains. No P0 stall or resource blow-up was established.

The cards below are the accepted findings and their implementation plans. Each identifies its source, workload, protection already present, measurement, safety checks, and owner. P1 means a material common-path or sustained idle cost with a clear mechanism. P2 means a credible bounded-workload gain whose total impact still needs a baseline. Report attribution uses **Claude** and **Codex** for the two indexed reports.

## Sequencing

1. Establish baselines, then address ligature expansion, hidden or paused SatView propagation, Rezonality’s idle watcher and resize path, the app’s idle loop, and the plugin self-tick loop. These have the clearest repeated work or visible stall mechanism.
2. For terminal throughput, optimize scrolling before interpreting residual serialization and queue metrics. Keep delta admission and worker publication redesign as investigations until their observed frequency is known.
3. Fix renderer sizing and overlay layout alongside slot-aware uploads; keep atlas reset and per-slot synchronization intact. Remove atlas uploads caused solely by resize separately.
4. Land small, isolated UI and product changes before larger asynchronous ownership changes. Coordinate the latter with existing owner-boundary cards identified below.

## Existing task refinements

These constraints belong in existing cards; they do not duplicate the new work items.

- `kanban/pending/23 native-renderer-operation-tests -refactor.md`: add operation-level evidence for per-slot dirty delivery, failed submissions, allocation counts, and completion-before-reuse while changing grid and NanoVG uploads.
- `kanban/pending/24 nanovg-cpu-batches -refactor.md` and `kanban/pending/31 nanovg-frame-driver -refactor.md`: preserve multiple flushes within one frame and keep native arena ownership separate from shared CPU batch construction.
- `kanban/pending/25 markdown-gpu-planning -refactor.md`: preserve atlas identity and reset behavior if rich-text services are shared. Markdown scrollbar copying is separate host work.
- `kanban/pending/29 kanban-interaction-policy -refactor.md`: retain debounce, focus, selection, and deferred-preview timing when a worker assumes board-load I/O.
- `kanban/pending/05 remote-command-queue -refactor.md`: add coordinator lookup or queue-copy measurements only if profiling substantiates that separate lead.
- `plugins/satview/kanban/pending/03 satview-scene-frame-composition -refactor.md`: measure candidate checks before caching filters. Catalog and filter revisions alone are insufficient because age filtering depends on simulation time; preserve draw/picking revision agreement.
- `plugins/megacity/kanban/pending/01 megacity-worker-ownership -refactor.md`: measure semantic/city-build latency and whole-layout copies before changing immutable request ownership. Existing timing log statements are not recorded timing results.
- `plugins/megacity/kanban/pending/02 megacity-frame-preparation -refactor.md`: coordinate adjacent renderer work, but retain separate ownership of native target lifetime and camera-scene caching.

Completed atlas-reset, SatView pause/cloud, Rezonality include-watching/dimension, and ScoreView final-save cards remain regression constraints. Root `kanban/.draxul-kanban.toml` also names absent files; those names are not tracker cards. The root ice-box directory is absent in this snapshot.

## Unresolved leads and rejected claims

No card is proposed for these without the stated evidence.

- **Terminal delta admission and resyncs:** `remote_terminal_service.cpp:457–488` can encode updates that later overflow a subscriber queue, while Session-stream acknowledgement is separate. Measure `resyncs`, discarded encoded bytes, queue occupancy, subscriber cadence, and multi-subscriber ordering during sustained output. Skipping `take_delta` whenever a queue is nonempty could lose ordering or fairness. Claude’s claim that repeated resync is common was not measured.
- **Worker-side remote snapshot copy and promotion:** `remote_session_coordinator.cpp:943–973` copies the mutable projection and promotes an unconsumed delta to full. Count worker copies, promotions, and client rebuilds under sparse and saturated output before introducing immutable publication ownership. The GUI-side copy has an independent accepted fix below.
- **SatView visible double propagation and filter work:** two samples per 16 ms publication are real, but a 100–250 ms cadence needs direct-SGP4 error checks at high time speeds and picking/ground-track checks. Text-filter caching must separate time-dependent age predicates.
- **Plugin discovery and staging:** temporary discovery occurs once per palette open, not per keypress. Package staging copies assets on load/reload, but inventory size and stall duration are unknown. Measure both before changing them; blanket hard links risk mutable staged generations and cross-volume failure.
- **ScoreView mouse redraw and audio:** unconditional pointer redraw exists, but `WantCaptureMouse` alone would miss hover entry or exit. GUI-thread audio synthesis exists, but underruns and synthesis duration are unmeasured; a larger queue increases input latency.
- **MegaCity semantic scan, route clearing, and broad instancing:** main-thread build and route-grid copies are visible in source, but their frequency and size are unmeasured. Record scan/build/copy time, per-pass draw distributions, and GPU bottlenecks before changing worker, route, or instance layout.
- **Other profile leads:** hidden diagnostics preparation, per-host render submits, enabled per-cell perf scopes, coordinator lookups, VT parser allocations, unused depth targets, exact-size Vulkan buffer growth, SatView labels, Rezonality FFT locking, and MegaCity scanner parallelism need representative counters or captures.
- **Corrected claims:** the SatView “700 MiB” texture figure came from filenames, not decoded asset measurements; scrollback resize copies live rows but initializes its full reserved capacity; the agent observer does not probe the process table every idle second, and its Windows wait differs from POSIX; MegaCity debug state remains useful when the visual overlay is `None`. A universal “under one wake per second” target would ignore real monitor and application deadlines. Claude’s conditional P0 ligature label has no measured stall evidence.
- **Scope exclusion:** a missing PTY EOF wake appears to delay process-exit notice, but that distinct lifecycle bug is outside this optimization plan. Build/test speed has no demonstrated repeated-workflow cost in these reports.

## Accepted work items

### kanban/pending/32 ligature-dirty-run-sweep -perf.md

# Sweep ligature dirty runs once

**Source:** `libs/draxul-runtime-support/src/grid_rendering_pipeline.cpp`  
**Priority/evidence:** P1; static cost model, high confidence. **Reported by:** Claude, Codex. Lines 64–116 expand every dirty cell through neighboring runs, append duplicates, then sort; lines 288–300 call this on a ligature-enabled flush. A homogeneous 300×80 full redraw emits 7.2 million coordinates for 24,000 cells; reused scratch capacity does not remove that work.

- [ ] **Baseline:** Count emitted coordinates, comparisons, scratch capacity, and flush time as row width grows on full redraw and scroll workloads.
- [ ] **Implement:** Sweep or merge touched row intervals and emit each cell once, retaining both adjacent runs when an edit breaks their chain.
- [ ] **Functional safety:** Preserve `////` regrouping, highlights, wide cells, atlas retry/reset, and final cell order.
- [ ] **Compare:** Require emitted work to scale with covered cells, recording before/after flush time without claiming a preset speedup.
- [ ] **Platforms:** Check Vulkan and Metal ligature renders and the scope-appropriate aggregate plus same-cache smoke.
- [ ] **Acceptance:** Identical rendered cells with no quadratic temporary expansion.

### plugins/satview/kanban/pending/04 satview-inactive-propagation -perf.md

# Stop settled inactive satellite propagation

**Source:** `plugins/satview/src/runtime/satview_simulation_worker.cpp`  
**Priority/evidence:** P1; static, high confidence. **Reported by:** Claude, Codex. Lines 310, 376, and 476 retain a 16 ms propagation loop; pause suppresses only the future sample, and `satview_plugin.cpp:272–282` does not pass visibility to the worker. Model and track caches still leave current-position propagation.

- [ ] **Baseline:** Count propagation calls, publications, and worker CPU with a fixed catalog while visible, paused, and hidden.
- [ ] **Implement:** Publish a settled state, then wait for catalog, clock, settings, resume, visibility, or shutdown changes; preserve resume clock anchoring.
- [ ] **Functional safety:** Check settings while paused, restored settings, pause authority, output after reveal, and bounded worker shutdown.
- [ ] **Compare:** Require zero propagations after an unchanged paused or hidden state settles.
- [ ] **Platforms:** Run SatView aggregate, render checks, and same-cache smoke on Windows/Vulkan and macOS/Metal.
- [ ] **Acceptance:** Inactive work stops without delaying a real state change.

### plugins/rezonality/kanban/pending/03 rezonality-idle-project-watch -perf.md

# Avoid rereading unchanged watched projects

**Source:** `plugins/rezonality/src/live_project.cpp`  
**Priority/evidence:** P1; static, high confidence. **Reported by:** Claude, Codex. `ProjectPipeline::fingerprint()` at lines 1238–1277 enumerates, sorts, reads, and hashes every file; lines 1544–1555 repeat after 100 ms even without edits. Its worker protects GUI latency, but idle I/O, CPU, and power scale with project bytes and pane count.

- [ ] **Baseline:** Measure unchanged bytes read, worker CPU, and edit-to-rebuild latency for one and several panes on an asset-rich project.
- [ ] **Implement:** Use native invalidation or a bounded per-file index, with overflow and missed-event rescans.
- [ ] **Functional safety:** Detect same-size edits, arbitrary-suffix shader includes, break/repair, and failed scans; preserve last-good builds.
- [ ] **Compare:** Require no repeated full-content reads while unchanged and bounded edit latency.
- [ ] **Platforms:** Verify native watcher behavior and Rezonality output on Windows/Vulkan and macOS/Metal.
- [ ] **Acceptance:** Idle project contents are not reread every poll.

### plugins/rezonality/kanban/pending/04 rezonality-resize-generation-reuse -perf.md

# Reuse Rezonality GPU assets across viewport changes

**Source:** `plugins/rezonality/src/native_backend_vulkan.cpp`  
**Priority/evidence:** P1; static, high confidence. **Reported by:** Claude, Codex. Lines 2167–2188 and Metal `native_backend_metal.mm:711–731` reject generations on width or height alone, causing source assets and pipelines to be recreated during a drag; Metal also recompiles libraries.

- [ ] **Baseline:** Record generation and pipeline creations, upload bytes, drag-frame p95, and retired-memory peak on an asset-rich scene.
- [ ] **Implement:** Separate viewport-sized targets from compatible immutable assets and pipelines; retain frame-slot retirement.
- [ ] **Functional safety:** Preserve format/render-pass compatibility, checked dimensions, failed-reload rollback, and last-good output.
- [ ] **Compare:** Require no source-asset rebuild on size-only steps; report before/after drag stalls and memory.
- [ ] **Platforms:** Check resize, reload failure, and render goldens on Vulkan and Metal.
- [ ] **Acceptance:** Size changes replace only size-dependent resources.

### kanban/pending/33 app-idle-deadline-wakes -perf.md

# Remove overlay-driven idle app polling

**Source:** `app/app.cpp`  
**Priority/evidence:** P1; static, high confidence. **Reported by:** Claude. `wait_timeout_ms()` at lines 3201–3250 caps waits at 50 ms when any visible host requests periodic wake. Event-driven chrome, palette, toast, and diagnostics hosts inherit or report that condition, so an otherwise idle window repeatedly executes `pump_once()`.

- [ ] **Baseline:** Count app-loop passes and per-process wakeups over 60 seconds with remote-only panes, then with Neovim and active deadlines.
- [ ] **Implement:** Mark event/deadline-driven overlays explicitly, schedule monitor/weather work, and avoid duplicate agent projection per pass.
- [ ] **Functional safety:** Retain prompt remote and Neovim output through the documented macOS SDL lost-wake race; checking a ready flag before sleep alone is insufficient.
- [ ] **Compare:** Report avoidable idle passes and output latency before/after, excluding legitimate deadlines.
- [ ] **Platforms:** Check SDL wake behavior on macOS and Windows, aggregate tests, and same-cache smoke.
- [ ] **Acceptance:** Inert overlays no longer impose a 50 ms loop while background output remains prompt.

### kanban/pending/59 plugin-self-tick-pacing -perf.md

# Pace plugin ticks requested inside a tick

**Source:** `libs/draxul-host/src/plugin_host.cpp`  
**Priority/evidence:** P1; static, high confidence. **Reported by:** Claude. `PluginHost::pump()` at lines 492–515 consumes `tick_pending_`; `request_tick()` at 567–574 immediately wakes SDL. ScoreView requests another tick from its playing pump (`score_runtime.cpp:1741–1742`, `scoreview_plugin.cpp:36–44`), which can spin when minimized and presentation supplies no pacing.

- [ ] **Baseline:** Count ticks, wakeups, redraws, and CPU during visible, minimized, and permitted hidden ScoreView playback.
- [ ] **Implement:** Prevent a tick from immediately rearming itself; use returned deadlines while retaining external asynchronous tick requests.
- [ ] **Functional safety:** Preserve playback cadence, worker completion, visibility policy, and plugin quiescence.
- [ ] **Compare:** Require bounded minimized/hidden tick rate and prompt response to external events.
- [ ] **Platforms:** Exercise ScoreView and generic plugin-host tests on Windows and macOS, then aggregate and smoke.
- [ ] **Acceptance:** Self-requested ticks cannot form an unpaced wake loop.

### kanban/pending/35 terminal-row-scroll-cost -perf.md

# Reduce per-line terminal scroll copying

**Source:** `libs/draxul-terminal-core/src/terminal_core.cpp`  
**Priority/evidence:** P1; static, high confidence. **Reported by:** Claude. `newline()` at lines 542–559 scrolls for each bottom-row LF; `libs/draxul-grid/src/grid.cpp:120–162,581–620` copies and dirty-marks the region cell by cell. A 200×50 region moves roughly 430 KB per output line before downstream rendering.

- [ ] **Baseline:** Replay a fixed 100,000-line capture through terminal/server paths; count cell copies, dirty calls, lines/s, CPU, and output latency.
- [ ] **Implement:** Provide a row-ring or equivalent bulk scroll/dirty operation that preserves intervening VT operations; do not batch blindly across row reads.
- [ ] **Functional safety:** Cover partial regions, wide pairs, attributes/links, alternate screen, scrollback capture, resize, and semantic deltas.
- [ ] **Compare:** Report work per line and end-to-end throughput before/after without weakening backpressure.
- [ ] **Platforms:** Run terminal/core and server integration coverage on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** A bottom-row LF no longer copies and indirectly marks every retained cell.

### kanban/pending/34 interruptible-background-waits -perf.md

# Replace avoidable timed cancellation polls

**Source:** `libs/draxul-control/src/async_frame_stream_posix.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Stream and listener paths poll at 50–100 ms in POSIX and Windows implementations; weather’s worker wakes every 100 ms between fetches (`weather_service.cpp:129–131`). These are idle cancellation checks, distinct from real heartbeats or write deadlines.

- [ ] **Baseline:** Measure idle wakeups by thread/process and shutdown latency with no payload, active connections, and configured weather.
- [ ] **Implement:** Use interruptible waits with owned stop signalling; retain finite write deadlines and a reliable POSIX listener wake mechanism.
- [ ] **Functional safety:** Check handle lifetime, concurrent close, blocked read/accept, stream-shutdown regression, and weather disable.
- [ ] **Compare:** Report avoidable wakes and cancellation time before/after, excluding heartbeats and fetches.
- [ ] **Platforms:** Verify socket/pipe cancellation on macOS and Windows; run aggregate and same-cache smoke.
- [ ] **Acceptance:** Idle threads sleep until work, a real deadline, or cancellation.

### kanban/pending/36 bounded-font-cache-retention -perf.md

# Retain useful font and ligature cache entries

**Source:** `libs/draxul-font/src/ligature_analyser.h`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 184–188 and `font_selector.h:303–308` clear whole caches at their 4,096-entry limit; `grid_rendering_pipeline.cpp:157–202` generates candidate strings on dirty code grids. Repeated working sets above the cap can reshuffle and re-shape the same text.

- [ ] **Baseline:** Count hits, misses, clears, `hb_shape`, and FreeType loads on repeated large code-grid scrolls with varied styles.
- [ ] **Implement:** Choose a bounded retention policy from the observed working set, isolating frequent cell choices from candidate churn.
- [ ] **Functional safety:** Preserve font/size/style invalidation, fallback selection, ligature grouping, and memory bounds.
- [ ] **Compare:** Report steady-state misses and shaping cost; do not assume LRU at the same cap or a preset 64K limit solves sequential replay.
- [ ] **Platforms:** Verify font renders and aggregate/smoke on Windows and macOS.
- [ ] **Acceptance:** Repeated stable text avoids whole-cache churn within an explicit byte budget.

### kanban/pending/37 terminal-frame-serialization -perf.md

# Remove repeated Session terminal JSON materialization

**Source:** `libs/draxul-server/src/session_stream_service.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. `remote_terminal_service.cpp:383–415` builds, sizes, and retains an unused JSON tree; `session_stream_service.cpp:179–187,508–514` serializes a queued frame, reparses it, then dumps it again to assign `frame_serial`. Queue limits bound frames but not repeated full-payload work.

- [ ] **Baseline:** Count parsed/dumped bytes, allocations, server CPU, `steady_frame_bytes`, and `terminal_delivery_latency_us` under the existing multi-pane load test.
- [ ] **Implement:** Remove the dead retained tree and add the serial-bearing envelope without reparsing the complete payload.
- [ ] **Functional safety:** Assign serial at dequeue, preserving priority overtaking, schema, replacement-safe encoding, and conservative byte accounting.
- [ ] **Compare:** Require zero whole-payload writer parses and report before/after latency and throughput.
- [ ] **Platforms:** Check POSIX Session sockets and Windows named pipes, aggregate and smoke.
- [ ] **Acceptance:** Terminal frames retain protocol semantics with fewer full-payload conversions.

### kanban/pending/38 remote-live-snapshot-reference -perf.md

# Avoid a second live snapshot copy on the GUI thread

**Source:** `libs/draxul-host/src/remote_terminal_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. Lines 1204–1208 copy the complete string-bearing snapshot for ordinary live publications, although sparse cells are applied at 1296–1299. Worker publication already owns a snapshot; scrollback composition is the branch that needs a separate value.

- [ ] **Baseline:** Count GUI-thread cell copies, allocations, and pump time for sparse and metadata-only updates at 200×60 and a larger grid.
- [ ] **Implement:** Borrow the live publication within its consumption scope and allocate an owned composed view only for scrollback.
- [ ] **Functional safety:** Preserve snapshot lifetime, resize, highlight exhaustion, selection, and scrollback behavior.
- [ ] **Compare:** Require zero live-branch full copies and record before/after pump latency.
- [ ] **Platforms:** Check remote-terminal host tests and visible output on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** Sparse live updates do not copy the complete grid on the GUI thread.

### kanban/pending/39 slot-aware-grid-uploads -perf.md

# Upload only grid state missing from each frame slot

**Source:** `libs/draxul-renderer/src/renderer_state.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. `copy_to()` at lines 360–368 sends every base cell; lines 62–77 zero a fixed overlay tail. Both Vulkan (`vk_renderer.cpp:203–224`) and Metal (`metal_renderer.mm:106–132`) copy this state on draw. Existing dirty-range methods are unused by the backends.

- [ ] **Baseline:** Count copied, zeroed, flushed, and GPU-uploaded bytes for four panes during a one-cell edit, cursor blink, and unrelated-pane animation.
- [ ] **Implement:** Track content generation and missing ranges per frame slot, with separate cursor/overlay state and full initialization after reallocation.
- [ ] **Functional safety:** Cover two-slot alternation, overlay shrink, resize, aborted frame, buffer reuse, and stale-range retirement.
- [ ] **Compare:** Require zero unchanged base-cell bytes after slot priming; report frame CPU and bytes before/after.
- [ ] **Platforms:** Run Vulkan and Metal snapshots, renderer tests, aggregate and smoke.
- [ ] **Acceptance:** Both slots receive edits exactly once without copying unchanged grids every draw.

### kanban/pending/40 renderer-sizing-overlay-storage -perf.md

# Avoid unchanged relayout and fixed overlay storage

**Source:** `libs/draxul-renderer/src/renderer_state.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. `set_grid_size()` at lines 88–143 relayouts on unchanged dimensions; `relayout()` fills cells and a 16,384-cell overlay. Chrome, palette, and toast call sizing repeatedly; each grid handle also reserves overlay space across CPU and GPU slots.

- [ ] **Baseline:** Count relayouts, CPU fills, RSS, and allocated GPU bytes with multiple panes and chrome/sidebar visible.
- [ ] **Implement:** Skip sizing only when dimensions **and padding** are unchanged; grow overlay storage on demand within its current cap.
- [ ] **Functional safety:** Audit callers that relied on implicit clearing; preserve overlay instance bounds, cursor, and layout after actual size changes.
- [ ] **Compare:** Require zero steady-state relayouts and report memory before/after.
- [ ] **Platforms:** Verify chrome/toast/palette snapshots on Vulkan and Metal, aggregate and smoke.
- [ ] **Acceptance:** Unchanged sizing does no full-grid work; overlay capacity follows use.

### kanban/pending/41 lazy-terminal-scrollback-storage -perf.md

# Allocate scrollback rows as they become live

**Source:** `libs/draxul-terminal-core/src/scrollback_buffer.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude; Codex treated impact as an open lead. `resize()` at lines 49–116 constructs `capacity × columns` cells even when no row is live. Default capacity is 10,000 rows; server admission already reserves against capacity, so this is memory/startup work rather than a reason to relax its budget.

- [ ] **Baseline:** Measure allocated cells, server RSS, pane-create time, and resize peak for one and multiple idle panes within the existing reservation limit.
- [ ] **Implement:** Allocate bounded row blocks on demand while retaining capacity accounting and stable row access semantics.
- [ ] **Functional safety:** Preserve oldest-first iteration, overflow, live-row resize copying, snapshots, and strong failure behavior.
- [ ] **Compare:** Report idle RSS and create/resize latency before/after; avoid configurations rejected by the existing admission budget.
- [ ] **Platforms:** Run terminal/server scrollback coverage on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** An empty pane does not construct all reserved scrollback cells.

### kanban/pending/42 atlas-upload-on-grid-resize -perf.md

# Keep grid resize separate from atlas invalidation

**Source:** `libs/draxul-host/src/grid_host_base.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. `apply_grid_size()` at lines 196–219 forces a full shared atlas upload; `grid_rendering_pipeline.cpp:328–343` stages the full 2048² RGBA atlas even if no glyph pixels changed. Atlas dirty-region and reset-generation handling already exist.

- [ ] **Baseline:** Count queued, staged, and GPU atlas bytes during a warmed-atlas multi-pane resize.
- [ ] **Implement:** Invalidate grid geometry without forcing unchanged texture content; retain first-use, font/reset, and device-recreation uploads.
- [ ] **Functional safety:** Check glyph generation, new glyphs during resize, font-size changes, and completed atlas-reset regression.
- [ ] **Compare:** Require zero unchanged texture bytes on geometry-only resize.
- [ ] **Platforms:** Verify text after resize on Vulkan and Metal, aggregate and smoke.
- [ ] **Acceptance:** Grid size changes do not reupload the whole shared atlas unnecessarily.

### kanban/pending/43 coalesced-nvim-flush-preparation -perf.md

# Prepare one presentation for already queued Neovim redraws

**Source:** `libs/draxul-host/src/nvim_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Codex. `pump()` drains notifications at lines 194–201; each `flush` prepares cells and glyphs at 464–475, while App presents after pumping (`app.cpp:2490–2494,2536–2539`). A queued macro can prepare the same area repeatedly before one presentation.

- [ ] **Baseline:** Replay 1, 10, and 100 complete queued redraw transactions; count preparations, dirty cells, and pump time.
- [ ] **Implement:** Apply semantic events in order but coalesce GPU preparation across already available complete transactions.
- [ ] **Functional safety:** Cover first-flush startup, resize, atlas reset, cursor state, and a trailing incomplete transaction.
- [ ] **Compare:** Require one final preparation for a complete queued batch with identical final cells and cursor.
- [ ] **Platforms:** Check Neovim integration and Vulkan/Metal render output, aggregate and smoke.
- [ ] **Acceptance:** Intermediate queued flushes do not trigger redundant presentation preparation.

### kanban/pending/44 incremental-nvim-packet-decoding -perf.md

# Stop rebuilding fragmented Neovim packet prefixes

**Source:** `libs/draxul-nvim/src/rpc.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Codex; Claude listed the frequency as unresolved. Lines 467–478 retry decoding from the same prefix after an incomplete read; `mpack_codec.cpp:146–171` allocates a declared string or binary body before discovering it is incomplete. An 8 MiB payload over a 256 KiB read buffer is a concrete bounded workload.

- [ ] **Baseline:** Feed identical large packets in 4, 64, and 256 KiB fragments; count initialized bytes, decoded nodes, allocations, and decode time.
- [ ] **Implement:** Check body availability before allocation and retain incremental container progress or an equivalent linear decode plan.
- [ ] **Functional safety:** Preserve incomplete-versus-invalid handling, depth/size caps, response ordering, and EOF behavior.
- [ ] **Compare:** Require work proportional to payload rather than repeated prefix length.
- [ ] **Platforms:** Check native pipe transports on Windows and macOS, RPC aggregate and smoke.
- [ ] **Acceptance:** Fragmentation no longer repeatedly initializes or decodes completed prefixes.

### kanban/pending/45 nvim-notification-move-and-wake -perf.md

# Move decoded Neovim notifications and coalesce wakes

**Source:** `libs/draxul-nvim/src/rpc.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 356–384 copy the decoded notification parameter tree into a queue and invoke the wake callback for each notification. Full-screen redraw strings and vectors are therefore deep-copied before GUI processing; burst wakes are not coalesced.

- [ ] **Baseline:** Count copied bytes, allocations, and SDL wakes for a fixed burst of redraw notifications.
- [ ] **Implement:** Move validated notification payloads and use an empty-to-nonempty wake flag acknowledged on drain.
- [ ] **Functional safety:** Preserve request/response routing, queue bounds, EOF wake, and races around drain and requeue.
- [ ] **Compare:** Require no decoded-tree deep copy and bounded wakes per burst.
- [ ] **Platforms:** Run RPC and Neovim integration on Windows/macOS, aggregate and smoke.
- [ ] **Acceptance:** Bursty notifications reach the GUI with fewer copies and wake events.

### kanban/pending/46 shared-nvim-clipboard-refresh -perf.md

# Avoid per-host full clipboard polling

**Source:** `libs/draxul-host/src/nvim_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 186–203 and 558–576 read/copy the OS clipboard every 500 ms per Neovim host, including hidden hosts. The gate limits frequency but not copies of a large clipboard across panes.

- [ ] **Baseline:** Count clipboard reads, copied bytes, and GUI time with several hosts and a fixed 5 MiB clipboard.
- [ ] **Implement:** Use a shared main-thread cache driven by clipboard/focus events, with a bounded fallback where event delivery needs one.
- [ ] **Functional safety:** Preserve external changes, Neovim clipboard requests, `clipboard_set`, focus transitions, and reader-thread access.
- [ ] **Compare:** Require reads to follow changes rather than host count during idle.
- [ ] **Platforms:** Check SDL clipboard behavior on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** Multiple idle Neovim panes do not each copy unchanged clipboard contents.

### kanban/pending/47 hidden-nvim-frame-suppression -perf.md

# Suppress rendering requests from hidden Neovim grids

**Source:** `libs/draxul-host/src/grid_host_base.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. `flush_grid()` at lines 222–240 requests a frame, and cursor updates at 397–454 can do likewise, without a presentation-visibility gate. Hidden remote terminals already suspend delivery; hidden Neovim hosts continue ingesting and can trigger visible-window renders.

- [ ] **Baseline:** Count foreground frames while a hidden Neovim pane outputs and blinks.
- [ ] **Implement:** Track presentation visibility, suppress hidden frame requests and unfocused blink work, then invalidate once on reveal.
- [ ] **Functional safety:** Keep semantic ingestion, title/atlas state, cursor correctness, and reveal convergence.
- [ ] **Compare:** Require no output-driven foreground render for a wholly hidden host.
- [ ] **Platforms:** Check tab/Space switching and render output on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** Hidden Neovim activity does not render the visible tree needlessly.

### kanban/pending/48 palette-dirty-rebuild -perf.md

# Rebuild the command palette only when its view changes

**Source:** `app/command_palette_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 77–100 copy view state, render the palette grid, and update cells on every pump while open. Filtering is already cached, but unrelated input/output still rebuilds unchanged presentation.

- [ ] **Baseline:** Count palette cell builds and pump time while open and idle, then during output in another pane.
- [ ] **Implement:** Track dirty revisions for query, selection, viewport, font/atlas, background, and open/close.
- [ ] **Functional safety:** Preserve key/text input, selection movement, clipping, and atlas reset.
- [ ] **Compare:** Require zero unchanged cell builds after the palette settles.
- [ ] **Platforms:** Check palette snapshots on Vulkan and Metal, aggregate and smoke.
- [ ] **Acceptance:** Unrelated app pumps do not rebuild the same palette cells.

### kanban/pending/49 bounded-toast-grid -perf.md

# Size toast geometry to the toast

**Source:** `app/toast_host.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 121–149 use a full-window grid for a small toast, and its animation schedules roughly 33 ms redraws. Dirty upload improvements reduce bytes but do not remove full-window instances and background quads.

- [ ] **Baseline:** Count instances, upload bytes, and toast frame time at increasing window sizes for a fixed message.
- [ ] **Implement:** Use toast-bounded grid and viewport geometry.
- [ ] **Functional safety:** Preserve corner anchoring, multiple-toast stacking, fade, clipping, DPI, and glyph atlas behavior.
- [ ] **Compare:** Require fixed-toast draw work to remain bounded as the window grows.
- [ ] **Platforms:** Check Vulkan and Metal toast snapshots, aggregate and smoke.
- [ ] **Acceptance:** A small toast no longer draws a window-sized grid.

### kanban/pending/50 vulkan-extent-only-resource-reuse -perf.md

# Reuse compatible Vulkan resources during extent changes

**Source:** `libs/draxul-renderer/src/vulkan/vk_renderer.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 756–813 recreate pipelines, descriptor pools/sets, and render-pass-related resources during frame-resource rebuilds on resize. Extent changes alone do not necessarily change their compatibility.

- [ ] **Baseline:** Count pipeline, descriptor, and render-pass creations and drag-frame p95 during a fixed Windows resize sequence.
- [ ] **Implement:** Reuse compatible invariant resources; recreate when format, layout, or device requirements change.
- [ ] **Functional safety:** Preserve transactional failure, swapchain out-of-date, minimize/restore, and dependent Markdown pass compatibility.
- [ ] **Compare:** Require invariant creation counts to remain flat for extent-only steps.
- [ ] **Platforms:** Run Windows Vulkan validation and render checks; inspect shared contracts and macOS smoke.
- [ ] **Acceptance:** Extent-only resize does not repeatedly rebuild compatible pipelines.

### kanban/pending/51 markdown-scrollbar-layout-copy -perf.md

# Draw the Markdown scrollbar without copying the document

**Source:** `modules/markdown/draxul-markdown/src/markdown_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. Lines 432–449 call `document_with_scrollbar()`; lines 555–587 copy the full layout and sort decorations before visible-row culling. Wheel or drag scrolling thus copies offscreen rows, text, and decorations.

- [ ] **Baseline:** Measure allocations, copied rows, and scroll CPU at fixed viewport for 1,000 and 100,000 layout rows.
- [ ] **Implement:** Read the retained layout and append separately clipped scrollbar draw commands in the proper paint order.
- [ ] **Functional safety:** Check mixed-height rows, tables, edge scrolling, decoration order, and scrollbar interaction.
- [ ] **Compare:** Require zero complete-document copies per scroll and report latency before/after.
- [ ] **Platforms:** Check Markdown output on Vulkan/Metal, aggregate and smoke.
- [ ] **Acceptance:** Scroll work no longer scales with offscreen document size through copying and sorting.

### kanban/pending/52 rich-text-style-atlas-sharing -perf.md

# Share rich-text services across style variants

**Source:** `libs/draxul-font/src/rich_text_service.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 62–87 key a full `TextService` by size and bold/italic, while style flags are already passed at use. Each service owns a 2048² RGBA CPU atlas (`glyph_cache.cpp:172–178`) and can cause a corresponding GPU atlas.

- [ ] **Baseline:** Count services, CPU/GPU atlases, RSS, and VRAM for a mixed-style Markdown or Kanban preview.
- [ ] **Implement:** Share by compatible point size while passing style at shaping/rasterization and maintaining correct atlas identity.
- [ ] **Functional safety:** Check metrics, fallback faces, bold/italic glyphs, atlas IDs, font reset, and upload generations.
- [ ] **Compare:** Report atlas count and memory before/after with identical text rendering.
- [ ] **Platforms:** Verify mixed-style output on Vulkan and Metal, aggregate and smoke.
- [ ] **Acceptance:** Style variants do not each reserve a complete redundant atlas.

### kanban/pending/53 kanban-worker-board-reload -perf.md

# Move full Kanban board reload off the GUI thread

**Source:** `modules/kanban/draxul-kanban/src/kanban_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. Lines 265–281 and 466–484 reload after a debounced invalidation; `kanban_store.cpp:255–262,655–660,714–727` opens and scans every card across discovered boards. Debounce limits repeats but leaves a GUI-thread N-file operation.

- [ ] **Baseline:** Inject delayed reads in a 5,000-card workspace; count GUI-thread file opens and typing latency in another pane.
- [ ] **Implement:** Run one owned, coalescing scan with generation-checked publication; consider a bounded unchanged-priority cache.
- [ ] **Functional safety:** Preserve moves, focus/preview, failures, native watcher invalidations, and shutdown.
- [ ] **Compare:** Require no GUI-thread board file scan and report reload latency before/after.
- [ ] **Platforms:** Check Windows and macOS watchers, Kanban aggregate and same-cache smoke.
- [ ] **Acceptance:** Large-board refresh does not block unrelated GUI input.

### kanban/pending/54 kanban-metadata-index -perf.md

# Index Kanban metadata filenames during ordering

**Source:** `modules/kanban/draxul-kanban/src/kanban_store.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Codex. Lines 228–252 search the complete card vector for each saved filename, approaching N² comparisons for a fully listed lane. The separate GUI-worker change moves this CPU work but does not remove it.

- [ ] **Baseline:** Count comparisons and reload CPU for 1,000, 5,000, and 10,000 cards in varied orders.
- [ ] **Implement:** Build a temporary owning filename-to-index map before moving cards.
- [ ] **Functional safety:** Preserve stale and duplicate metadata names and original order of unmatched cards; avoid keys into moved strings.
- [ ] **Compare:** Require lookup growth proportional to card count and identical final order.
- [ ] **Platforms:** Check ordering on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** Metadata ordering no longer repeatedly scans the lane.

### kanban/pending/55 nanovg-frame-allocation-reuse -perf.md

# Reuse NanoVG native frame allocations

**Source:** `libs/draxul-nanovg/backend/src/nanovg_vk.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Vulkan lines 1442–1461, 1500–1539, and 1616–1633 allocate buffers/framebuffer/descriptor state on flush; Metal `nanovg_mtl.mm:795–816` creates two buffers for each nonempty flush. Chrome and ScoreView exercise these paths repeatedly.

- [ ] **Baseline:** Count native allocations, flushes, and frame CPU after warmup in chrome and a long ScoreView Flow.
- [ ] **Implement:** Use per-slot growable arenas supporting multiple flushes in one frame; cache compatible framebuffer/descriptor state.
- [ ] **Functional safety:** Preserve completion retirement, resize invalidation, aborted flushes, and no overwrite of in-flight data.
- [ ] **Compare:** Require warm steady frames to avoid repeated native object creation where capacity suffices.
- [ ] **Platforms:** Check Vulkan and Metal NanoVG snapshots, aggregate and smoke.
- [ ] **Acceptance:** Stable flush workloads reuse native capacity safely.

### kanban/pending/56 remote-scrollback-page-work -perf.md

# Avoid duplicate anchored scrollback retrieval and full presentation rebuild

**Source:** `libs/draxul-client/src/remote_session_coordinator.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 1028–1046 can request the current offset and then the anchored offset after new rows; `remote_terminal_host.cpp:1225–1228` rebuilds the whole display grid while scrolled back. Continuing output therefore repeats round trips and cell work for a fixed history view.

- [ ] **Baseline:** Count scrollback requests, copied cells, rebuild cells, and delivery latency while output continues behind a fixed anchor.
- [ ] **Implement:** Request an anchored page atomically and update only changed visible rows where its semantics allow.
- [ ] **Functional safety:** Preserve ring overflow, resize, attributes, selection, and independent multi-client viewports.
- [ ] **Compare:** Report requests/rebuilds and GUI latency before/after.
- [ ] **Platforms:** Check remote terminal integration on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** Stable anchored views do not repeatedly fetch or rebuild unchanged content.

### kanban/pending/57 selection-same-cell-overlay-work -perf.md

# Skip selection overlay rebuilds for unchanged cell endpoints

**Source:** `libs/draxul-host/src/selection_manager.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 48–56 rebuild during every active drag; lines 262–324 can emit many selected overlay cells. Pixel motion within one terminal cell can repeat the same bounded but large work.

- [ ] **Baseline:** Count overlay builds and cells emitted for a large selection while many pointer events stay within one cell.
- [ ] **Implement:** Return early when endpoint and grid revision are unchanged, with explicit invalidation on content/resize.
- [ ] **Functional safety:** Preserve drag threshold, truncation notice, selection changes, and drag end.
- [ ] **Compare:** Require no repeated overlay build for unchanged endpoints.
- [ ] **Platforms:** Check mouse selection on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** Sub-cell pointer movement does not regenerate identical overlays.

### kanban/pending/58 fair-terminal-pump-budget -perf.md

# Bound terminal pumping ahead of server commands

**Source:** `libs/draxul-server/src/server_kernel_lifecycle.cpp`  
**Priority/evidence:** P2; static, medium confidence. **Reported by:** Claude, narrowed in verification. Lines 418–437 pump terminal output before pending control and stream commands at 554; `server_terminal_runtime.cpp:406–424` feeds returned chunks without a processing-time budget. Multiple sustained-output panes can delay interactive commands.

- [ ] **Baseline:** Measure keypress and delivery p95/p99, terminal throughput, and retained bytes with several busy panes.
- [ ] **Implement:** Bound each terminal’s work and dispatch commands fairly while retaining unprocessed data and VT order.
- [ ] **Functional safety:** Preserve synchronized-output atomicity, PTY backpressure, shutdown, and per-terminal fairness.
- [ ] **Compare:** Require bounded command latency without an unbounded queue or major throughput regression.
- [ ] **Platforms:** Run server/client integration on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** A busy output pump cannot indefinitely defer ready commands.

### plugins/satview/kanban/pending/05 satview-texture-startup -perf.md

# Remove blocking SatView texture setup from first render

**Source:** `plugins/satview/src/render/satview_render_vk.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 515–545 decode and immediately upload Earth, Moon, Sun, and Milky Way images during first render; Metal has analogous synchronous setup at `satview_render.mm:135–151`. Vulkan cloud replacement also waits idle at line 681. Actual decoded bytes and stall duration remain unmeasured.

- [ ] **Baseline:** Record actual decoded dimensions/bytes, first-pane frame time, upload waits, and forced cloud-refresh hitch for one and two panes.
- [ ] **Implement:** Decode off-thread and publish bounded frame-slot uploads, using existing solid fallbacks until ready.
- [ ] **Functional safety:** Preserve cancellation, slot retirement, resource failure, and completed Metal cloud-refresh lifetime behavior.
- [ ] **Compare:** Report first-frame and refresh stalls and peak memory before/after; assess mips/compression separately.
- [ ] **Platforms:** Verify images and cloud updates on Vulkan and Metal, SatView aggregate and smoke.
- [ ] **Acceptance:** Image decode and per-image immediate waits no longer block first-frame presentation.

### plugins/satview/kanban/pending/06 satview-catalog-publication -perf.md

# Load cached SatView catalogs without blocking pane creation

**Source:** `plugins/satview/src/services/satview_catalog_service.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 317–417 read and parse GP/SATCAT caches synchronously; `satview_runtime.cpp:826–827` transfers a by-value catalog onward. This affects opening or restoring panes; refresh workers do not protect this initial cached-load path.

- [ ] **Baseline:** Measure create-instance GUI time, file reads, and catalog copies with populated caches and one/multiple panes.
- [ ] **Implement:** Publish an immutable catalog from one owned worker with a bounded latest result and loading presentation.
- [ ] **Functional safety:** Preserve corrupt-cache diagnostics, network fallback, settings changes, and worker shutdown.
- [ ] **Compare:** Require no catalog file reads on the GUI creation path and report startup latency/copies.
- [ ] **Platforms:** Check SatView startup on Windows and macOS, aggregate and smoke.
- [ ] **Acceptance:** A large cached catalog does not block pane creation.

### plugins/satview/kanban/pending/07 satview-lunar-track-uploads -perf.md

# Rebuild only lunar-dependent SatView tracks

**Source:** `plugins/satview/src/runtime/satview_runtime.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 1217–1226 trigger reanchoring when any lunar track is present; lines 1248–1270 recompose and upload the whole track vector, including stable Earth tracks. Other dirty/source/window gates are bypassed by that condition.

- [ ] **Baseline:** Count composed and uploaded vertices for mixed Earth/lunar tracks during time and camera changes and while paused.
- [ ] **Implement:** Separate stable and lunar-dependent geometry/revisions without an unmeasured visual tolerance.
- [ ] **Functional safety:** Preserve map wrap, selection, ground projection, Moon windows, and track invalidation.
- [ ] **Compare:** Require unchanged Earth-track work to remain flat when only lunar anchoring changes.
- [ ] **Platforms:** Check SatView output on Vulkan and Metal, aggregate and smoke.
- [ ] **Acceptance:** Lunar reanchoring does not upload unrelated stable tracks.

### plugins/satview/kanban/pending/08 satview-resize-target-lifetime -perf.md

# Retire SatView resize targets without device-wide waits

**Source:** `plugins/satview/src/render/satview_render_vk.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 1057–1099 wait for device idle and recreate all frame targets on exact size changes; debug difference resources are also required when unused. Dragging a divider can repeat this work.

- [ ] **Baseline:** Count waits, target creations, allocated bytes, and resize-frame p95 through a scripted drag.
- [ ] **Implement:** Retire old size generations through completed slots and allocate debug attachments on demand.
- [ ] **Functional safety:** Preserve correct current viewport sizing, transactional allocation failure, debug toggle, and slot lifetime.
- [ ] **Compare:** Require no device-wide wait for ordinary extent changes and report peak memory.
- [ ] **Platforms:** Run Vulkan validation and compare Metal resize/output behavior, SatView aggregate and smoke.
- [ ] **Acceptance:** Resize reuses compatible resources without showing stale-sized frames.

### plugins/rezonality/kanban/pending/05 rezonality-build-activation-move -perf.md

# Avoid deep-copying Rezonality builds at activation

**Source:** `plugins/rezonality/src/runtime_controller.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. Lines 47–62 copy `ShaderBuild`, including vectors of shaders, models, and pixels, before replacing active storage. The same copy also occurs for active-build resize recovery.

- [ ] **Baseline:** Count copied bytes, allocations, and peak memory during an asset-heavy reload and resize.
- [ ] **Implement:** Move a successfully prepared pending build; retain the owned active build for compatible recreation.
- [ ] **Functional safety:** Keep pending-rejection rollback, diagnostics/status, generation identity, and last-good output.
- [ ] **Compare:** Require no whole-build activation copy and report peak memory before/after.
- [ ] **Platforms:** Check Vulkan and Metal reload/resize paths, aggregate and smoke.
- [ ] **Acceptance:** Activation transfers ownership without duplicating immutable asset payloads.

### plugins/rezonality/kanban/pending/06 rezonality-vulkan-staging-retirement -perf.md

# Retire one-time Rezonality Vulkan texture staging

**Source:** `plugins/rezonality/src/native_backend_vulkan.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. Lines 556–559 and 634–638 retain mapped staging buffers after one-time texture upload at 1875–1881 and 1934–1973; lines 2206–2223 retire whole generations only. A 4096² RGBA buffer represents 64 MiB of staging by arithmetic, not an observed allocation.

- [ ] **Baseline:** Measure static staging bytes after initial frames, after reload, and at generation retirement.
- [ ] **Implement:** Retire completed one-time upload buffers with their submitting frame slot; retain streaming audio buffers.
- [ ] **Functional safety:** Cover aborted recording, failed submit, in-flight completion, and visible texture integrity.
- [ ] **Compare:** Require static staging to return to steady baseline after upload completion.
- [ ] **Platforms:** Run Windows/Vulkan validation and Rezonality renders; inspect Metal’s separate upload policy.
- [ ] **Acceptance:** Uploaded static textures do not retain their one-time mapped staging.

### plugins/rezonality/kanban/pending/07 rezonality-decoded-asset-reuse -perf.md

# Reuse decoded Rezonality assets across shader-only edits

**Source:** `plugins/rezonality/src/live_project.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. `build_candidate()` at lines 1316–1326 reloads models and lines 1357–1385 decode images for every candidate, including a `.frag` edit. The watcher detects the edit, but no decoded-asset cache separates unchanged inputs.

- [ ] **Baseline:** Count import/decode calls, reload latency, and peak memory for repeated shader edits in an asset-rich project.
- [ ] **Implement:** Cache bounded immutable decoded assets by source/options and complete external dependencies.
- [ ] **Functional safety:** Detect same-size edits and model-referenced files; preserve break/repair and last-good generations.
- [ ] **Compare:** Require zero unchanged model/image decodes for shader-only edits without unbounded cache growth.
- [ ] **Platforms:** Check reloads on Vulkan and Metal, aggregate and smoke.
- [ ] **Acceptance:** Shader edits rebuild shader work without reimporting unchanged assets.

### plugins/rezonality/kanban/pending/08 rezonality-editor-idle-refresh -perf.md

# Suppress unchanged Rezonality editor refresh work

**Source:** `plugins/rezonality/integrations/neovim/rezonality.nvim/lua/rezonality/init.lua`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 29–32 default to 500 ms diagnostics and 2 s registry refresh; lines 650–684 reread/rebuild and spawn `draxul pane list`; lines 1071–1072 install the repeating timer. Every installed editor instance can do this while unchanged.

- [ ] **Baseline:** Count file reads, `vim.diagnostic.set` calls, spawned commands, and idle CPU per editor instance.
- [ ] **Implement:** Gate unchanged diagnostics by reliable file revision/notification; retain bounded registry refresh unless topology events replace it.
- [ ] **Functional safety:** Check diagnostics arrival, pane disappearance, file replacement, and timer teardown.
- [ ] **Compare:** Require no unchanged diagnostics reset while preserving prompt topology updates.
- [ ] **Platforms:** Check Neovim integration on Windows and macOS with the corresponding CLI.
- [ ] **Acceptance:** Idle editors avoid repeated diagnostics work without losing pane changes.

### plugins/megacity/kanban/pending/04 megacity-lcov-idle-redraw -perf.md

# Stop idle redraw for static MegaCity LCOV coverage

**Source:** `plugins/megacity/product/draxul-megacity/src/megacity_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 1886–1894 allow an active LCOV overlay to bypass idle return, then schedule the movement tick; `megacity_plugin.cpp:317–320` requests redraw for the deadline. LCOV import is static between explicit changes, while live perf overlays legitimately refresh.

- [ ] **Baseline:** Count ticks, full draws, and CPU after settling with LCOV on, overlay off, and live perf overlay on.
- [ ] **Implement:** Schedule periodic refresh only for live data, movement, or settling; invalidate once on LCOV import/settings.
- [ ] **Functional safety:** Preserve hover, camera movement, LCOV updates, and live perf display.
- [ ] **Compare:** Require no repeated settled LCOV redraw and report idle CPU before/after.
- [ ] **Platforms:** Check MegaCity on Vulkan and Metal, product aggregate and smoke.
- [ ] **Acceptance:** Static coverage display becomes render-on-change.

### plugins/megacity/kanban/pending/05 megacity-shadow-target-resize -perf.md

# Retain fixed-resolution MegaCity shadows during pane resize

**Source:** `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. `ensure_gbuffer_targets()` at lines 2851–2924 waits idle, destroys all targets, then reallocates fixed-resolution shadow cascades on an extent change. Metal `codeviz_render.mm:988–1018` has the same combined lifetime. The shadow maps’ size is independent of pane size.

- [ ] **Baseline:** Count target creations/bytes, device waits, resize p95, and peak VRAM with fixed shadow settings.
- [ ] **Implement:** Separate per-slot shadow lifetime from viewport-sized G-buffer lifetime and retire old extent targets safely.
- [ ] **Functional safety:** Preserve frame-count changes, shadows, resize failure, and in-flight slot ownership; do not share shadows across slots without barrier/overlap analysis.
- [ ] **Compare:** Require no shadow reallocation on extent-only drag.
- [ ] **Platforms:** Check Vulkan and Metal resize/render goldens, MegaCity aggregate and smoke.
- [ ] **Acceptance:** Pane resizing replaces only size-dependent targets.

### plugins/megacity/kanban/pending/06 megacity-camera-snapshot-reuse -perf.md

# Reuse MegaCity world records on camera-only frames

**Source:** `plugins/megacity/product/draxul-megacity/src/megacity_host.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. Lines 1499–1505 mark the scene dirty on camera movement; 1559–1573 publish a full snapshot. `scene_snapshot_builder.cpp:245–338,464–493,536` revisits entities, material lookups, strings, and sort order during pan/orbit.

- [ ] **Baseline:** Count full ECS snapshot builds and GUI CPU during a fixed camera replay as entity count grows.
- [ ] **Implement:** Retain object/material/mesh records; recompute camera-dependent matrices, bounds, depth/order, and selection opacity.
- [ ] **Functional safety:** Preserve transparent order, labels, hover/selection, and model/material invalidation.
- [ ] **Compare:** Require zero full ECS rebuilds after priming for camera-only frames.
- [ ] **Platforms:** Check MegaCity Vulkan and Metal views, aggregate and smoke.
- [ ] **Acceptance:** Camera motion updates camera-dependent state without reconstructing the world.

### plugins/megacity/kanban/pending/07 megacity-camera-frustum-culling -perf.md

# Cull offscreen MegaCity objects before camera draws

**Source:** `plugins/megacity/product/draxul-megacity/src/scene_snapshot_builder.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Codex. Lines 327–338 retain all renderables; Vulkan `codeviz_render_vk.cpp:3712–3718,3913–3925` and Metal `codeviz_render.mm:1700–1765` record camera-pass draws without frustum rejection. Hardware clipping comes after CPU recording and vertex work.

- [ ] **Baseline:** Hold visible content fixed while increasing offscreen objects; count camera-pass draws and frame CPU/GPU time.
- [ ] **Implement:** Derive conservative object bounds and a camera-visible list; keep shadow-caster visibility independent.
- [ ] **Functional safety:** Check boundary objects, custom meshes, labels, transparency, and camera motion.
- [ ] **Compare:** Require camera draws to follow intersecting objects, reporting before/after frame cost.
- [ ] **Platforms:** Verify Vulkan and Metal render goldens, MegaCity aggregate and smoke.
- [ ] **Acceptance:** Offscreen objects are not recorded for main camera passes.

### plugins/megacity/kanban/pending/08 megacity-debug-state-demand -perf.md

# Build MegaCity debug state only for consumers

**Source:** `plugins/megacity/product/draxul-megacity/src/megacity_host.cpp`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude, narrowed. Lines 1070–1085 already skip when UI panels are hidden, but visible collapsed panels still cause `metrics_overlay_controller.cpp:156–168` and `live_city_metrics.cpp:318–353` to traverse model/layers and construct keys. Debug data can be useful with overlay `None`.

- [ ] **Baseline:** Count debug builds, allocations, and frame CPU with panels hidden, collapsed, and expanded during motion.
- [ ] **Implement:** Pass actual panel demand or cache by model, mode, runtime, and LCOV revision.
- [ ] **Functional safety:** Keep expanded debug panels live and correct with overlay `None`.
- [ ] **Compare:** Require no unchanged collapsed-panel rebuild and report before/after cost.
- [ ] **Platforms:** Check MegaCity UI on Vulkan and Metal, aggregate and smoke.
- [ ] **Acceptance:** Debug state is produced when a consumer needs it, not merely because panel UI exists.

### plugins/megacity/kanban/pending/09 megacity-point-shadow-revision -perf.md

# Skip unchanged MegaCity point-shadow cube passes

**Source:** `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp`  
**Priority/evidence:** P2; static, medium confidence. **Reported by:** Claude. Lines 3572–3660 record valid cube faces every frame without a revision gate. Camera-only movement need not change fixed light/caster shadows; pass eligibility varies, so a universal draw-count multiplier is unsupported.

- [ ] **Baseline:** Count cube-pass draws and GPU duration during camera-only movement and scene/light edits.
- [ ] **Implement:** Track per-slot initialized shadow revisions and invalidate on caster, opacity/material, light, or target changes.
- [ ] **Functional safety:** Preserve in-flight slot writes, selection modes, shadow correctness, and recreation.
- [ ] **Compare:** Require unchanged camera-only frames to skip cube work while changed shadows update.
- [ ] **Platforms:** Verify Vulkan and inspect/cover equivalent Metal cube recording, MegaCity aggregate and smoke.
- [ ] **Acceptance:** Point shadows render only when their inputs or target require it.

### plugins/megacity/kanban/pending/10 megacity-metal-texture-upload -perf.md

# Batch MegaCity Metal material texture preparation

**Source:** `plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render.mm`  
**Priority/evidence:** P2; static, medium-high confidence. **Reported by:** Claude. Lines 669–683 create, commit, and wait for a mip-generation command buffer per material texture during cold setup. Vulkan records equivalent preparation without a per-texture wait.

- [ ] **Baseline:** Count texture submissions/waits and cold-start frame p95 as material texture count grows.
- [ ] **Implement:** Batch upload and mip work in the owned preparation command stream, retaining inputs until completion.
- [ ] **Functional safety:** Preserve mip sampling, resource lifetime, missing-material fallback, and error reporting.
- [ ] **Compare:** Require no per-material blocking wait and report startup latency before/after.
- [ ] **Platforms:** Check Metal output and lifetime; retain Vulkan behavior and run MegaCity aggregate/smoke.
- [ ] **Acceptance:** Cold material setup avoids serial GPU waits per texture.

### plugins/scoreview/kanban/pending/02 scoreview-height-only-viewport -perf.md

# Avoid paged reengraving on height-only resize

**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Codex; Claude found adjacent layout work. Lines 347–352 dirty layout for any size change, while engraving options at 574–580 depend on width, scale, and zoom. Lines 582–633 render and interpret each page after invalidation.

- [ ] **Baseline:** Count `RedoLayout`, SVG renders, and frame p95 during fixed-width vertical drag on a long score.
- [ ] **Implement:** Invalidate engraving only for its actual inputs; clamp scroll and repaint on height change.
- [ ] **Functional safety:** Preserve width/DPI/zoom, page bounds, scroll, and presentation changes.
- [ ] **Compare:** Require zero reengraves after initial layout for height-only changes.
- [ ] **Platforms:** Check ScoreView snapshots and aggregate/smoke on Windows and macOS.
- [ ] **Acceptance:** Vertical resizing does not rebuild paged notation.

### plugins/scoreview/kanban/pending/03 scoreview-offscreen-flow-culling -perf.md

# Cull offscreen monolithic Flow and Clock notation

**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_render_nvg.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. `score_presentation.cpp:327–333` scissors output, but this file’s lines 157–180 and 264–269 still replay complete path and glyph outlines. Active playback uses roughly 16 ms frames. Normal windowed Roll and paged views already bound more work.

- [ ] **Baseline:** Count submitted paths/glyphs and frame p95 as monolithic score length grows with a fixed viewport.
- [ ] **Implement:** Store conservative interpreted-object bounds and reject offscreen NanoVG submissions.
- [ ] **Functional safety:** Preserve paint order, transformed glyphs, strokes/accidentals at clip edges, and replacement invalidation.
- [ ] **Compare:** Require submissions to follow visible notation, with identical pixels at boundaries.
- [ ] **Platforms:** Check Vulkan and Metal ScoreView output, product aggregate and smoke.
- [ ] **Acceptance:** Distant Flow/Clock notation is not replayed each frame.

### plugins/scoreview/kanban/pending/04 scoreview-progress-persistence -perf.md

# Move ScoreView progress saves out of the playback pump

**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_session_controller.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude, Codex. `score_runtime.cpp:1703–1713` flushes at dirty bar transitions; controller lines 105–124 serialize the complete model, and `progress_store.cpp:43–69` writes, flushes, closes, and renames synchronously. Bar gating limits frequency but not storage latency on the playback path.

- [ ] **Baseline:** Measure pump p95/p99 and written bytes on long progress with injected slow storage.
- [ ] **Implement:** Use one coalescing worker with bounded immutable latest versions and explicit final drain.
- [ ] **Functional safety:** Preserve latest-state persistence, failure reporting, shutdown, and completed final-save behavior.
- [ ] **Compare:** Require file I/O to leave the interactive pump, reporting before/after latency.
- [ ] **Platforms:** Check persistence and teardown on Windows/macOS, ScoreView aggregate and smoke.
- [ ] **Acceptance:** Dirty bar transitions cannot synchronously stall playback for disk writes.

### plugins/scoreview/kanban/pending/05 scoreview-engraver-completion-wake -perf.md

# Wake ScoreView when background engraving completes

**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp`  
**Priority/evidence:** P2; static, high confidence. **Reported by:** Claude. Lines 1748–1749 schedule a 16 ms deadline merely while the engraver is busy; `scoreview_plugin.cpp:318–320` converts that deadline into redraw. Paused views can render repeatedly just to poll for completion.

- [ ] **Baseline:** Count paused busy frames, tick CPU, and completion-to-display latency for a long engraving job.
- [ ] **Implement:** Publish one thread-safe completion wake and consume the latest result once; retain true playback deadlines.
- [ ] **Functional safety:** Preserve cancellation, latest-wins behavior, plugin lifetime, and hidden presentation policy.
- [ ] **Compare:** Require no repeated paused redraw solely for `engrave_busy()`.
- [ ] **Platforms:** Check ScoreView on Windows/macOS, aggregate and smoke.
- [ ] **Acceptance:** Background completion triggers presentation without polling frames.

### plugins/scoreview/kanban/pending/06 scoreview-paged-layout-worker -perf.md

# Keep required full-page engraving off the interactive pump

**Source:** `plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp`  
**Priority/evidence:** P2; static, medium confidence. **Reported by:** Claude. Legitimate width, zoom, and view changes still reach synchronous relayout at line 1609, render/interpret every page at 582–633, and repeat timemap-related work. Height-only invalidation should be removed first.

- [ ] **Baseline:** Measure pump p95, page SVG/timemap calls, and allocations for long-piece zoom/view bursts.
- [ ] **Implement:** Use a single latest-wins owned engine worker or adapt the existing engraver; retain prior pages until a current result publishes.
- [ ] **Functional safety:** Avoid concurrent access to mutable Verovio state; preserve full-source versus rolling-slice behavior and cancellation.
- [ ] **Compare:** Report interactive stalls and redundant jobs before/after; parse timemap once per accepted result.
- [ ] **Platforms:** Check ScoreView behavior on Windows/macOS, aggregate and smoke.
- [ ] **Acceptance:** Required reengraving does not monopolize the GUI pump.
