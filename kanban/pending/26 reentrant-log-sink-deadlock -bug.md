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
