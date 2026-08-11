# review-bugs-consensus.md

## Confirmed bugs

### CRITICAL

1. **ConPTY output-handle shutdown race**
   - **File:** `libs/draxul-terminal-process/src/conpty_process.cpp:620-651,871-879`
   - **Defect:** `shutdown()` closes and mutates the non-atomic `output_read_` before joining the reader that passes it to `ReadFile`, creating a data race and possible use of a closed or recycled handle.
   - **Trigger:** Close or restart a Windows terminal while its reader is between checking `reader_running_` and entering `ReadFile`.
   - **Fix:** Signal and cancel the reader, join it, and only then close and invalidate `output_read_`.
   - **Reported by:** Anthropic Claude Opus.

2. **Failed Vulkan chunk transition leaves invalid recording state**
   - **File:** `libs/draxul-renderer/src/vulkan/vk_renderer.cpp:275-278,952-981,1247-1385`
   - **Defect:** `FrameContext::flush_submit_chunk()` discards failure, allowing later rendering against an ended/submitted command buffer; allocation failure also leaves `current_chunk_index_` advanced and can lead to out-of-bounds vector access.
   - **Trigger:** `vkEndCommandBuffer`, `vkQueueSubmit`, command-buffer allocation, reset, or begin fails during a mid-frame chunk flush.
   - **Fix:** Propagate or latch failure so the frame is aborted immediately, null invalid state, and roll back the chunk index on every failed transition.
   - **Reported by:** Anthropic Claude Opus.
   - **Ruling:** Raised as HIGH, promoted to CRITICAL because the verified path includes out-of-bounds access and invalid Vulkan command-buffer use.

3. **Large CSI cursor parameters cause signed overflow**
   - **File:** `libs/draxul-terminal-core/src/terminal_core_csi.cpp:254-260,267-268,281-289,352-356`
   - **Defect:** Cursor and erase operations add an attacker-controlled positive `int` to an existing row or column without saturating first.
   - **Trigger:** At a nonzero cursor position, output such as `ESC[2147483647C` overflows `vt_.col + n`.
   - **Fix:** Parse or normalize CSI counts through checked, saturating helpers before arithmetic and clamp all affected operations.
   - **Reported by:** xAI Grok 4.5.
   - **Ruling:** Raised as MEDIUM, promoted to CRITICAL because the trigger reaches signed-overflow undefined behavior. `kanban/ice-box/05 csi-cursor-boundary -test.md` lacks extreme-value coverage, while `kanban/ice-box/07 cursor-index-signed-overflow -bug.md` concerns different renderer/grid index expressions.

### HIGH

4. **ConPTY launch hides unrelated desktop console windows**
   - **File:** `libs/draxul-terminal-process/src/conpty_process.cpp:124-175,546-549`
   - **Defect:** Every spawn polls all top-level desktop windows and hides every new `ConsoleWindowClass`, without checking process ownership.
   - **Trigger:** Any other application opens a console during Draxul’s eight-second post-spawn polling interval; the scan also starts when `CreateProcessW` later fails.
   - **Fix:** Remove the workaround or start it only after successful creation and restrict it to the child PID or verified descendants, stopping after the target is handled.
   - **Reported by:** Anthropic Claude Opus and OpenAI Codex GPT-5.6-sol.

5. **Unbounded CHT/CBT repeat loops can hang terminal processing**
   - **File:** `libs/draxul-terminal-core/src/terminal_core_csi.cpp:303-319`
   - **Defect:** CHT and CBT iterate once per untrusted CSI parameter even after the cursor reaches a boundary.
   - **Trigger:** A pane emits `ESC[2000000000I` or a similarly large CBT sequence, blocking the local UI or shared server pump.
   - **Fix:** Compute the final tab position in constant time or cap work to the number of reachable tab stops.
   - **Reported by:** Anthropic Claude Opus and xAI Grok 4.5.

