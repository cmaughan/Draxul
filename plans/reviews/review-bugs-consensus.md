# Bug consensus

Read both indexed reviews and checked their findings against current source. Verification was static; no files were changed and no builds or tests were run.

**C** = Claude Fable 5.1; **A** = GPT-6 Astra. The complete cards below contain verified locations, triggers, fixes, attribution, and acceptance criteria for newly accepted work.

**Severity rulings:** Both reviewers identify the Rezonality worker exception and Vulkan capture overflow. Merge those duplicates. For capture, accept Astra’s CRITICAL rating over Claude’s MEDIUM: growth can cause an out-of-bounds CPU read. Other confirmed crashes, lifetime violations, integer overflow, and invalid GPU resource use are likewise CRITICAL, regardless of their original labels.

**Confirmed findings, in severity order:**

| Severity | Finding | Reporter | Disposition |
|---|---|---|---|
| CRITICAL | Wrong-typed control protocol version terminates listener | C #1 | Card below |
| CRITICAL | Wrong-typed layout fields escape stream dispatch | C #2 | Card below |
| CRITICAL | Scene parsing and filesystem exceptions escape live-project worker | C #4; A #1 | Merged card below |
| CRITICAL | Shader activation reads a destroyed pending build | C #3 | Card below |
| CRITICAL | Vulkan capture reads using replacement swapchain dimensions | C #31; A #2 | Merged card below; Metal extension unconfirmed |
| CRITICAL | SatView replaces a joinable refresh thread | C #5 | Card below |
| CRITICAL | Metal NanoVG initialization failure double-deletes context | C #6 | Card below |
| CRITICAL | Indented Codex features header produces an invalid iterator | C #20 | Card below |
| CRITICAL | Config reload invalidates the action string used by trace logging | C #35 | Card below |
| CRITICAL | Multipart score slicing indexes shorter parts out of bounds | C #43 | Card below |
| CRITICAL | PCB routing dimensions overflow or permit excessive allocation | C #26 | Card below |
| CRITICAL | SatView overwrites or destroys GPU buffers still in flight | C #15 | Card below |
| CRITICAL | MegaCity rewrites shared mesh pools still in flight | C #37 | Card below |
| CRITICAL | MegaCity updates descriptors after binding them | C #24 | Card below |
| CRITICAL | Rezonality image format disagrees with uploaded pixel storage | C #16 | Card below |
| CRITICAL | Rezonality diagnostic serialization throws on invalid UTF-8 | C #17 | Card below |
| CRITICAL | Server filesystem exceptions escape startup/request handling | C #18, #29 | Combined card below |
| CRITICAL | SatView’s redundant asset existence check can terminate its worker | C #42 | Card below |
| CRITICAL | Kanban iteration errors escape the UI; construction errors are hidden | C #23 | Card below |
| CRITICAL | SatView reuses partially initialized HDR targets | C #39 | Card below |
| CRITICAL | Rezonality retries an incompletely initialized vertex buffer | C #45a | Card below |
| CRITICAL | ScoreView progress deserialization throws on structurally invalid JSON | C #13 | Existing progress-recovery work |
| CRITICAL | ScoreView serialization throws on non-UTF-8 text | C #44 | Existing progress-recovery work |
| CRITICAL | Late plugin callbacks can race host retirement | C #28 | Existing hot-reload work; narrowed trigger |
| CRITICAL | MegaCity default-root lookup throws when cwd is unavailable | C #38 | Existing degraded-initialization work |
| HIGH | Concurrent SatView cache writers can remove a successfully published cache | A #3 | Card below |
| HIGH | PTY shutdown loses the output-backpressure wake | C #9 | Card below |
| HIGH | Session-client stop/fallback loses an untimed worker wake | C #10 | Card below |
| HIGH | Failed final Vulkan submission leaves an unsignaled frame fence | C #7 | Card below |
| HIGH | Unsupported CSI prefixes execute unrelated terminal commands | C #8 | Card below |
| HIGH | Failed local host restart leaves an orphaned tree leaf | C #12 | Card below; plugin trigger narrowed |
| HIGH | Showing ScoreView does not restore keyboard input | C #14 | Card below |
| HIGH | Windows Neovim shutdown blocks the UI | C #34 | Card below |
| HIGH | Windows OSC 7 paths remain unsuitable for native cwd reuse | C #36 | Card below |
| HIGH | Legacy remote-pane visibility/stop transitions lose worker wakes | C #11, #27 | Existing hidden-terminal work |
| MEDIUM | Plugin storage cleanup overwrites the original error | C #21 | Card below |
| MEDIUM | Minimum-only integer clamping permits narrowing overflow | C #22 | Card below |
| MEDIUM | Compacted dependency routes use the wrong original pair | C #25 | Card below |
| MEDIUM | ConPTY publishes a stale PID after exit | C #32 | Card below |
| MEDIUM | SDL event registration checks the wrong failure sentinel | C #33 | Card below |
| MEDIUM | Rejected ephemeris rows overwrite accepted metadata | C #40 | Card below |
| MEDIUM | SatView picking applies the marker limit before horizon filtering | C #41 | Card below |
| MEDIUM | Failed Rezonality model-texture creation leaks partial resources | C #45b | Included with transactional GPU initialization below |

