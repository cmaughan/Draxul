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