6. **PTY dimensions diverge from the terminal grid above 320×200**
   - **File:** `libs/draxul-terminal-process/src/unix_pty_process.cpp:248-249,613-614`; `libs/draxul-terminal-process/src/conpty_process.cpp:478-479,813-814`; `libs/draxul-host/src/terminal_host_base.cpp:340-350`
   - **Defect:** Both PTY backends silently clamp to 320 columns and 200 rows while `TerminalHostBase` resizes the grid to the full viewport.
   - **Trigger:** A sufficiently large window or small font produces a grid wider than 320 or taller than 200.
   - **Fix:** Normalize dimensions once and apply the same values to the grid and PTY, preferably supporting the grid’s actual validated range.
   - **Reported by:** xAI Grok 4.5.

7. **Reentrant log sinks deadlock the logger**
   - **File:** `libs/draxul-types/src/log.cpp:303-337`
   - **Defect:** `log_message()` invokes arbitrary configured sink code while holding the same mutex required by logging and sink-configuration APIs.
   - **Trigger:** A sink logs recursively, calls `clear_log_sink()`, or reconfigures logging.
   - **Fix:** Copy the sink under the mutex, release the mutex, and invoke the copy afterward.
   - **Reported by:** OpenAI Codex GPT-5.6-sol.

8. **Scrollback and resize restoration discard OSC 8 hyperlinks**
   - **File:** `libs/draxul-host/src/local_terminal_host.cpp:43-64,138-142,234-277,757-775`
   - **Defect:** Restoration rebuilds cells through `Grid::set_cell()`, which resets `hyperlink_id`, rather than restoring the complete semantic cell.
   - **Trigger:** Display an OSC 8 link, then enter/leave scrollback or resize through a restoration path.
   - **Fix:** Add a full-cell restoration API or explicitly restore hyperlink IDs after each reconstructed leader cell.
   - **Reported by:** OpenAI Codex GPT-5.6-sol.

### MEDIUM

9. **Overlapping wide-glyph writes leave orphan continuation cells**
   - **File:** `libs/draxul-grid/src/grid.cpp:230-288`
   - **Defect:** Writing a wide glyph over the leader of an adjacent wide pair converts that leader into a continuation without clearing the old pair’s far-side continuation.
   - **Trigger:** A wide glyph occupies columns 5–6 and a subsequent wide glyph is written at column 4.
   - **Fix:** Clear the displaced leader’s continuation before overwriting it, then normalize affected neighbouring pairs.
   - **Reported by:** Anthropic Claude Opus.

10. **Failed Windows process-status query is treated as exit code zero**
    - **File:** `libs/draxul-terminal-process/src/conpty_process.cpp:705-720`; consequence at `libs/draxul-server/src/server_kernel.cpp:3287-3319`
    - **Defect:** `GetExitCodeProcess` failure leaves the initialized value `0`, caches it as a clean exit, and can make the server remove a still-live terminal.
    - **Trigger:** The status API transiently fails for a process handle during the server cleanup loop.
    - **Fix:** Preserve an unknown/running state on query failure, preferably through a tri-state result; merely returning `false` from `is_running()` would still permit erroneous cleanup.
    - **Reported by:** Anthropic Claude Opus.

11. **macOS screenshot updater resolves a nonexistent executable**
    - **File:** `scripts/update_screenshot.py:37-40`
    - **Defect:** `draxul_path()` returns `build/draxul` on macOS instead of the application-bundle executable.
    - **Trigger:** Run the screenshot updater after a standard macOS build.
    - **Fix:** Return `build/draxul.app/Contents/MacOS/draxul` when `sys.platform == "darwin"`.
    - **Reported by:** OpenAI Codex GPT-5.6-sol.

12. **macOS log helper launches a nonexistent executable**
    - **File:** `scripts/store_logs.sh:10`
    - **Defect:** The helper invokes `build/draxul`, which is not the standard macOS build output.
    - **Trigger:** Run `scripts/store_logs.sh` after a normal macOS build.
    - **Fix:** Resolve and launch `build/draxul.app/Contents/MacOS/draxul`, with a clear missing-build error.
    - **Reported by:** OpenAI Codex GPT-5.6-sol.

13. **Window-failure integration assertion is unconditional**
    - **File:** `tests/startup_rollback_tests.cpp:131`
    - **Defect:** `|| true` makes the assertion pass even if `init_error()` stops identifying window creation as the failure.
    - **Trigger:** Regress or replace the window-creation error text while leaving it nonempty.
    - **Fix:** Remove `|| true` and assert the stable expected error text.
    - **Reported by:** OpenAI Codex GPT-5.6-sol.