**Existing work; no duplicate cards:**

- **ScoreView progress recovery:** `plugins/scoreview/product/draxul-score-learn/src/player_model.cpp:537`, `:554`, `:556`, and `:620` throw on malformed keys or values; `player_model.cpp:486` and `piece_analysis.cpp:1062` use strict serialization. A malformed progress file or imported non-UTF-8 title can escape initialization/save callbacks. Validate into temporary state, return a recoverable load failure, and sanitize or reject invalid text before serialization. Covered by `plugins/scoreview/kanban/ice-box/17 scoreview-progress-crash-safety -test.md`.
- **Late plugin callbacks:** `libs/draxul-host/src/plugin_host.cpp:715` loads a host pointer before dereferencing its plain `active_callback_context_` at `:716`. A late callback that passes the atomic checks before retirement can race pointer mutation or host destruction. Quiescing compliant workers reduces exposure; the advertised retained-token protection for broken late callbacks still lacks a lifetime barrier. Protect callback entry through completion and drain active callbacks before destruction; atomics alone do not pin the host. Covered by `kanban/pending/41 dynamic-plugin-hot-reload -feature.md`.
- **MegaCity unavailable cwd:** `plugins/megacity/product/draxul-megacity/src/megacity_host.cpp:237` calls throwing `current_path()` when no source is supplied. Deleting the working directory before opening the plugin can escape initialization. Use the error-code overload and return a degraded result. Covered by `plugins/megacity/kanban/ice-box/12 megacity-degraded-init -test.md`.
- **Legacy terminal wakes:** `libs/draxul-host/src/remote_terminal_host.cpp:459` changes visibility outside the waiter’s mutex; `libs/draxul-client/src/remote_session_coordinator.cpp:186` similarly changes the stop predicate. Racing the untimed suspended wait can strand a shown pane or its teardown worker. Change predicates under the corresponding mutex and notify afterward. Covered by the resume/deadlock/leak requirements in `kanban/pending/34 hidden-remote-terminal-suspension -bug.md`.

**Dropped or narrowed claims:**

- **C #19, local PTY writes freezing the UI:** not confirmed in the current production graph. Concrete PTY writes run through `ServerTerminalRuntime`’s writer thread (`server_terminal_runtime.cpp:170`); the retained local-host abstraction does not establish the claimed concrete UI-thread caller.
- **C #30, orphaned ConPTY children:** the general claim overlooks `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` at `conpty_process.cpp:788`. The job-creation/assignment fallback warrants reproduction, but detached teardown alone does not establish this finding.
- **C #31, Metal capture overflow:** dimension sources differ, but the cited path does not recreate resources between recording and synchronous readback. No concrete production interleaving was established; retain only the proven Vulkan defect.
- **C #45c, Objective-C exception from invalid Metal text:** the unchecked nullable conversion is present, but the claimed exception outcome was not established. No accepted card.
- **C #12:** missing plugins normally receive `UnavailableHost`; the confirmed restart defect concerns failing non-plugin hosts, including Neovim.
- **C #7:** device loss does not necessarily produce an infinite fence wait because Vulkan waits can return device-loss errors. The confirmed defect is unsignaled-fence reuse after a failed submission. Resetting the fence *after* successful submission is not a valid fix.

**Dependencies:** Implement stream exception containment with the server filesystem fixes. Coordinate final-submit recovery with `kanban/pending/21 vulkan-chunk-flush-failure-state -bug.md`, which concerns mid-frame transitions. Share wakeup regression techniques across PTY, session-client, and existing hidden-terminal work. Rezonality’s worker, diagnostic, activation, and GPU-resource fixes should share a break/repair reload scenario. SatView’s worker race and shared-cache race are independent and both require fixes.

Cards use the requested sequence beginning at `00`; the trusted runner can normalize occupied priorities. These are proposed contents, not created files.

### kanban/pending/05 control-version-type-validation -bug.md

# Reject malformed control protocol versions safely

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`libs/draxul-control/src/control_codec.cpp:180` reads `version` with throwing `value<int>()` before authentication. Sending `{"version":"1"}` or a null version can escape `handle_frame` and terminate a transport listener thread’s process.

**Investigation**

- [ ] Exercise malformed versions through POSIX and Windows control transports.

**Fix strategy**

- [ ] Validate the version’s type and range before conversion.
- [ ] Contain frame-processing exceptions and return a structured failure.

**Acceptance criteria**

- [ ] String, null, container, and oversized versions fail without disconnecting healthy clients or terminating the process.
- [ ] Valid protocol versions continue working.

### kanban/pending/06 layout-stream-exception-containment -bug.md

