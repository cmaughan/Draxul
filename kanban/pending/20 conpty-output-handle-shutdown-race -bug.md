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