## Dropped finding

- **Server scrollback budget double-reservation:** Not a bug. `ScrollbackBuffer::resize()` allocates replacement storage while retaining the old storage until commit, so old-plus-new peak accounting is deliberate. `kanban/done/25 server-resource-bounds -bug.md` explicitly records this strong-allocation guarantee.

## Interdependencies

- `22 csi-cursor-parameter-overflow -bug.md` and `24 csi-tab-repeat-hang -bug.md` should share one checked CSI-parameter normalization design.
- `20 conpty-output-handle-shutdown-race -bug.md`, `23 conpty-desktop-window-suppression -bug.md`, and `29 conpty-process-status-query-failure -bug.md` touch the same implementation and should be sequenced to avoid merge conflicts.
- `27 terminal-cell-restoration-preserves-hyperlinks -bug.md` and `28 grid-wide-glyph-overlap-orphan -bug.md` both affect cell restoration invariants; a full-cell restoration API must preserve valid wide-pair structure.
- `30 macos-screenshot-executable-path -bug.md` and `31 macos-store-logs-executable-path -bug.md` should use the same documented bundle-path convention.

<codex:gpt-5.6-sol>

### kanban/pending/20 conpty-output-handle-shutdown-race -bug.md

# Prevent ConPTY output-handle shutdown races

**Severity:** CRITICAL  
**Type:** Bug

## Bug description

`ConPtyProcess::shutdown()` closes and invalidates `output_read_` before joining the reader thread. The reader can consequently access the non-atomic member concurrently or call `ReadFile` with a closed or recycled handle.

**Trigger:** Close or restart a Windows terminal while the reader is transitioning into `ReadFile`.

## Investigation

- [ ] Map ownership and mutation of `output_read_` across spawn, reader, shutdown, and failure cleanup.
- [ ] Verify `CancelSynchronousIo` behavior when the reader has not yet entered `ReadFile`.
- [ ] Add deterministic synchronization or stress coverage for the check-to-read shutdown window.

## Fix strategy

- [ ] Set the stop flag and cancel any pending reader I/O.
- [ ] Join the reader thread before closing or invalidating `output_read_`.
- [ ] Ensure all spawn-failure and repeated-shutdown paths preserve the same lifetime invariant.

## Acceptance criteria

- [ ] The output handle is never closed or mutated while the reader can access it.
- [ ] Repeated spawn, active-output shutdown, and restart stress tests complete without invalid-handle reads, crashes, or hangs.
- [ ] Windows terminal-process tests and the full Windows validation gate pass.

### kanban/pending/21 vulkan-chunk-flush-failure-state -bug.md

# Abort Vulkan frames after chunk-flush failure

**Severity:** CRITICAL  
**Type:** Bug

## Bug description

Mid-frame chunk-flush failures are discarded, allowing rendering to continue on ended or submitted command buffers. Allocation failure also leaves `current_chunk_index_` advanced, enabling out-of-bounds access on a later transition.

**Trigger:** Inject failure into command-buffer end, submit, allocation, reset, or begin during `IFrameContext::flush_submit_chunk()`.

## Investigation

- [ ] Document the valid state of `active_cmd_buffer_`, `frame_active_`, and `current_chunk_index_` after every Vulkan call.
- [ ] Add failure injection for each chunk-transition operation.
- [ ] Audit callers of the void `IFrameContext::flush_submit_chunk()` contract.

## Fix strategy

- [ ] Propagate failure or latch an aborted-frame state visible to all subsequent recording calls.
- [ ] Null or quarantine command buffers that are no longer recording.
- [ ] Roll back `current_chunk_index_` and vector state on allocation, reset, or begin failure.
- [ ] Make `end_frame()` safely clean up an already-aborted frame.

## Acceptance criteria

- [ ] No command is recorded after a failed chunk transition.
- [ ] No failure path indexes beyond `extra_cmd_buffers_`.
- [ ] Failure-injection tests recover or skip the frame without validation-layer errors, crashes, or hangs.
- [ ] Windows Vulkan build, render tests, and smoke validation pass.

### kanban/pending/22 csi-cursor-parameter-overflow -bug.md

# Saturate large CSI cursor parameters

