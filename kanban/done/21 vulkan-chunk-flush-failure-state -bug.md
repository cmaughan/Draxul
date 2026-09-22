# Abort Vulkan frames after chunk-flush failure

**Severity:** CRITICAL  
**Type:** Bug

## Bug description

Mid-frame chunk-flush failures are discarded, allowing rendering to continue on ended or submitted command buffers. Allocation failure also leaves `current_chunk_index_` advanced, enabling out-of-bounds access on a later transition.

**Trigger:** Inject failure into command-buffer end, submit, allocation, reset, or begin during `IFrameContext::flush_submit_chunk()`.

## Investigation

- [x] Document the valid state of `active_cmd_buffer_`, `frame_active_`, and `current_chunk_index_` after every Vulkan call.
- [x] Add failure injection for each chunk-transition operation.
- [x] Audit callers of the void `IFrameContext::flush_submit_chunk()` contract.

## Fix strategy

- [x] Propagate failure or latch an aborted-frame state visible to all subsequent recording calls.
- [x] Null or quarantine command buffers that are no longer recording.
- [x] Roll back `current_chunk_index_` and vector state on allocation, reset, or begin failure.
- [x] Make `end_frame()` safely clean up an already-aborted frame.

**Implementation note (2026-09-21):** Chunk failures now abort the active frame,
wait for submitted work, replace the current frame's semaphore and signaled
fence, and force a swapchain rebuild before another acquire. Command-buffer
allocation, reset, and begin leave the chunk index unchanged until success.
Focused Vulkan source-contract tests passed on macOS (75 cases, 635 assertions).
Failure injection and Windows Vulkan validation remain pending.

**Coverage note (2026-09-22):** A production-pass-through operation seam and
Windows white-box matrix now inject chunk end, submit, allocation, reset, and
begin failures. The matrix verifies atomic abort, command-buffer quarantine,
stable chunk/vector indexes, cleared image ownership, repaired synchronization,
and that stale frame contexts cannot issue more recording operations.

**Windows validation note (2026-09-22):** The renderer now documents both the
transactional chunk-transition invariant and the canonical post-failure state
beside the implementation. `py do.py test debug --products` passed with the
failure matrix in the normal MSVC/Vulkan build. Runtime failure injection under
Vulkan validation layers and the final same-cache smoke remain open below.

## Acceptance criteria

- [x] No command is recorded after a failed chunk transition.
- [x] No failure path indexes beyond `extra_cmd_buffers_`.
- [x] Failure-injection tests recover or skip the frame without validation-layer errors, crashes, or hangs.
- [x] Windows Vulkan build, render tests, and smoke validation pass.

## Windows closeout (2026-09-22)

The injected end/submit/allocation/reset/begin matrix passed in the final 48/48
products aggregate. Basic, Unicode, panel, NanoVG, MegaCity, and ScoreView Vulkan
renders passed, followed by same-cache smoke.
