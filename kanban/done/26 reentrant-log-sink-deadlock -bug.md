# Invoke log sinks outside the logger mutex

**Severity:** HIGH
**Type:** Bug

## Bug description

`log_message()` calls arbitrary sink code while holding the logger mutex, so recursive logging or sink reconfiguration deadlocks.

**Trigger:** A configured sink logs a second message, clears itself, or calls a logging configuration API.

## Investigation

- [x] Document which logger state must remain protected during stderr and file writes.
- [x] Add reentrant sink tests for recursive logging, clearing, and replacement.
- [x] Check shutdown interaction with a copied in-flight sink.

## Fix strategy

- [x] Copy the current sink while holding the mutex.
- [x] Release the mutex before invoking external sink code.
- [x] Retain safe serialization for internal file and stderr state.

## Acceptance criteria

- [x] Recursive and self-clearing sinks return without deadlock.
- [x] Concurrent sink replacement is race-free.
- [x] Existing filtering and capture tests continue to pass.