**Severity:** CRITICAL  
**Type:** Bug

## Bug description

Several CSI cursor and erase operations add an untrusted positive `int` to the current row or column before clamping, permitting signed-overflow undefined behavior.

**Trigger:** Emit a maximum-width parameter such as `ESC[2147483647C` while the cursor column is nonzero.

## Investigation

- [ ] Audit every CSI arithmetic expression involving parsed parameters.
- [ ] Add extreme-value tests for CUD, CUF, CNL, origin-mode CUP/HVP, and ECH.
- [ ] Confirm out-of-range `std::from_chars` behavior is handled consistently.

## Fix strategy

- [ ] Introduce checked or saturating helpers for relative and absolute CSI coordinates.
- [ ] Clamp before arithmetic or use a wider unsigned intermediate with validated bounds.
- [ ] Keep the implementation coordinated with `kanban/pending/24 csi-tab-repeat-hang -bug.md`.

## Acceptance criteria

- [ ] `INT_MAX` parameters never overflow and always produce a valid grid coordinate or bounded range.
- [ ] UBSan coverage reports no signed-overflow failures for hostile CSI inputs.
- [ ] Existing terminal-core behavior and CSI tests remain passing.

### kanban/pending/23 conpty-desktop-window-suppression -bug.md

# Restrict ConPTY console-window suppression

**Severity:** HIGH  
**Type:** Bug

## Bug description

ConPTY spawn polls every top-level desktop window for eight seconds and hides any new `ConsoleWindowClass`, including windows owned by unrelated applications.

**Trigger:** Open another application’s console within eight seconds of a Draxul terminal spawn or failed spawn attempt.

## Investigation

- [ ] Determine why visible consoles still require suppression with current ConPTY creation flags.
- [ ] Trace the spawned child and conhost process ownership relationships.
- [ ] Add coverage for failed spawn and concurrent unrelated console creation.

## Fix strategy

- [ ] Prefer removing the global suppression workaround.
- [ ] If suppression remains necessary, start only after successful process creation.
- [ ] Filter candidates to the spawned PID or verified descendants and stop once handled.
- [ ] Avoid detached polling that outlives its owning spawn operation unnecessarily.

## Acceptance criteria

- [ ] Draxul never hides a console window owned by an unrelated process.
- [ ] Failed spawns start no suppression worker.
- [ ] Terminal spawning remains free of unwanted Draxul-owned console flashes.
- [ ] Windows spawn and smoke tests pass.

### kanban/pending/24 csi-tab-repeat-hang -bug.md

# Bound CSI tab-repeat processing

**Severity:** HIGH  
**Type:** Bug

## Bug description

CHT and CBT process one loop iteration per untrusted CSI count even after reaching the grid edge, allowing a short output sequence to stall terminal processing for billions of iterations.

**Trigger:** Emit `ESC[2000000000I` or the equivalent large CBT sequence.

## Investigation

- [ ] Add focused terminal-core tests for zero, normal, boundary, and `INT_MAX` repeat counts.
- [ ] Verify expected tab-stop semantics at columns zero and the final grid column.
- [ ] Measure that hostile inputs complete within a strict time bound.

## Fix strategy

- [ ] Compute the destination tab stop in constant time where practical.
- [ ] Otherwise cap iterations to the number of reachable tab stops.
- [ ] Reuse parameter normalization from `kanban/pending/22 csi-cursor-parameter-overflow -bug.md`.

## Acceptance criteria

- [ ] Runtime is bounded by grid dimensions rather than the supplied repeat count.
- [ ] Large CHT and CBT sequences leave the cursor at the correct boundary.
- [ ] Local and server terminal-core tests complete without stalls.

### kanban/pending/25 pty-grid-dimension-desynchronization -bug.md

# Keep PTY and terminal-grid dimensions synchronized

**Severity:** HIGH  
**Type:** Bug

## Bug description

Windows and Unix PTY backends clamp dimensions to 320×200 while the terminal grid accepts the full viewport, causing shell wrapping and cursor state to disagree with rendering.

**Trigger:** Use a large display or small font that produces more than 320 columns or 200 rows.

## Investigation

- [ ] Identify all grid and PTY dimension limits and their platform type constraints.
- [ ] Confirm realistic maximum viewport sizes on Windows and macOS.
- [ ] Add tests that resize beyond both current clamp thresholds.