# Contain malformed layout requests on the session stream

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`libs/draxul-server/src/topology_service.cpp:208` converts `dry_run` before checking its type; `:333` converts `direction` without validation. An authenticated stream request with `"dry_run":"yes"` can throw through `session_stream_service.cpp:305` and terminate the server.

**Investigation**

- [ ] Exercise wrong-typed layout fields over both control and persistent-stream paths.

**Fix strategy**

- [ ] Validate fields before conversion.
- [ ] Catch dispatch exceptions per command, preserving correlation and replay behavior.

**Acceptance criteria**

- [ ] Malformed layouts return structured errors without changing topology.
- [ ] Other sessions remain usable after each rejected request.
- [ ] Coordinate exception handling with the server filesystem card.

### kanban/pending/07 rezonality-worker-exception-recovery -bug.md

# Recover from live-project parsing and filesystem exceptions

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1; GPT-6 Astra

`plugins/rezonality/src/live_project.cpp:413` and `:568` accept malformed numeric tokens before calling `std::stof`; `:1237` invokes the build without an exception barrier. Autosaving `field_of_view: -` or `scale: (1, -` terminates the worker’s process. Fingerprinting also contains throwing filesystem operations.

**Investigation**

- [ ] Exercise incomplete numbers, out-of-range values, and filesystem failures during watching.

**Fix strategy**

- [ ] Use checked numeric parsing and error-code filesystem operations.
- [ ] Convert build/watch exceptions into failed diagnostics while continuing the worker.

**Acceptance criteria**

- [ ] Invalid edits preserve the last valid scene.
- [ ] Repairing the file successfully activates a later generation on both backends.

### kanban/pending/09 rezonality-pending-build-lifetime -bug.md

# Preserve build lifetime while publishing activation status

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/rezonality/src/rezonality_plugin.cpp:3191` and `:3494` reset `pending_build`, then read its destroyed object through `desired`. A successful pending shader activation reaches this lifetime violation on Metal and Vulkan.

**Investigation**

- [ ] Trace `desired` ownership for pending activation and resize recreation.

**Fix strategy**

- [ ] Build status from the committed `active_build`, or capture required values before resetting the pending build.
- [ ] Audit both success and failure branches for invalidated aliases.

**Acceptance criteria**

- [ ] Repeated successful reloads produce correct counts without lifetime violations.
- [ ] Failed reloads and resize recreation retain valid active state.

### kanban/pending/10 vulkan-capture-resize-bounds -bug.md

# Keep capture dimensions tied to the recorded image

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1; GPT-6 Astra

`libs/draxul-renderer/src/vulkan/vk_renderer.cpp:1363` recreates the swapchain before readback. `:409` then uses its new extent to read the old capture allocation. Growing the surface can read beyond mapped storage; shrinking misinterprets the image.

**Investigation**

- [ ] Exercise capture with presentation-triggered swapchain recreation, growing and shrinking.

**Fix strategy**

- [ ] Retain recorded capture dimensions and allocation bounds through completion.
- [ ] Complete readback before recreation where appropriate, or explicitly cancel invalid captures.

**Acceptance criteria**

- [ ] Captures never read beyond their allocation.
- [ ] Returned dimensions describe the captured image.
- [ ] Inspect Metal dimension handling without assuming the same resize mechanism.

### kanban/pending/14 satview-refresh-thread-retirement -bug.md

# Retire completed refresh threads before replacement

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/services/satview_catalog_service.cpp:602` and `satview_cloud_service.cpp:233` assign new threads after checking only `refresh_in_flight_`. Completion between pump and start checks clears that flag while `worker_` remains joinable, causing `std::terminate`.

**Investigation**

- [ ] Force completion between result collection and the next refresh decision in both services.

**Fix strategy**

- [ ] Model completed-but-unjoined workers explicitly.
- [ ] Consume completion and join the previous worker before replacing it.

**Acceptance criteria**

- [ ] Automatic and manual refresh races cannot replace a joinable thread.
- [ ] Completion results are applied once, and cancellation remains responsive.

### kanban/pending/16 metal-nanovg-failure-ownership -bug.md