## Fix strategy

- [ ] Define one shared validated terminal-dimension policy.
- [ ] Apply identical normalized dimensions to `TerminalCore`, Grid, ConPTY, and Unix PTY.
- [ ] Preserve both backend implementations and surface resize failure rather than silently diverging.

## Acceptance criteria

- [ ] The child-reported terminal size always matches the rendered grid size.
- [ ] Widths above 320 and heights above 200 work up to the documented supported limit.
- [ ] Windows ConPTY and macOS Unix PTY resize validation passes.

### kanban/pending/26 reentrant-log-sink-deadlock -bug.md

# Invoke log sinks outside the logger mutex

**Severity:** HIGH  
**Type:** Bug

## Bug description

`log_message()` calls arbitrary sink code while holding the logger mutex, so recursive logging or sink reconfiguration deadlocks.

**Trigger:** A configured sink logs a second message, clears itself, or calls a logging configuration API.

## Investigation

- [ ] Document which logger state must remain protected during stderr and file writes.
- [ ] Add reentrant sink tests for recursive logging, clearing, and replacement.
- [ ] Check shutdown interaction with a copied in-flight sink.

## Fix strategy

- [ ] Copy the current sink while holding the mutex.
- [ ] Release the mutex before invoking external sink code.
- [ ] Retain safe serialization for internal file and stderr state.

## Acceptance criteria

- [ ] Recursive and self-clearing sinks return without deadlock.
- [ ] Concurrent sink replacement is race-free.
- [ ] Existing filtering and capture tests continue to pass.

### kanban/pending/27 terminal-cell-restoration-preserves-hyperlinks -bug.md

# Preserve OSC 8 hyperlinks during terminal restoration

**Severity:** HIGH  
**Type:** Bug

## Bug description

Local scrollback and resize restoration reconstruct cells through `Grid::set_cell()`, which clears `hyperlink_id`, so previously valid OSC 8 links stop working.

**Trigger:** Display an OSC 8 link and then restore cells by scrolling back, resizing, or pulling scrollback rows into a grown viewport.

## Investigation

- [ ] Enumerate every `Cell`-to-Grid restoration path in `LocalTerminalHost` and `ScrollbackBuffer`.
- [ ] Identify all semantic fields lost by the current reconstruction API.
- [ ] Add link-click and effective-link-ID tests across scrollback and resize operations.

## Fix strategy

- [ ] Add a safe full-cell restoration API or restore hyperlink IDs explicitly after leader cells.
- [ ] Preserve wide-cell invariants while restoring semantic metadata.
- [ ] Coordinate adjacent-cell behavior with `kanban/pending/28 grid-wide-glyph-overlap-orphan -bug.md`.

## Acceptance criteria

- [ ] OSC 8 links remain resolvable and clickable after scrollback round trips and viewport resize.
- [ ] Wide hyperlinks retain correct leader and continuation metadata.
- [ ] Existing URL detection, selection, and terminal restoration tests pass.

### kanban/pending/28 grid-wide-glyph-overlap-orphan -bug.md

# Clear orphaned wide-glyph continuations

**Severity:** MEDIUM  
**Type:** Bug

## Bug description

Writing a wide glyph over the leader of an adjacent wide pair leaves the displaced pair’s far-side continuation cell orphaned.

**Trigger:** Store a wide glyph at columns 5–6 and then write another wide glyph at column 4.

## Investigation

- [ ] Add tests for wide-over-wide writes from both directions and at row edges.
- [ ] Verify dirty tracking for every cleared or converted cell.
- [ ] Audit other cell mutation paths for split leader/continuation pairs.

## Fix strategy

- [ ] Detect when the destination continuation column is currently a wide-glyph leader.
- [ ] Clear and dirty that leader’s former continuation before overwriting it.
- [ ] Normalize all locally affected wide-pair metadata after mutation.

## Acceptance criteria

- [ ] No cell has `double_width_cont` without an immediately preceding valid leader.
- [ ] Overlapping wide writes render and serialize the expected cells.
- [ ] Grid, Unicode, observation, and render-snapshot tests pass.

### kanban/pending/29 conpty-process-status-query-failure -bug.md

# Preserve unknown ConPTY process status

**Severity:** MEDIUM  
**Type:** Bug

## Bug description

`ConPtyProcess::is_running()` ignores `GetExitCodeProcess` failure, caches the initialized value zero, and permits the server to treat an unknown status as a clean exit.

**Trigger:** `GetExitCodeProcess` fails transiently while the shared server evaluates a started terminal.

## Investigation

- [ ] Add an injectable process-status seam covering active, exited, and query-failed states.
- [ ] Trace all callers that currently interpret `false` or a missing exit code as clean exit.
- [ ] Verify status checks cannot race process-handle retirement.

## Fix strategy

- [ ] Do not update `last_exit_code_` when the Windows query fails.
- [ ] Represent query failure as unknown or conservatively running.
- [ ] Require a confirmed zero exit before automatic topology cleanup.

## Acceptance criteria

- [ ] Query failure cannot close or remove a live terminal.
- [ ] Confirmed zero exits still receive normal cleanup.
- [ ] Nonzero and unknown exits remain available for inspection or recovery.
- [ ] Server lifecycle and Windows terminal-process tests pass.

### kanban/pending/30 macos-screenshot-executable-path -bug.md

# Resolve the macOS bundle executable in the screenshot updater

**Severity:** MEDIUM  
**Type:** Bug

## Bug description

`scripts/update_screenshot.py` looks for `build/draxul` on macOS although the standard build produces `build/draxul.app/Contents/MacOS/draxul`.

**Trigger:** Run the screenshot updater after a standard macOS CMake build.

## Investigation

- [ ] Confirm executable paths for Windows, macOS, and other supported script platforms.
- [ ] Add path-resolution tests with each supported `sys.platform` value.
- [ ] Check whether multi-config build directories require additional handling.

## Fix strategy

- [ ] Return the application-bundle executable for Darwin.
- [ ] Preserve existing Windows behavior and explicitly define any supported non-macOS Unix path.
- [ ] Keep missing-build diagnostics pointed at the resolved path.

## Acceptance criteria

- [ ] The updater finds and launches a standard macOS build.
- [ ] Platform path-resolution tests cover Darwin and Windows.
- [ ] Existing screenshot conversion behavior remains unchanged.

### kanban/pending/31 macos-store-logs-executable-path -bug.md

# Launch the macOS bundle executable from store_logs.sh

**Severity:** MEDIUM  
**Type:** Bug

## Bug description

`scripts/store_logs.sh` invokes nonexistent `build/draxul` instead of the executable inside the macOS application bundle.

**Trigger:** Run the log-storage helper after a normal macOS build.

## Investigation

- [ ] Confirm the script’s supported platforms and documented invocation.
- [ ] Compare its path resolution with other macOS helper scripts.
- [ ] Verify paths containing spaces are handled safely.

## Fix strategy

- [ ] Resolve `build/draxul.app/Contents/MacOS/draxul` on macOS.
- [ ] Add a clear executable-not-found diagnostic before launch.
- [ ] Keep output redirection and caller-supplied log paths intact.

## Acceptance criteria

- [ ] The helper launches a standard macOS build and writes the requested log file.
- [ ] Missing builds fail with a clear actionable message.
- [ ] Paths containing spaces work correctly.

### kanban/pending/32 startup-window-error-assertion -bug.md

# Make the startup window-error assertion effective

**Severity:** MEDIUM  
**Type:** Bug

## Bug description

The startup rollback integration test appends `|| true` to its window-error assertion, making that assertion unconditional.

**Trigger:** Change `App::init_error()` so a window-creation failure returns unrelated nonempty text.

## Investigation

- [ ] Confirm the stable user-facing or semantic error contract for window creation failure.
- [ ] Search the rollback suite for other unconditional or tautological assertions.
- [ ] Verify the injected null window factory exercises the intended initialization stage.

## Fix strategy

- [ ] Remove `|| true`.
- [ ] Assert the stable expected window-failure text or error category.
- [ ] Keep the existing failure-return and clean-destruction assertions.

## Acceptance criteria

- [ ] The test fails when the window-specific error is absent.
- [ ] The test passes for the current documented window-creation failure.
- [ ] Startup rollback and app test suites pass.