# Delete the Metal NanoVG context exactly once

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`libs/draxul-nanovg/backend/src/nanovg_mtl.mm:1072` deletes the backend during NanoVG cleanup, and `:1105` deletes it again after initialization failure. The pinned [NanoVG implementation](https://raw.githubusercontent.com/memononen/nanovg/ce3bf745eb2d2dbc14a50bf2446783f691ac4353/src/nanovg.c) invokes that cleanup after renderer or atlas initialization fails.

**Investigation**

- [ ] Inject renderer, atlas, and initial NanoVG allocation failures.

**Fix strategy**

- [ ] Give backend allocation ownership to one layer across successful creation, failed creation, and normal deletion.
- [ ] Preserve cleanup when NanoVG fails before installing its callbacks.

**Acceptance criteria**

- [ ] Every failure frees resources once, with no double-free or leak.
- [ ] Normal initialization and teardown remain valid under sanitizers.

### kanban/pending/18 codex-indented-features-header -bug.md

# Locate indented features headers without invalid iterators

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`app/agent_integration.cpp:420` searches untrimmed lines after detecting a trimmed `[features]` header. With `  [features]` and no hooks key, `:421` inserts through `end() + 1`, invoking undefined behavior.

**Investigation**

- [ ] Cover indented headers with missing and existing hooks settings.

**Fix strategy**

- [ ] Record the matching header index during the initial scan.
- [ ] Insert only through a validated position while preserving unrelated settings.

**Acceptance criteria**

- [ ] Installation safely enables hooks for indented and unindented headers.
- [ ] Repeated installation remains idempotent and preserves valid TOML.

### kanban/pending/19 config-reload-action-lifetime -bug.md

# Own action text across configuration reload

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`app/input_dispatcher.cpp:389` retains an action view into keybindings. Executing `reload_config` replaces their storage at `app/app.cpp:686`, after which trace logging reads the dangling view at `input_dispatcher.cpp:399`.

**Investigation**

- [ ] Dispatch a reload binding with input trace logging enabled and changed keybindings.

**Fix strategy**

- [ ] Copy the selected action before execution, or otherwise retain its owning storage.
- [ ] Inspect other action dispatch paths for equivalent reentrant invalidation.

**Acceptance criteria**

- [ ] Trace-enabled reload has no invalid memory access.
- [ ] Logs retain the dispatched action name, and new bindings work afterward.

### kanban/pending/24 scoreview-multipart-slice-bounds -bug.md

# Validate every part before slicing a score window

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/scoreview/product/draxul-score-learn/src/source_slicer.cpp:256` validates against the first part’s bar count, then indexes every part at `:286` and `:314`. A shorter subsequent part causes out-of-bounds access.

**Investigation**

- [ ] Construct multipart scores with shorter and empty later parts.

**Fix strategy**

- [ ] Validate each requested measure and attribute-state index per part.
- [ ] Reject incompatible windows or define an explicit safe missing-measure policy.

**Acceptance criteria**

- [ ] Unequal part lengths return a controlled result without invalid access.
- [ ] Equal-length multipart windows preserve their notation and attribute state.

### kanban/pending/31 pcbview-routing-cell-budget -bug.md

# Bound PCB routing dimensions before arithmetic and allocation

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/pcbview/src/autorouter.cpp:224` narrows computed dimensions, and `:230` multiplies them as `int`. Positive finite dimensions accepted at `board_model.cpp:115` can overflow or allocate gigabytes; initialization exceptions escape `pcbview_plugin.cpp:96`.

**Investigation**

- [ ] Evaluate extreme board dimensions, fine grid spacing, and layer counts.

**Fix strategy**

- [ ] Check conversions and multiplication before narrowing.
- [ ] Enforce a documented total routing-memory/cell budget.
- [ ] Contain initialization failures at the plugin boundary.

**Acceptance criteria**

- [ ] Oversized inputs fail diagnostically before overflow or excessive allocation.
- [ ] Supported boards still route, and a rejected board cannot terminate Draxul.

### kanban/pending/36 satview-vulkan-stream-buffer-lifetime -bug.md

# Keep SatView vertex streams valid across in-flight frames

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/render/satview_render_vk.cpp:833` rewrites shared mapped stream buffers while previous frames may read them. Growth destroys those buffers at `:801` before their GPU users finish.

**Investigation**

- [ ] Trace every stream buffer’s use across buffered frames and revision changes.

**Fix strategy**

- [ ] Use frame-owned buffers or immutable upload generations.
- [ ] Retire replaced allocations only after all referencing frame slots complete.

**Acceptance criteria**

- [ ] Same-capacity updates and growth produce no GPU lifetime or synchronization errors.
- [ ] Rapid marker/track updates render correctly with multiple frames in flight.
- [ ] Review equivalent Metal ownership.

### kanban/pending/38 megacity-vulkan-mesh-pool-lifetime -bug.md

# Protect MegaCity mesh pools from in-flight rewrites

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp:1006` rewrites persistent mesh pools on each preparation. A rebuild fitting existing capacity overwrites geometry still referenced by an earlier frame; retirement currently handles growth only.

**Investigation**

- [ ] Trace same-capacity scene updates through all buffered frame slots.

**Fix strategy**

- [ ] Use frame-owned pools or immutable geometry generations with completion-based retirement.
- [ ] Avoid rewriting unchanged geometry.

**Acceptance criteria**

- [ ] Same-size, shrinking, and growing rebuilds preserve in-flight data.
- [ ] Vulkan synchronization validation remains clean.
- [ ] Review Metal’s corresponding mesh lifetime.

### kanban/pending/39 megacity-ao-descriptor-recording -bug.md

# Select AO descriptors before recording their use

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/megacity/product/draxul-codeviz-renderer/src/codeviz_render_vk.cpp:3799` and `:3953` update a descriptor set already bound at `:3667`. Raw-AO debug mode therefore invalidates recorded commands under the [Vulkan descriptor-update rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkUpdateDescriptorSets.html).

**Investigation**

- [ ] Trace binding 3 through GBuffer, scene, and debug passes.

**Fix strategy**

- [ ] Select and write descriptors before their first recorded binding.
- [ ] Use separate descriptor sets where passes require different images.

**Acceptance criteria**

- [ ] Raw and denoised AO views use their intended images.
- [ ] Toggling debug modes produces no descriptor-update validation errors.
- [ ] Preserve Metal’s corresponding visual behavior.

### kanban/pending/42 rezonality-image-storage-format -bug.md

# Match image formats to uploaded pixel storage

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/rezonality/src/live_project.cpp:1142` normalizes HDR formats only. An LDR image with `format: rgba16f` retains that override, while `rezonality_plugin.cpp:1187` allocates four bytes per pixel and `:2513` copies the full image into a wider format.

**Investigation**

- [ ] Exercise LDR images with float/depth overrides and HDR images with explicit formats.

**Fix strategy**

- [ ] Derive format from decoded storage, perform an explicit conversion, or reject incompatible overrides.
- [ ] Validate upload size and row stride on both backends.

**Acceptance criteria**

- [ ] No accepted image upload reads beyond its source storage.
- [ ] Invalid combinations report diagnostics and preserve the active generation.

### kanban/pending/43 rezonality-diagnostic-utf8 -bug.md

# Make diagnostic publication tolerant of invalid UTF-8

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/rezonality/src/diagnostics.cpp:33` truncates strings by byte count, while `:180` serializes with strict UTF-8 handling. A multibyte character crossing the message limit, or invalid compiler bytes, can throw through publication and terminate the application.

**Investigation**

- [ ] Exercise invalid compiler text and multibyte messages at each truncation boundary.

**Fix strategy**

- [ ] Sanitize invalid UTF-8 and truncate complete characters.
- [ ] Make diagnostic serialization failure recoverable.

**Acceptance criteria**

- [ ] Publication always produces valid bounded JSON or a controlled error.
- [ ] Broken shader diagnostics cannot destroy the active scene or prevent later recovery.

### kanban/pending/44 server-filesystem-exception-recovery -bug.md

# Handle unavailable server paths without process termination

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`libs/draxul-server/src/server_terminal_runtime.cpp:266` uses throwing `current_path()` for an unspecified cwd. `libs/draxul-server/src/server_kernel.cpp:298` uses throwing `exists()` during restore preparation. A deleted cwd or inaccessible checkpoint path can escape server execution.

**Investigation**

- [ ] Exercise terminal startup with a deleted cwd and server startup with an inaccessible checkpoint path.

**Fix strategy**

- [ ] Use error-code filesystem operations and explicit failure/fallback policy.
- [ ] Preserve startup failure reporting and contain request exceptions with the layout-stream fix.

**Acceptance criteria**

- [ ] Filesystem failures produce actionable errors without terminating unrelated sessions.
- [ ] Startup failure is reported through the normal discovery/failure channel.

### kanban/pending/45 satview-asset-probe-exception -bug.md

# Remove the throwing redundant SatView asset probe

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/core/satview_catalog.cpp:691` calls throwing `exists()` but returns the same path either way. An inaccessible asset ancestor can therefore terminate initialization or a refresh worker before ordinary file-read error handling runs.

**Investigation**

- [ ] Trace bundled-catalog loading during initialization and refresh.

**Fix strategy**

- [ ] Remove the redundant probe and rely on the existing read-error result.
- [ ] Verify asset-access errors remain contained at worker boundaries.

**Acceptance criteria**

- [ ] Missing or inaccessible bundled assets produce a controlled diagnostic.
- [ ] Refresh failure preserves usable catalog state and does not terminate Draxul.

### kanban/pending/46 kanban-directory-scan-errors -bug.md

# Propagate Kanban scan errors without replacing the board

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`modules/kanban/draxul-kanban/src/kanban_store.cpp:289` and `:315` check iterator-construction errors only inside loops that never run on failure. Their range-for increments can also throw. Permission loss or filesystem failure during reload can hide a board or escape the UI callback.

**Investigation**

- [ ] Inject iterator construction and advancement failures for roots and columns.

**Fix strategy**

- [ ] Check construction errors immediately and advance through `increment(error_code)`.
- [ ] Return scan failures before committing a replacement board.

**Acceptance criteria**

- [ ] Failed reloads preserve the last valid board and expose an error.
- [ ] Mid-scan failures never escape the UI callback.

### kanban/pending/47 satview-hdr-target-transaction -bug.md

# Publish HDR targets only after complete initialization

**Severity:** CRITICAL  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/render/satview_render_vk.cpp:1048` considers matching vector size and first-target dimensions sufficient. A later failure at `:1076`, `:1094`, or `:1107` leaves that condition true, allowing subsequent frames to use incomplete targets.

**Investigation**

- [ ] Inject failures after the first target succeeds and within later attachment/descriptor creation.

**Fix strategy**

- [ ] Build a temporary complete target set before publication, or clear every partial set on failure.
- [ ] Make validity checks cover all required frame resources.

**Acceptance criteria**

- [ ] Failed creation cannot lead to null framebuffer or descriptor use.
- [ ] A subsequent successful retry recovers cleanly without leaks.
- [ ] Preserve Metal failure behavior.

### kanban/pending/48 rezonality-gpu-initialization-rollback -bug.md

# Roll back incomplete Rezonality GPU resource creation

**Severity:** CRITICAL; includes a MEDIUM resource leak  
**Reported by:** Claude Fable 5.1

`plugins/rezonality/src/rezonality_plugin.cpp:1000` treats a non-null vertex buffer as ready after allocation/binding failure at `:1033`. Retrying a candidate can use an unbacked buffer. Separately, model-texture failure at `:1245` or `:1258` leaks resources held only by the local object at `:1522`.

**Investigation**

- [ ] Inject vertex memory, bind/map, sampler, and upload allocation failures.

**Fix strategy**

- [ ] Construct resources transactionally with scoped ownership.
- [ ] Publish readiness only after complete initialization; clear partial backend state.

**Acceptance criteria**

- [ ] Failed candidates preserve the active generation.
- [ ] Retrying succeeds without invalid buffer use or cumulative GPU leaks.

### kanban/pending/49 satview-concurrent-cache-publication -bug.md

# Preserve SatView caches during concurrent publication

**Severity:** HIGH  
**Reported by:** GPT-6 Astra

`plugins/satview/src/services/satview_catalog_service.cpp:105` gives concurrent writers the same temporary path. After one publishes it, another rename can fail and the fallback at `:124` removes the newly published cache.

**Investigation**

- [ ] Interleave two catalog services writing the same payload and metadata paths.

**Fix strategy**

- [ ] Use unique sibling temporary files.
- [ ] Replace destinations atomically without deleting the last valid cache on failure.
- [ ] Keep payload/metadata publication coherent across writers.

**Acceptance criteria**

- [ ] Concurrent refreshes leave a complete usable cache.
- [ ] Failed replacement preserves the previous destination on Windows and macOS.
- [ ] Offline startup remains functional afterward.

### kanban/pending/50 pty-backpressure-shutdown-wakeup -bug.md

# Synchronize PTY stop predicates with output waits

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-terminal-process/src/unix_pty_process.cpp:298` and `conpty_process.cpp:854` change the reader stop predicate outside `output_mutex_`. Shutdown racing the backpressure wait can lose notification and block a join indefinitely. Unix `request_close()` also omits waking that condition variable.

**Investigation**

- [ ] Force shutdown between predicate evaluation and blocking with a full output queue.

**Fix strategy**

- [ ] Change the relevant stop predicate under the waiter’s mutex.
- [ ] Notify the backpressure condition on every stop/close path.

**Acceptance criteria**

- [ ] Saturated readers exit within a bounded deadline on both platforms.
- [ ] Normal backpressure still preserves output ordering and contents.

### kanban/done/51 session-client-worker-wakeup -bug.md

# Prevent lost session-client stop and fallback wakes

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-client/src/remote_session_client.cpp:59` changes `stopping_`, and `:291` changes `externally_fed_`, without the mutex used by the untimed wait at `:811`. A race can hang shutdown or prevent legacy polling from starting.

**Investigation**

- [ ] Force each transition between predicate evaluation and entry into the external-feed wait.

**Fix strategy**

- [ ] Update wait predicates under `mutex_`, then notify.
- [ ] Audit startup and fallback transitions for consistent synchronization.

**Acceptance criteria**

- [ ] An idle externally fed client always stops promptly.
- [ ] Legacy fallback begins without needing an unrelated command.
- [ ] Coordinate regression techniques with existing hidden-terminal work.

### kanban/pending/52 vulkan-final-submit-fence-recovery -bug.md

# Recover frame ownership after final submission failure

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-renderer/src/vulkan/vk_renderer.cpp:1345` resets the frame fence before submission, but failure at `:1349` leaves it unsignaled and associated with the image. A subsequent `begin_frame()` at `:884` can wait for work never submitted.

**Investigation**

- [ ] Inject final-submit failure and inspect fence, image, semaphore, and frame ownership.

**Fix strategy**

- [ ] Enter a defined renderer-failure or recovery state that never waits on nonexistent work.
- [ ] Repair or retire associated synchronization objects safely; do not reset a fence after submitting it.

**Acceptance criteria**

- [ ] Submission failure cannot cause indefinite fence reuse or invalid synchronization.
- [ ] Coordinate with `kanban/pending/21 vulkan-chunk-flush-failure-state -bug.md`.

### kanban/pending/53 terminal-csi-prefix-dispatch -bug.md

# Dispatch CSI commands by their complete prefix

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-terminal-core/src/terminal_core_csi.cpp:129` recognizes only `?`, while dispatch at `:330` ignores that distinction for cursor restore. Keyboard-protocol sequences such as `CSI ? u` move the cursor; `CSI > 4;2 m` changes SGR, and DA2 receives the wrong response.

**Investigation**

- [ ] Replay prefixed keyboard, attribute, and device-query sequences alongside ordinary CSI commands.

**Fix strategy**

- [ ] Parse private markers and intermediates separately from numeric parameters.
- [ ] Dispatch supported combinations explicitly and ignore unsupported combinations.

**Acceptance criteria**

- [ ] Unsupported extensions cannot mutate cursor or rendition state accidentally.
- [ ] Existing cursor, SGR, and supported device-query behavior remains correct.

### kanban/pending/54 failed-local-host-restart-state -bug.md

# Keep a valid pane after local host restart failure

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`app/pane_manager.cpp:409` erases the old host before recreation. A failed non-plugin initialization at `:1405` removes its pane ID but leaves the tree leaf and launch options. Restarting Neovim after removing its executable can leave a pane that cannot be closed or restarted normally.

**Investigation**

- [ ] Fail recreation after an existing local host has been shut down.

**Fix strategy**

- [ ] Install a recoverable unavailable placeholder or perform complete structural rollback.
- [ ] Keep tree, host, pane identity, launch options, and input routing consistent.

**Acceptance criteria**

- [ ] The failed pane remains closable and retryable.
- [ ] Session snapshotting and other panes continue working.
- [ ] Preserve existing missing-plugin placeholder behavior.

### kanban/pending/55 scoreview-keyboard-resume -bug.md

# Restore keyboard input when ScoreView becomes visible

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`plugins/scoreview/product/draxul-scoreview/src/score_runtime.cpp:341` clears the input rig when hidden. The show branch at `:347` excludes keyboard input while resuming playback, so returning to a keyboard-driven Roll/Gate pane rejects keys and can record missed notes.

**Investigation**

- [ ] Hide and show keyboard-driven Roll and Gate modes with background playback disabled.

**Fix strategy**

- [ ] Restore the requested input rig for every applicable non-Clock mode before resuming.
- [ ] Preserve input lease and background-playback semantics.

**Acceptance criteria**

- [ ] Keyboard notes work immediately after showing the pane.
- [ ] Visibility transitions do not create artificial missed-note progress.
- [ ] Other input modes still reacquire correctly.

### kanban/pending/56 windows-nvim-nonblocking-shutdown -bug.md

# Move Windows Neovim process waits off the UI thread

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-nvim/src/nvim_process.cpp:233` and `:237` perform consecutive two-second waits. `libs/draxul-host/src/nvim_host.cpp:182` calls shutdown synchronously, so closing an unresponsive Neovim pane can freeze the interface.

**Investigation**

- [ ] Trace process, RPC reader, and pipe ownership during Windows pane closure.

**Fix strategy**

- [ ] Transfer bounded process termination/reaping to a safe background owner.
- [ ] Preserve cancellation, handle lifetime, and idempotent shutdown.

**Acceptance criteria**

- [ ] Closing an unresponsive Neovim pane leaves other panes responsive.
- [ ] The child is eventually reaped without leaked handles or worker access to destroyed state.
- [ ] Preserve the existing nonblocking POSIX behavior.

### kanban/pending/57 windows-osc7-path-normalization -bug.md

# Normalize Windows OSC 7 paths before reuse

**Severity:** HIGH  
**Reported by:** Claude Fable 5.1

`libs/draxul-terminal-core/src/terminal_core_csi.cpp:689` retains the URI’s leading slash for Windows drive paths. Metadata is reused as a working directory, while `app/app.cpp:3177` recognizes only `/` when naming tabs. Windows OSC 7 paths can therefore produce invalid launch directories or incorrect names.

**Investigation**

- [ ] Trace drive, UNC, percent-encoded, and backslash-containing paths through metadata, naming, and pane launch.

**Fix strategy**

- [ ] Convert file URIs to platform-appropriate filesystem paths.
- [ ] Extract display basenames using platform-aware rules while preserving POSIX behavior.

**Acceptance criteria**

- [ ] Windows drive and supported UNC paths round-trip into valid working directories.
- [ ] Tab names show the intended basename.
- [ ] POSIX paths remain unchanged semantically.

### kanban/pending/58 plugin-storage-error-preservation -bug.md

# Preserve plugin storage errors during temporary-file cleanup

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`libs/draxul-host/src/plugin_host.cpp:190` returns a write failure without assigning an I/O error. Cleanup at `:193` and `:199` reuses the primary error code and can clear replacement failures. Hot reload can consequently report a storage failure as “Success.”

**Investigation**

- [ ] Inject open, write, flush, and replacement failures followed by successful cleanup.

**Fix strategy**

- [ ] Capture the original failure before cleanup.
- [ ] Use a separate cleanup error code.

**Acceptance criteria**

- [ ] Every failed operation returns a meaningful non-success diagnostic.
- [ ] Successful cleanup cannot erase the original error.
- [ ] Existing reload rollback behavior remains intact.

### kanban/pending/59 config-integer-representable-bounds -bug.md

# Bound configuration integers before narrowing

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`libs/draxul-config/src/config_schema.cpp:539` applies only a lower bound for `ClampMin`, then narrows at `:553`. A value such as `chord_timeout_ms = 2147483648` becomes an invalid negative timeout on the supported integer representation.

**Investigation**

- [ ] Identify every minimum-only integer field and test representable boundaries.

**Fix strategy**

- [ ] Apply destination-type bounds before conversion.
- [ ] Keep configured minimum and fallback/clamping semantics explicit.

**Acceptance criteria**

- [ ] Values beyond `int` range cannot wrap into negative or unrelated settings.
- [ ] Ordinary values and documented minimum clamping remain unchanged.

### kanban/pending/60 megacity-route-pair-association -bug.md

# Preserve route-pair identity when failed routes are removed

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`plugins/megacity/product/draxul-megacity/src/semantic_city_layout.cpp:1172` compacts successful routes, but `:1258` indexes the original pair vector with the compacted index. An unroutable pair followed by a valid route can assign elevations from the wrong buildings.

**Investigation**

- [ ] Construct a failed route followed by successful routes with distinct layer heights.

**Fix strategy**

- [ ] Retain each successful result’s original pair identity through compaction.
- [ ] Compute endpoint elevations from that retained association.

**Acceptance criteria**

- [ ] Every route receives elevations from its own endpoints.
- [ ] Skipping failed routes preserves deterministic ordering and existing valid layouts.

### kanban/pending/61 conpty-exited-process-id -bug.md

# Stop publishing exited ConPTY process identifiers

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`libs/draxul-terminal-process/src/conpty_process.cpp:973` returns retained `dwProcessId` after the child exits or handles are closed. The server publishes that recyclable PID, unlike the POSIX implementation’s zero result after confirmed exit.

**Investigation**

- [ ] Follow PID publication through natural exit, abnormal exit, shutdown, and restart.

**Fix strategy**

- [ ] Return zero after confirmed exit and clear retired process identity.
- [ ] Keep unknown process-status handling distinct from confirmed exit.

**Acceptance criteria**

- [ ] Exited panes never advertise a stale live PID.
- [ ] Restart publishes the new PID.
- [ ] Coordinate status semantics with `kanban/pending/29 conpty-process-status-query-failure -bug.md`.

### kanban/pending/62 sdl-event-registration-failure -bug.md

# Check SDL3 event-registration failure correctly

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`libs/draxul-window/src/sdl_window.cpp:175` checks for `Uint32(-1)`, but [SDL3 returns zero on failure](https://wiki.libsdl.org/SDL3/SDL_RegisterEvents). Exhausted event registration therefore leaves unusable wake/file-dialog event IDs while initialization appears successful.

**Investigation**

- [ ] Exercise failed allocation of the two application event IDs.

**Fix strategy**

- [ ] Reject a zero result and unwind initialization through the existing error path.

**Acceptance criteria**

- [ ] Registration failure reports an actionable initialization error.
- [ ] Successful registration provides distinct working wake and file-dialog events.
- [ ] Verify both platform startup paths.

### kanban/pending/63 satview-ephemeris-row-commit -bug.md

# Commit ephemeris metadata only after accepting a row

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/core/satview_catalog.cpp:1353` updates source and frame before validating the frame and track horizon. A rejected final row can overwrite metadata describing previously accepted samples.

**Investigation**

- [ ] Append invalid-frame and invalid-horizon rows after valid samples for the same object.

**Fix strategy**

- [ ] Parse row metadata into temporary values.
- [ ] Commit metadata and sample data together only after complete validation.

**Acceptance criteria**

- [ ] Rejected rows leave accepted samples and their metadata unchanged.
- [ ] Valid rows retain intended source, frame, and horizon values.
- [ ] Define consistent handling for conflicting accepted frames.

### kanban/pending/64 satview-visible-marker-picking -bug.md

# Apply identical visibility and limits to marker drawing and picking

**Severity:** MEDIUM  
**Reported by:** Claude Fable 5.1

`plugins/satview/src/runtime/satview_runtime.cpp:871` applies the drawing cap after horizon rejection, while picking applies it at `:4962` before that rejection. With a finite limit and early below-horizon objects, visible later markers cannot be selected.

**Investigation**

- [ ] Build a ground-view snapshot with occluded objects preceding visible markers near the limit.

**Fix strategy**

- [ ] Share marker eligibility and limit ordering between rendering and picking.
- [ ] Preserve the selected-marker exception explicitly.

**Acceptance criteria**

- [ ] Every displayed eligible marker can be picked at its rendered position.
- [ ] Occluded markers do not consume the visible-marker budget.
- [ ] Unlimited, map, and existing selection behavior remain correct.

<model>gpt-6-astra</model>
